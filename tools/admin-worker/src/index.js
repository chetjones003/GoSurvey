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
 *   GET /api/telemetry/daily               -> installs/day and active installs/day, last 30 days
 *   GET /api/telemetry/startup-reports      -> paginated, filterable by version/stage
 *   GET /api/telemetry/crash-reports        -> paginated, filterable by version/reason
 *   GET /api/accounts                       -> paginated accounts list
 *
 * The dashboard page polls every REFRESH_MS so numbers/tables/charts stay current without a
 * manual reload -- there is no push channel, this is a small owner-only page, not worth one.
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

async function telemetryDaily(env, url) {
  const db = env.TELEMETRY_DB;
  const days = [7, 14, 30].includes(Number(url.searchParams.get("days")))
    ? Number(url.searchParams.get("days"))
    : 30;
  const sinceClause = `date('now', '-${days} days')`;

  const installs = await db
    .prepare(
      `SELECT day, COUNT(*) AS n FROM pings
       WHERE event = 'install' AND day >= ${sinceClause}
       GROUP BY day ORDER BY day`
    )
    .all();

  const active = await db
    .prepare(
      `SELECT day, COUNT(DISTINCT install_id) AS n FROM pings
       WHERE event = 'active' AND day >= ${sinceClause}
       GROUP BY day ORDER BY day`
    )
    .all();

  return { days, installs: installs.results ?? [], active: active.results ?? [] };
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
      `SELECT auth0_sub, email, tier, created_at
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
  .tabs { margin-bottom: 1rem; display: flex; align-items: center; gap: 1rem; }
  .tabs button { background: #1c2026; color: #e4e6eb; border: 1px solid #2a2f36; padding: 0.4rem 0.9rem; cursor: pointer; }
  .tabs button.active { background: #2a2f36; }
  .live { font-size: 0.75rem; color: #6ad68a; margin-left: auto; }
  .live .dot { display: inline-block; width: 6px; height: 6px; border-radius: 50%; background: #6ad68a; margin-right: 0.35rem; animation: pulse 2s infinite; }
  @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
  section { display: none; }
  section.active { display: block; }
  .chart-wrap { background: #1c2026; border-radius: 6px; padding: 1rem; margin: 0.5rem 0 1.5rem; position: relative; }
  .chart-wrap svg { display: block; width: 100%; height: 160px; cursor: crosshair; }
  .chart-legend { display: flex; align-items: center; gap: 1.25rem; font-size: 0.75rem; color: #9aa4b2; margin-top: 0.5rem; }
  .chart-legend span.swatch { display: inline-block; width: 9px; height: 9px; border-radius: 2px; margin-right: 0.35rem; }
  .range-btns { margin-left: auto; display: flex; gap: 0.25rem; }
  .range-btns button { background: #15181c; color: #9aa4b2; border: 1px solid #2a2f36; padding: 0.15rem 0.5rem; font-size: 0.75rem; cursor: pointer; border-radius: 3px; }
  .range-btns button.active { background: #2a2f36; color: #e4e6eb; }
  .chart-tip { position: absolute; pointer-events: none; background: #0d0f12; border: 1px solid #2a2f36; border-radius: 4px; padding: 0.3rem 0.5rem; font-size: 0.7rem; white-space: nowrap; display: none; }
  th[data-sort] { cursor: pointer; user-select: none; }
  th[data-sort]:after { content: ""; margin-left: 0.3rem; opacity: 0.4; }
  th[data-sort="asc"]:after { content: "\\2191"; opacity: 1; }
  th[data-sort="desc"]:after { content: "\\2193"; opacity: 1; }
</style>
</head>
<body>
<h1>GoSurveyCAD admin</h1>
<div class="tabs">
  <button data-tab="telemetry" class="active">Telemetry</button>
  <button data-tab="startup">Startup failures</button>
  <button data-tab="crash">Crash reports</button>
  <button data-tab="accounts">Accounts</button>
  <span class="live"><span class="dot"></span>Live &mdash; refreshes every <span id="refresh-secs"></span>s</span>
</div>

<section id="telemetry" class="active">
  <div class="stats" id="telemetry-stats"></div>
  <h2>Installs &amp; active installs per day</h2>
  <div class="chart-wrap">
    <svg id="telemetry-chart" viewBox="0 0 600 160" preserveAspectRatio="none"></svg>
    <div class="chart-tip" id="chart-tip"></div>
    <div class="chart-legend">
      <span><span class="swatch" style="background:#6cb6ff"></span>Installs</span>
      <span><span class="swatch" style="background:#6ad68a"></span>Active</span>
      <div class="range-btns" id="chart-range">
        <button data-days="7">7d</button>
        <button data-days="14">14d</button>
        <button data-days="30" class="active">30d</button>
      </div>
    </div>
  </div>
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

// Click a header to sort by that column: none -> asc -> desc -> none. Sort state lives on the
// table element itself so repeated calls (polling) keep whatever order the user picked.
function renderTable(table, columns, rows, cellFn) {
  const sortKey = table.dataset.sortKey || "";
  const sortDir = table.dataset.sortDir || "";

  let sorted = rows;
  if (sortKey && sortDir) {
    sorted = rows.slice().sort((a, b) => {
      const av = a[sortKey], bv = b[sortKey];
      const cmp = av === bv ? 0 : (av ?? "") < (bv ?? "") ? -1 : 1;
      return sortDir === "asc" ? cmp : -cmp;
    });
  }

  table.innerHTML = "";
  const thead = el("thead");
  const headRow = el("tr");
  for (const c of columns) {
    const th = el("th", { "data-sort": c === sortKey ? sortDir : "" }, c);
    th.addEventListener("click", () => {
      if (table.dataset.sortKey !== c) {
        table.dataset.sortKey = c;
        table.dataset.sortDir = "asc";
      } else if (table.dataset.sortDir === "asc") {
        table.dataset.sortDir = "desc";
      } else {
        table.dataset.sortKey = "";
        table.dataset.sortDir = "";
      }
      renderTable(table, columns, rows, cellFn);
    });
    headRow.appendChild(th);
  }
  thead.appendChild(headRow);
  table.appendChild(thead);
  const tbody = el("tbody");
  for (const row of sorted) {
    const tr = el("tr");
    for (const c of columns) tr.appendChild(cellFn ? cellFn(c, row) : el("td", null, String(row[c] ?? "")));
    tbody.appendChild(tr);
  }
  table.appendChild(tbody);
}

const SVG_NS = "http://www.w3.org/2000/svg";
function svgEl(tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  return e;
}

// Renders two day-keyed series as line charts sharing one y-scale, zero-filled over rangeDays
// days, with a hover crosshair + tooltip reading out both values for the nearest day.
function renderDailyChart(svg, tip, seriesA, seriesB, colorA, colorB, rangeDays) {
  const W = 600, H = 160, PAD = 8;
  svg.innerHTML = "";

  const days = [];
  const today = new Date();
  for (let i = rangeDays - 1; i >= 0; i--) {
    const d = new Date(today);
    d.setUTCDate(d.getUTCDate() - i);
    days.push(d.toISOString().slice(0, 10));
  }
  const toMap = (rows) => Object.fromEntries(rows.map(r => [r.day, r.n]));
  const mapA = toMap(seriesA), mapB = toMap(seriesB);
  const valsA = days.map(d => mapA[d] ?? 0);
  const valsB = days.map(d => mapB[d] ?? 0);
  const max = Math.max(1, ...valsA, ...valsB);

  const x = (i) => PAD + (i / Math.max(1, days.length - 1)) * (W - 2 * PAD);
  const y = (v) => H - PAD - (v / max) * (H - 2 * PAD);

  const pathFor = (vals) =>
    vals.map((v, i) => (i === 0 ? "M" : "L") + " " + x(i).toFixed(1) + " " + y(v).toFixed(1)).join(" ");

  svg.appendChild(svgEl("path", { d: pathFor(valsA), fill: "none", stroke: colorA, "stroke-width": "2" }));
  svg.appendChild(svgEl("path", { d: pathFor(valsB), fill: "none", stroke: colorB, "stroke-width": "2" }));

  const crosshair = svgEl("line", { x1: 0, y1: 0, x2: 0, y2: H, stroke: "#3a4049", "stroke-width": "1", visibility: "hidden" });
  svg.appendChild(crosshair);

  svg.addEventListener("mousemove", (ev) => {
    const rect = svg.getBoundingClientRect();
    const px = ((ev.clientX - rect.left) / rect.width) * W;
    const i = Math.round(((px - PAD) / (W - 2 * PAD)) * Math.max(1, days.length - 1));
    const idx = Math.min(days.length - 1, Math.max(0, i));
    crosshair.setAttribute("x1", x(idx)); crosshair.setAttribute("x2", x(idx));
    crosshair.setAttribute("visibility", "visible");
    tip.style.display = "block";
    tip.style.left = Math.min(rect.width - 140, Math.max(0, (x(idx) / W) * rect.width + 8)) + "px";
    tip.style.top = "8px";
    tip.textContent = days[idx] + "  installs " + valsA[idx] + "  active " + valsB[idx];
  });
  svg.addEventListener("mouseleave", () => {
    crosshair.setAttribute("visibility", "hidden");
    tip.style.display = "none";
  });
}

let chartRangeDays = 30;

async function loadTelemetry() {
  const [summary, daily] = await Promise.all([
    fetch("/api/telemetry/summary").then(r => r.json()),
    fetch("/api/telemetry/daily?days=" + chartRangeDays).then(r => r.json()),
  ]);
  const stats = document.getElementById("telemetry-stats");
  stats.innerHTML = "";
  const add = (label, n) => {
    const d = el("div", { class: "stat" });
    d.appendChild(el("span", { class: "n" }, String(n)));
    d.appendChild(el("span", { class: "label" }, label));
    stats.appendChild(d);
  };
  add("Installs (all time)", summary.installs);
  add("Active (7d)", summary.active7d);
  add("Active (30d)", summary.active30d);
  renderTable(document.getElementById("telemetry-versions"), ["version", "users"], summary.byVersion);
  renderTable(document.getElementById("telemetry-channels"), ["channel", "users"], summary.byChannel);
  renderDailyChart(
    document.getElementById("telemetry-chart"), document.getElementById("chart-tip"),
    daily.installs, daily.active, "#6cb6ff", "#6ad68a", chartRangeDays
  );
}

for (const btn of document.querySelectorAll("#chart-range button")) {
  btn.addEventListener("click", () => {
    chartRangeDays = Number(btn.dataset.days);
    for (const b of document.querySelectorAll("#chart-range button")) b.classList.remove("active");
    btn.classList.add("active");
    loadTelemetry();
  });
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
  await loadReports("/api/accounts", "accounts-table", ["email", "tier", "created_at"]);
}

for (const btn of document.querySelectorAll(".tabs button")) {
  btn.addEventListener("click", () => {
    for (const b of document.querySelectorAll(".tabs button")) b.classList.remove("active");
    for (const s of document.querySelectorAll("section")) s.classList.remove("active");
    btn.classList.add("active");
    document.getElementById(btn.dataset.tab).classList.add("active");
  });
}

const REFRESH_MS = 20000;
document.getElementById("refresh-secs").textContent = String(REFRESH_MS / 1000);

function refreshAll() {
  loadTelemetry();
  loadReports("/api/telemetry/startup-reports", "startup-table", ["ts", "version", "channel", "os", "stage", "country", "report"]);
  loadReports("/api/telemetry/crash-reports", "crash-table", ["ts", "version", "channel", "os", "reason", "country", "report"]);
  loadAccounts();
}

refreshAll();
setInterval(refreshAll, REFRESH_MS);
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

    if (url.pathname === "/api/telemetry/daily") {
      return json(await telemetryDaily(env, url));
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
