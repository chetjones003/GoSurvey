# TASK-735 — Owner-only admin dashboard (REQ-402, ADR-068)

## Requirement authority
- REQ-402 (spec/requirements.md)
- ADR-068 (spec/architecture.md)
- Depends on REQ-080 (telemetry), REQ-091/092 (accounts)

## Files / subsystems affected
- New: `tools/admin-worker/` — Worker source, `wrangler.toml`, `schema` (none — read-only, no new tables), dashboard HTML/JS served by the Worker itself (no separate Pages project needed for a single small page).
- `docs/` — new `docs/cloudflare-admin-dashboard-setup.md` covering the Cloudflare-dashboard-side steps this repo cannot do for the user: Cloudflare Access application + policy, `admin.gosurveycad.com` DNS/route.
- `tools/telemetry-worker/`, `worker/` (accounts) — read-only D1 bindings added to the new Worker's `wrangler.toml`; no changes to either existing Worker's own code.
- `gosurveycad-site` (separate private repo) — confirm `admin.gosurveycad.com` never appears in its `sitemap.xml`/`robots.txt`/nav (already true today; this is a check, not a change).

## Implementation approach
1. `tools/admin-worker/wrangler.toml`: new Worker `admin-api`, two `[[d1_databases]]` bindings (`TELEMETRY_DB` → `gosurvey-telemetry`, `ACCOUNTS_DB` → `gosurveycad-db`), route `admin.gosurveycad.com/*`.
2. `src/index.js`: serves
   - `GET /` — the dashboard page (inline HTML, no build step, matching the style of the other two Workers in this repo).
   - `GET /api/telemetry/summary` — installs, 7/30-day actives, version/channel breakdown (same queries as `tools/telemetry-worker/stats.mjs` and `queries.sql`, reused not reinvented).
   - `GET /api/telemetry/startup-reports`, `GET /api/telemetry/crash-reports` — paginated, filterable by version/stage/reason.
   - `GET /api/accounts` — paginated list from `gosurveycad-db.users`.
   - A link out to the Cloudflare dashboard's own Workers Logs for live request/error logs (REQ-402 clause 2d) — not proxied, just linked.
   - Every route is read-only: no `INSERT`/`UPDATE`/`DELETE` anywhere in this Worker.
3. No authentication code in the Worker — Cloudflare Access is configured at the Cloudflare account level (dashboard or `cloudflare` MCP tool), per ADR-068(a). The setup doc records the exact steps so they're reproducible if the account is ever rebuilt.
4. Update `docs/cloudflare-pages-setup.md` / add new doc cross-linking the admin dashboard setup.

## Test approach
- `tools/admin-worker/test.mjs` (mirrors `tools/telemetry-worker/test.mjs`'s style): each summary/list endpoint against a seeded local D1 (`wrangler d1 execute --local`), asserting shape and that no write-capable binding exists.
- Manual acceptance per REQ-402's four `[req402]` criteria — these require a live Cloudflare Access policy and so can't be exercised by `ctest`/unit tests; recorded as manual verification in the completion report.

## Architectural-boundary check
- No changes to the existing accounts (`worker/`) or telemetry (`tools/telemetry-worker/`) Worker code or schemas — the new Worker only reads their D1s via its own bindings. Satisfies ADR-068(b)'s separation.
- No new PII collection — satisfies REQ-402 clause 5.

## Status
Plan drafted 2026-10-11. Awaiting user confirmation before implementation (Cloudflare Access/DNS steps require the user's own Cloudflare account access either via dashboard or the `cloudflare` MCP tool's `authenticate` call — not something to do silently).
