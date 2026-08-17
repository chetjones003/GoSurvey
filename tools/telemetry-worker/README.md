# GoSurvey telemetry worker

The REQ-080 / ADR-032 telemetry receiver: a Cloudflare Worker that validates an anonymous ping
and appends one row to a D1 (SQLite) database.

**Setup and operation: [`docs/cloudflare-telemetry-setup.md`](../../docs/cloudflare-telemetry-setup.md).**
This file is orientation only.

## The numbers

```bash
npm run stats
```

```
GoSurvey telemetry — 2026-08-17

  Installs (all time)          1
  Active users (7 days)        0
  Active users (30 days)       0
```

Reads through your own `wrangler login`, so there is no public dashboard route to secure. For
anything else, use `queries.sql` or the D1 console in the Cloudflare dashboard.

| File | What it is |
|---|---|
| `src/index.js` | The Worker. Plain JS — no build step, no `node_modules`. |
| `schema.sql` | The `pings` table and its indexes. Idempotent; safe to re-run. |
| `stats.mjs` | `npm run stats` — installs and active users. |
| `queries.sql` | The analytics queries worth having. Run individually. |
| `wrangler.toml` | Deploy config. `database_id` must be filled in before first deploy. |

Not part of the CMake build and not compiled by anything. It is deployed with `wrangler`, on its
own cadence, by whoever operates the endpoint.

## The one coupling to know about

`src/telemetry/TelemetryService.cpp` does not trust a 2xx on its own — it requires the literal
substring `"ok":true` in the response body before recording a ping as delivered. So:

- keep `{ ok: true }` as the success response, with `ok` first;
- do not pretty-print or reformat the success JSON.

That check exists because the previous backend (Google Apps Script) could not set status codes
and answered `200` to every failure, including the one that meant no data was being recorded at
all. See BUG-020 in `TRACKER.md`.

## Deploy

```bash
npx wrangler deploy
```

Changing the deployed URL means changing a **compile-time constant** in the C++ client and
shipping a new build — the endpoint is deliberately not configurable at runtime (ADR-032 (f)).
Settle the final hostname before the first release.
