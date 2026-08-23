# GoSurvey accounts worker

The REQ-092 / ADR-037 license-tier lookup: a Cloudflare Worker that verifies an Auth0-issued JWT
and appends/reads one row in a D1 (SQLite) database, separate from `telemetry-worker`'s.

**Setup and operation: [`docs/auth0-setup.md`](../../docs/auth0-setup.md).** This file is
orientation only.

| File | What it is |
|---|---|
| `src/index.js` | The Worker. Plain JS — no build step, no `node_modules`. |
| `schema.sql` | The `users` table. Idempotent; safe to re-run. |
| `wrangler.toml` | Deploy config. `database_id` and the Auth0 domain/audience vars must be filled in before first deploy. |
| `test.mjs` | `npm test` — offline checks; generates its own RSA keypair, no real Auth0 tenant needed. |

Not part of the CMake build and not compiled by anything. Deployed with `wrangler`, on its own
cadence, by whoever operates the endpoint — same as `telemetry-worker`.

## Why this is a separate Worker and database from telemetry

`telemetry-worker` is public and unauthenticated by design (REQ-080: anonymous, no identity to
check). This one requires a verified JWT on every request. Sharing infrastructure between an
anonymous public endpoint and an authenticated one means a bug in the anonymous side's validation
could, in principle, reach account data. Keeping them as separate Workers with separate D1
databases (`gosurvey-telemetry` vs `gosurvey-accounts`) means that can't happen structurally,
not just by convention (ADR-037 (e)).

## JWT verification, in one paragraph

The Worker fetches the Auth0 tenant's JWKS (`https://<domain>/.well-known/jwks.json`), matches
the token header's `kid` to a key, and verifies the RS256 signature with the Workers-native Web
Crypto API (`crypto.subtle`) — no JWT library. It then checks `exp` (not expired), `iss` (matches
the configured tenant), and `aud` (matches `AUTH0_AUDIENCE`) before trusting `sub` as the user's
identity. Any failure is a bare 401 — the specific reason is logged, never returned, so a probing
client learns nothing about which check it failed.

## What this does NOT do yet

Every new `sub` gets the default tier (`'free'`) on first contact. Nothing sets a tier to
anything else — billing, an admin tool, or a manual grant are explicitly future work (REQ-092),
and no application feature currently checks the tier for anything (REQ-091 is identity/mechanism
only).

## Deploy

```bash
wrangler d1 create gosurvey-accounts     # paste the printed database_id into wrangler.toml
npm run schema
npm run deploy
```

The client calls this endpoint with the Auth0 access token it already holds after sign-in — no
separate credential to configure on the client side.
