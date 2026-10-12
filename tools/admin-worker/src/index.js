/**
 * GoSurveyCAD admin dashboard — REQ-402, ADR-068.
 *
 * Read-only view over the telemetry and accounts D1 databases, plus a link out to Cloudflare's
 * own Workers Logs for live request/error diagnostics. Authentication is Cloudflare Access,
 * configured on the `admin.gosurveycad.com` route outside this Worker (ADR-068 (a)) -- there is
 * deliberately no password/session logic here. Every DB query below is a SELECT; this Worker has
 * no write path to either database.
 *
 * Routes:
 *   GET /                                  -> dashboard page
 *   GET /api/telemetry/summary             -> installs, 7/30-day actives, version/channel split
 *   GET /api/telemetry/startup-reports      -> paginated, filterable by version/stage
 *   GET /api/telemetry/crash-reports        -> paginated, filterable by version/reason
 *   GET /api/accounts                       -> paginated accounts list
 */

const PAGE_SIZE_DEFAULT = 50;
const PAGE_SIZE_MAX = 200;

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json", "cache-control": "no-store" },
  });
}

function html(body) {
  return new Response(body, {
    headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" },
  });
}

function pagingParams(url) {
  const limit = Math.min(
    PAGE_SIZE_MAX,
    Math.max(1, Number(url.searchParams.get("limit")) || PAGE_SIZE_DEFAULT)
  );
  const offset = Math.max(0, Number(url.searchParams.get("offset")) || 0);
  return { limit, offset };
}

async function telemetrySummary(env) {
  const db = env.TELEMETRY_DB;

  const installs = await db
    .prepare("SELECT COUNT(*) AS n FROM pings WHERE event = 'install'")
    .first();

  const active = await db
    .prepare(
      `SELECT
         COUNT(DISTINCT CASE WHEN day >= date('now', '-7 days')  THEN install_id END) AS active_7d,
         COUNT(DISTINCT CASE WHEN day >= date('now', '-30 days') THEN install_id END) AS active_30d
       FROM pings WHERE event = 'active'`
    )
    .first();

  const byVersion = await db
    .prepare(
      `SELECT version, COUNT(DISTINCT install_id) AS users
       FROM pings WHERE day >= date('now', '-30 days')
       GROUP BY version ORDER BY users DESC LIMIT 20`
    )
    .all();

  const byChannel = await db
    .prepare(
      `SELECT channel, COUNT(DISTINCT install_id) AS users
       FROM pings WHERE day >= date('now', '-30 days')
       GROUP BY channel`
    )
    .all();

  return {
    installs: installs?.n ?? 0,
    active7d: active?.active_7d ?? 0,
    active30d: active?.active_30d ?? 0,
    byVersion: byVersion.results ?? [],
    byChannel: byChannel.results ?? [],
  };
}

async function listReports(env, table, allowedFilterColumns, url) {
  const db = env.TELEMETRY_DB;
  const { limit, offset } = pagingParams(url);

  const where = [];
  const binds = [];
  for (const col of allowedFilterColumns) {
    const value = url.searchParams.get(col);
    if (value) {
      where.push(`${col} = ?`);
      binds.push(value);
    }
  }
  const whereSql = where.length ? `WHERE ${where.join(" AND ")}` : "";

  const rows = await db
    .prepare(
      `SELECT * FROM ${table} ${whereSql} ORDER BY id DESC LIMIT ? OFFSET ?`
    )
    .bind(...binds, limit, offset)
    .all();

  const total = await db
    .prepare(`SELECT COUNT(*) AS n FROM ${table} ${whereSql}`)
    .bind(...binds)
    .first();

  return { rows: rows.results ?? [], total: total?.n ?? 0, limit, offset };
}

async function listAccounts(env, url) {
  const db = env.ACCOUNTS_DB;
  const { limit, offset } = pagingParams(url);

  const rows = await db
    .prepare(
      `SELECT auth0_sub, email, name, created_at, updated_at
       FROM users ORDER BY created_at DESC LIMIT ? OFFSET ?`
    )
    .bind(limit, offset)
    .all();

  const total = await db.prepare("SELECT COUNT(*) AS n FROM users").first();

  return { rows: rows.results ?? [], total: total?.n ?? 0, limit, offset };
}

const DASHBOARD_HTML = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="robots" content="noindex, nofollow">
<title>GoSurveyCAD admin</title>
<style>
  :root { color-scheme: dark; }
  body { font-family: system-ui, sans-serif; background: #15181c; color: #e4e6eb; margin: 0; padding: 2rem; }
  h1 { font-size: 1.2rem; margin: 0 0 1.5rem; }
  h2 { font-size: 1rem; color: #9aa4b2; margin: 2rem 0 0.5rem; }
  table { border-collapse: collapse; width: 100%; font-size: 0.85rem; }
  th, td { text-align: left; padding: 0.35rem 0.6rem; border-bottom: 1px solid #2a2f36; }
  th { color: #9aa4b2; font-weight: 600; }
  .stats { display: flex; gap: 2rem; }
  .stat { background: #1c2026; border-radius: 6px; padding: 0.75rem 1.25rem; }
  .stat .n { font-size: 1.6rem; font-weight: 700; display: block; }
  .stat .label { font-size: 0.75rem; color: #9aa4b2; }
  .report { white-space: pre-wrap; font-family: ui-monospace, monospace; font-size: 0.75rem; max-width: 60ch; overflow: hidden; text-overflow: ellipsis; }
  a { color: #6cb6ff; }
  .tabs { margin-bottom: 1rem; }
  .tabs button { background: #1c2026; color: #e4e6eb; border: 1px solid #2a2f36; padding: 0.4rem 0.9rem; cursor: pointer; }
  .tabs button.active { background: #2a2f36; }
  section { display: none; }
  section.active { display: block; }
</style>
</head>
<body>
<h1>GoSurveyCAD admin</h1>
<div class="tabs">
  <button data-tab="telemetry" class="active">Telemetry</button>
  <button data-tab="startup">Startup failures</button>
  <button data-tab="crash">Crash reports</button>
  <button data-tab="accounts">Accounts</button>
</div>

<section id="telemetry" class="active">
  <div class="stats" id="telemetry-stats"></div>
  <h2>Versions (30d)</h2>
  <table id="telemetry-versions"></table>
  <h2>Channels (30d)</h2>
  <table id="telemetry-channels"></table>
</section>

<section id="startup">
  <h2>Startup failure reports <a href="https://dash.cloudflare.com" target="_blank">(live Workers Logs &rarr;)</a></h2>
  <table id="startup-table"></table>
</section>

<section id="crash">
  <h2>Crash reports <a href="https://dash.cloudflare.com" target="_blank">(live Workers Logs &rarr;)</a></h2>
  <table id="crash-table"></table>
</section>

<section id="accounts">
  <h2>Accounts</h2>
  <table id="accounts-table"></table>
</section>

<script>
function el(tag, attrs, text) {
  const e = document.createElement(tag);
  if (attrs) for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (text !== undefined) e.textContent = text;
  return e;
}

function renderTable(table, columns, rows, cellFn) {
  table.innerHTML = "";
  const thead = el("thead");
  const headRow = el("tr");
  for (const c of columns) headRow.appendChild(el("th", null, c));
  thead.appendChild(headRow);
  table.appendChild(thead);
  const tbody = el("tbody");
  for (const row of rows) {
    const tr = el("tr");
    for (const c of columns) tr.appendChild(cellFn ? cellFn(c, row) : el("td", null, String(row[c] ?? "")));
    tbody.appendChild(tr);
  }
  table.appendChild(tbody);
}

async function loadTelemetry() {
  const r = await fetch("/api/telemetry/summary").then(r => r.json());
  const stats = document.getElementById("telemetry-stats");
  stats.innerHTML = "";
  const add = (label, n) => {
    const d = el("div", { class: "stat" });
    d.appendChild(el("span", { class: "n" }, String(n)));
    d.appendChild(el("span", { class: "label" }, label));
    stats.appendChild(d);
  };
  add("Installs (all time)", r.installs);
  add("Active (7d)", r.active7d);
  add("Active (30d)", r.active30d);
  renderTable(document.getElementById("telemetry-versions"), ["version", "users"], r.byVersion);
  renderTable(document.getElementById("telemetry-channels"), ["channel", "users"], r.byChannel);
}

async function loadReports(path, tableId, columns) {
  const r = await fetch(path).then(r => r.json());
  renderTable(document.getElementById(tableId), columns, r.rows, (c, row) => {
    if (c === "report") {
      const td = el("td", { class: "report", title: row[c] }, String(row[c] ?? "").slice(0, 200));
      return td;
    }
    return el("td", null, String(row[c] ?? ""));
  });
}

async function loadAccounts() {
  await loadReports("/api/accounts", "accounts-table", ["email", "name", "created_at", "updated_at"]);
}

for (const btn of document.querySelectorAll(".tabs button")) {
  btn.addEventListener("click", () => {
    for (const b of document.querySelectorAll(".tabs button")) b.classList.remove("active");
    for (const s of document.querySelectorAll("section")) s.classList.remove("active");
    btn.classList.add("active");
    document.getElementById(btn.dataset.tab).classList.add("active");
  });
}

loadTelemetry();
loadReports("/api/telemetry/startup-reports", "startup-table", ["ts", "version", "channel", "os", "stage", "country", "report"]);
loadReports("/api/telemetry/crash-reports", "crash-table", ["ts", "version", "channel", "os", "reason", "country", "report"]);
loadAccounts();
</script>
</body>
</html>`;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === "/" && request.method === "GET") {
      return html(DASHBOARD_HTML);
    }

    if (url.pathname === "/api/telemetry/summary") {
      return json(await telemetrySummary(env));
    }

    if (url.pathname === "/api/telemetry/startup-reports") {
      return json(await listReports(env, "startup_reports", ["version", "stage"], url));
    }

    if (url.pathname === "/api/telemetry/crash-reports") {
      return json(await listReports(env, "crash_reports", ["version", "reason"], url));
    }

    if (url.pathname === "/api/accounts") {
      return json(await listAccounts(env, url));
    }

    return json({ error: "not found" }, 404);
  },
};
