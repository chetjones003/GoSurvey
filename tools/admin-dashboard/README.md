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

Mock data is seeded deterministically (240 installs, ~1800 active pings, 68 users) so the dashboard works with no credentials. Replace `internal/store` with live D1 calls via `wrangler` API when you wire real credentials.

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
- HTMX 1.9 via CDN, CSS without framework (so no purple-gradient AI tell)
- `internal/store` is the only place to swap mock → live D1

## Wrangler parity

The cards show the exact queries from `tools/telemetry-worker/queries.sql` and the `wrangler d1 execute` commands from `database.bat` / README, so numbers match what you'd see in the D1 console.
