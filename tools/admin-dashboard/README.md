# GoSurvey Admin Dashboard

HTMX + Go admin for the two Cloudflare D1 databases that `database.bat` queries manually.

- **gosurvey-telemetry** — `pings` (REQ-080, ADR-032) · anonymous install/active pings, validated at `tools/telemetry-worker/src/index.js`, stored via `INSERT OR IGNORE`
- **gosurvey-accounts** — `users` (REQ-092, ADR-037) · `auth0_sub` PK + email + tier + created_at, JWT-verified at `tools/accounts-worker/src/index.js`

The two DBs are separate by design (ADR-037e) — a bug in the public telemetry writer cannot reach account data.

## Run

```bash
cd tools/admin-dashboard
go run .                 # http://localhost:8080
PORT=8081 go run .       # custom port
go build -o admin . && ./admin
```

With `MOCK=1`, data is seeded deterministically (240 installs, ~1800 active pings, 68 users) so the dashboard works with no credentials.

**Tables** (all pages): click a header to sort (asc → desc → off), drag a column edge to resize, double-click the edge to fit to content; widths are remembered per table. Click an ID to copy it. **Charts**: hover for exact values, 7d/14d/30d range buttons.

**Live D1 is the default:** just `go run .` (needs `wrangler login` once). The server starts instantly, shows "Connecting…", fetches pings + users from D1 in parallel in the background, reloads itself when data lands, then re-fetches every 30s. Page loads never wait on wrangler. A globally installed `wrangler` is used if found (much faster to start than `npx`). Set `MOCK=1` (or `LIVE_D1=0`) to run offline on seeded demo data.

## Pages

| Route | What it shows |
|---|---|
| `/` | KPIs (installs total, active 7d/30d DISTINCT, users), DAU + installs charts (canvas), version/channel/country splits, retention, recent pings |
| `/telemetry` | Filterable pings table — HTMX `hx-get` to `/partials/pings` with `event`/`channel`/`version`/`q` + pagination |
| `/accounts` | Users table — HTMX to `/partials/users` with `tier`/`q` |
| `/analytics` | Shipped `queries.sql` blocks rendered live (totals, retention LEFT JOIN, version/channel GROUP BYs, DAU time series) |

## API (JSON)

```
GET /api/health          # mode + database schemas
GET /api/stats           # same aggregation as overview
GET /api/pings?event=&channel=&version=&q=&page=&per_page=
GET /api/users?q=&tier=&page=&per_page=
GET /partials/pings      # HTML rows for HTMX
GET /partials/users
```

## Stack

- Go `net/http` + `html/template` (embedded via `embed.FS`) — no external router
- HTMX 1.9 vendored in `static/` (no CDN round trip), CSS without framework (so no purple-gradient AI tell)
- Templates are parsed once at startup; stats, sort order and search index are computed once per data refresh, not per request
- `internal/store` is the only place to swap mock → live D1

## Wrangler parity

The cards show the exact queries from `tools/telemetry-worker/queries.sql` and the `wrangler d1 execute` commands from `database.bat` / README, so numbers match what you'd see in the D1 console.
