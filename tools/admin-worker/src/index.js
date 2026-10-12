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
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans+Condensed:wght@500;600;700&family=IBM+Plex+Sans:wght@400;500;600&display=swap" rel="stylesheet">
<style>
  /* Palette lifted straight from gosurveycad.com's styles.css -- same tokens, same dark-mode
     rule, so this page looks like a room in the same house as the marketing site. */
  :root{
    --paper:#f2f4f6; --surface:#ffffff; --surface-2:#e8ecf0;
    --ink:#12161a; --ink-2:#3d4753; --ink-3:#69747f;
    --rule:#c9d2da; --rule-strong:#9aa7b3;
    --accent:#c2551b; --accent-ink:#ffffff; --steel:#2f4f6b; --ok:#1f7a4d;
  }
  @media (prefers-color-scheme:dark){
    :root{
      --paper:#0f1317; --surface:#161b21; --surface-2:#1e252c;
      --ink:#e8edf2; --ink-2:#b3bfca; --ink-3:#8492a0;
      --rule:#2b343d; --rule-strong:#41505d;
      --accent:#f0803c; --accent-ink:#12161a; --steel:#7fa8cc; --ok:#4cc38a;
    }
  }
  *{box-sizing:border-box}
  html,body{margin:0;padding:0;background:var(--paper);color:var(--ink)}
  body{font-family:"IBM Plex Sans",ui-sans-serif,system-ui,sans-serif;font-size:14px;line-height:1.5}
  a{color:var(--accent);text-decoration:none}
  .mono{font-family:"IBM Plex Mono",ui-monospace,monospace}
  .app{display:flex;min-height:100vh}

  .sidebar{width:220px;flex-shrink:0;background:var(--surface);border-right:1px solid var(--rule);
    display:flex;flex-direction:column;position:sticky;top:0;height:100vh}
  .brand{display:flex;align-items:baseline;gap:.5rem;padding:1.25rem 1.1rem;border-bottom:1px solid var(--rule);
    font-family:"IBM Plex Sans Condensed",sans-serif;font-weight:700;font-size:1.05rem}
  .brand .tick{color:var(--accent)}
  .nav{padding:.75rem;flex:1}
  .nav-item{display:block;padding:.5rem .65rem;border-radius:7px;color:var(--ink-2);font-size:.83rem;
    cursor:pointer;margin-bottom:2px}
  .nav-item:hover{background:var(--surface-2);color:var(--ink)}
  .nav-item.active{background:var(--surface-2);color:var(--ink);font-weight:600}
  .sidebar-foot{padding:.9rem;border-top:1px solid var(--rule);font-size:.7rem;color:var(--ink-3)}
  .live{display:flex;align-items:center;gap:.4rem;margin-bottom:.4rem}
  .dot{width:7px;height:7px;border-radius:50%;background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb, var(--ok) 20%, transparent);
    animation:pulse 2s infinite}
  @keyframes pulse{0%,100%{opacity:1}50%{opacity:.35}}

  .main{flex:1;min-width:0}
  .topbar{display:flex;align-items:center;justify-content:space-between;padding:1rem 1.5rem;
    background:color-mix(in srgb, var(--paper) 88%, transparent);backdrop-filter:blur(10px);
    border-bottom:1px solid var(--rule);position:sticky;top:0;z-index:5}
  .page-title{margin:0;font-family:"IBM Plex Sans Condensed",sans-serif;font-size:1.1rem;font-weight:700}
  .page-sub{margin:.15rem 0 0;color:var(--ink-3);font-size:.78rem}
  .content{padding:1.5rem;max-width:1200px}
  section{display:none}
  section.active{display:block}

  .kpi-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:.9rem;margin-bottom:1rem}
  .kpi{background:var(--surface);border:1px solid var(--rule);border-radius:10px;padding:.95rem 1.15rem}
  .kpi-label{font-size:.66rem;font-weight:600;letter-spacing:.06em;text-transform:uppercase;color:var(--ink-3)}
  .kpi-value{font-family:"IBM Plex Sans Condensed",sans-serif;font-size:1.75rem;font-weight:700;margin-top:.3rem}

  .card{background:var(--surface);border:1px solid var(--rule);border-radius:10px;padding:1rem;margin-bottom:1rem}
  .card-head{display:flex;align-items:center;gap:1rem;margin-bottom:.8rem}
  .card-head h3{margin:0;font-size:.78rem;font-weight:700;text-transform:uppercase;letter-spacing:.04em;color:var(--ink-2)}
  .card-head a{font-size:.75rem}
  .push{margin-left:auto}

  .chart{display:block;width:100%;height:170px;cursor:crosshair}
  .chart-wrap{position:relative}
  .chart-legend{display:flex;align-items:center;gap:1.1rem;font-size:.75rem;color:var(--ink-3);margin-top:.6rem}
  .chart-legend .swatch{display:inline-block;width:9px;height:9px;border-radius:2px;margin-right:.35rem}
  .chart-tip{display:none;position:absolute;pointer-events:none;background:var(--ink);color:var(--paper);
    font-size:.72rem;padding:.3rem .55rem;border-radius:6px;white-space:nowrap;z-index:5}
  .range-btns{display:flex;gap:.25rem}
  .range-btns button{background:var(--surface);color:var(--ink-3);border:1px solid var(--rule);
    padding:.15rem .55rem;font-size:.72rem;cursor:pointer;border-radius:999px}
  .range-btns button.active{background:var(--steel);color:#fff;border-color:var(--steel)}

  .bar-row{display:grid;grid-template-columns:110px 1fr 36px;align-items:center;gap:.6rem;
    font-size:.78rem;margin-bottom:.5rem}
  .bar-row:last-child{margin-bottom:0}
  .bar-label{font-family:"IBM Plex Mono",monospace;color:var(--ink-2);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
  .bar-track{height:7px;background:var(--surface-2);border-radius:999px;overflow:hidden}
  .bar-fill{height:100%;background:var(--accent)}
  .bar-fill.alt{background:var(--steel)}
  .bar-val{text-align:right;font-family:"IBM Plex Mono",monospace;font-weight:600}

  .table-wrap{overflow:auto;max-height:70vh}
  table{width:100%;border-collapse:collapse;font-size:.8rem}
  th{text-align:left;padding:.5rem .65rem;font-size:.66rem;font-weight:600;letter-spacing:.05em;
    text-transform:uppercase;color:var(--ink-3);border-bottom:1px solid var(--rule);
    background:var(--surface-2);white-space:nowrap;position:sticky;top:0;cursor:pointer;user-select:none}
  th:after{content:"";margin-left:.3rem;opacity:.4}
  th[data-sort="asc"]:after{content:"\\2191";opacity:1}
  th[data-sort="desc"]:after{content:"\\2193";opacity:1}
  td{padding:.45rem .65rem;border-bottom:1px solid var(--rule);color:var(--ink-2);white-space:nowrap;vertical-align:middle}
  tbody tr:hover{background:var(--surface-2)}
  .report{white-space:pre-wrap;font-family:"IBM Plex Mono",monospace;font-size:.72rem;max-width:60ch;
    overflow:hidden;text-overflow:ellipsis}
  .badge{display:inline-flex;align-items:center;padding:.1rem .5rem;border-radius:999px;font-size:.64rem;
    font-weight:700;letter-spacing:.02em;text-transform:uppercase;border:1px solid var(--rule)}
  .badge.install{color:var(--ok);border-color:var(--ok)}
  .badge.active{color:var(--steel);border-color:var(--steel)}
  .badge.tier-free{color:var(--ink-3)}
  .badge.tier-pro, .badge.tier-paid{color:var(--accent);border-color:var(--accent)}
</style>
</head>
<body>
<div class="app">
  <aside class="sidebar">
    <div class="brand"><span class="tick">&bull;</span>GoSurveyCAD admin</div>
    <nav class="nav">
      <div class="nav-item active" data-tab="telemetry">Telemetry</div>
      <div class="nav-item" data-tab="startup">Startup failures</div>
      <div class="nav-item" data-tab="crash">Crash reports</div>
      <div class="nav-item" data-tab="accounts">Accounts</div>
    </nav>
    <div class="sidebar-foot">
      <div class="live"><span class="dot"></span>Live &mdash; every <span id="refresh-secs"></span>s</div>
      <div class="mono">gosurvey-telemetry</div>
      <div class="mono">gosurvey-accounts</div>
    </div>
  </aside>

  <main class="main">
    <div class="topbar">
      <div>
        <h1 class="page-title" id="page-title">Telemetry</h1>
        <p class="page-sub" id="page-sub">Installs, active usage, versions and channels</p>
      </div>
    </div>

    <div class="content">
      <section id="telemetry" class="active">
        <div class="kpi-grid" id="telemetry-stats"></div>

        <div class="card">
          <div class="card-head">
            <h3>Installs &amp; active installs per day</h3>
            <div class="range-btns push" id="chart-range">
              <button data-days="7">7d</button>
              <button data-days="14">14d</button>
              <button data-days="30" class="active">30d</button>
            </div>
          </div>
          <div class="chart-wrap">
            <svg id="telemetry-chart" class="chart" viewBox="0 0 600 170" preserveAspectRatio="none"></svg>
            <div class="chart-tip" id="chart-tip"></div>
          </div>
          <div class="chart-legend">
            <span><span class="swatch" style="background:var(--accent)"></span>Installs</span>
            <span><span class="swatch" style="background:var(--steel)"></span>Active</span>
          </div>
        </div>

        <div class="card">
          <div class="card-head"><h3>Versions (30d)</h3></div>
          <div id="telemetry-versions"></div>
        </div>
        <div class="card">
          <div class="card-head"><h3>Channels (30d)</h3></div>
          <div id="telemetry-channels"></div>
        </div>
      </section>

      <section id="startup">
        <div class="card">
          <div class="card-head">
            <h3>Startup failure reports</h3>
            <a class="push" href="https://dash.cloudflare.com" target="_blank">Live Workers Logs &rarr;</a>
          </div>
          <div class="table-wrap"><table id="startup-table"></table></div>
        </div>
      </section>

      <section id="crash">
        <div class="card">
          <div class="card-head">
            <h3>Crash reports</h3>
            <a class="push" href="https://dash.cloudflare.com" target="_blank">Live Workers Logs &rarr;</a>
          </div>
          <div class="table-wrap"><table id="crash-table"></table></div>
        </div>
      </section>

      <section id="accounts">
        <div class="card">
          <div class="card-head"><h3>Accounts</h3></div>
          <div class="table-wrap"><table id="accounts-table"></table></div>
        </div>
      </section>
    </div>
  </main>
</div>

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

function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

// Horizontal bar-list breakdown (label / proportional bar / value), styled after the existing
// Go admin dashboard's .bar-row. Bars are relative to the largest value in the set.
function renderBarList(container, labelKey, valueKey, rows, alt) {
  container.innerHTML = "";
  const max = Math.max(1, ...rows.map(r => r[valueKey]));
  for (const row of rows) {
    const pct = Math.round((row[valueKey] / max) * 100);
    const line = el("div", { class: "bar-row" });
    line.appendChild(el("span", { class: "bar-label" }, String(row[labelKey])));
    const track = el("div", { class: "bar-track" });
    track.appendChild(el("div", { class: "bar-fill" + (alt ? " alt" : ""), style: "width:" + pct + "%" }));
    line.appendChild(track);
    line.appendChild(el("span", { class: "bar-val" }, String(row[valueKey])));
    container.appendChild(line);
  }
  if (!rows.length) container.appendChild(el("div", { class: "bar-row" }, "No data yet"));
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

  const crosshair = svgEl("line", { x1: 0, y1: 0, x2: 0, y2: H, stroke: cssVar("--rule-strong"), "stroke-width": "1", visibility: "hidden" });
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
    const d = el("div", { class: "kpi" });
    d.appendChild(el("div", { class: "kpi-label" }, label));
    d.appendChild(el("div", { class: "kpi-value" }, String(n)));
    stats.appendChild(d);
  };
  add("Installs (all time)", summary.installs);
  add("Active (7d)", summary.active7d);
  add("Active (30d)", summary.active30d);
  renderBarList(document.getElementById("telemetry-versions"), "version", "users", summary.byVersion, false);
  renderBarList(document.getElementById("telemetry-channels"), "channel", "users", summary.byChannel, true);
  renderDailyChart(
    document.getElementById("telemetry-chart"), document.getElementById("chart-tip"),
    daily.installs, daily.active, cssVar("--accent"), cssVar("--steel"), chartRangeDays
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

const PAGE_INFO = {
  telemetry: ["Telemetry", "Installs, active usage, versions and channels"],
  startup: ["Startup failures", "Silent diagnostics sent before the startup error dialog"],
  crash: ["Crash reports", "Runtime crash diagnostics from the desktop client"],
  accounts: ["Accounts", "Signed-in users, read from gosurvey-accounts"],
};

for (const item of document.querySelectorAll(".nav-item")) {
  item.addEventListener("click", () => {
    for (const b of document.querySelectorAll(".nav-item")) b.classList.remove("active");
    for (const s of document.querySelectorAll("section")) s.classList.remove("active");
    item.classList.add("active");
    document.getElementById(item.dataset.tab).classList.add("active");
    const [title, sub] = PAGE_INFO[item.dataset.tab];
    document.getElementById("page-title").textContent = title;
    document.getElementById("page-sub").textContent = sub;
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
