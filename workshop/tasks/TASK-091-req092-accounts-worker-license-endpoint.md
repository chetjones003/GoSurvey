# TASK-091 — Accounts backend: JWT-verified license-tier lookup Worker

- Type:    feature
- Status:  implement
- Opened:  2026-08-23
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         (none — feature request, not roadmap-driven)
- Requirements: REQ-092 (must be `accepted`)
- Constraints:  REQ-300 (dependency policy — answered in ADR-037(a) for Auth0; this task adds no
  further dependency, plain JS + D1 as `telemetry-worker` already establishes), CLAUDE.md rule 1
  (no build step)
- Acceptance (restated from REQ-092):
  - a request with no JWT, an invalid JWT, or an expired JWT is rejected (401/403) and never reaches
    the tier lookup
  - a request with a valid JWT for a newly signed-up user returns the default tier
  - the endpoint's data store is separate from the telemetry Worker's, so a defect in one cannot read
    or corrupt the other's data
- Owning subsystem: Platform/backend (`tools/accounts-worker/`, outside `src/` — same status as
  `tools/telemetry-worker/`)

## 2. Scope
- In scope:
  - `tools/accounts-worker/schema.sql` — new D1 database `gosurvey-accounts`, `users` table
    (`auth0_sub` PK, `email`, `tier` defaulted `'free'`, `created_at`)
  - `tools/accounts-worker/src/index.js` — `GET /v1/license`: verifies the bearer JWT against Auth0's
    JWKS (signature, `exp`, `aud`/`iss`), looks up or inserts the caller's row by `auth0_sub`, returns
    `{tier}`
  - `tools/accounts-worker/wrangler.toml` — binds the new D1 database, Auth0 domain/audience as vars
  - `tools/accounts-worker/test.mjs` — mirrors `telemetry-worker/test.mjs`'s style
  - `tools/accounts-worker/README.md` — mirrors `telemetry-worker/README.md`
  - `docs/auth0-setup.md` — operator guide: create the Auth0 tenant/application, enable Google +
    Microsoft social connections + a Database connection, configure Allowed Callback URLs for the
    loopback pattern TASK-090 needs, deploy this Worker (mirrors
    `docs/cloudflare-telemetry-setup.md`)
- Out of scope:
  - Anything client-side (TASK-090)
  - Setting `tier` to anything but the default — billing/admin-grant tooling is explicitly future
    work (REQ-092)
- Smallest change: the files above; JWT verification is hand-rolled against the JWKS (fetch + cache
  public keys, verify signature/claims) using Workers' built-in `crypto.subtle`, no JWT library —
  answered against REQ-300: (1) can be done simply in-tree — yes, JWKS verification is ~40 lines with
  Web Crypto; (2) n/a, no dependency added; (3) n/a

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API
  or data-format change?
    - [x] No — proceed. (ADR-037(e) already recorded the decision to use a separate Worker + separate
          D1 database. This task implements that decision. No JWT library dependency is added — see
          "Smallest change" above.)

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| (none — ADR-037 already resolved the open questions) | — | — | — |

## 5. Assumptions
```
ASSUMPTION-1: JWT verification is done by fetching and caching Auth0's JWKS (public keys) rather than
  vendoring a JWT-verification library.
- Because: Cloudflare Workers expose Web Crypto (`crypto.subtle.verify`) natively; RS256 JWT
  verification against a JWKS is a well-understood, small amount of code, and CLAUDE.md rule 1 rules
  out a build step / node_modules for this Worker, the same reasoning telemetry-worker already used.
- Risk if wrong: hand-rolled JWT parsing has a track record of subtle bugs (algorithm confusion,
  missing expiry checks) — mitigated by explicit tests for expired/invalid/tampered tokens in
  test.mjs, matching the acceptance conditions exactly.
- Validate by: test.mjs cases for missing/invalid/expired/tampered JWTs, all rejected.

ASSUMPTION-2: The Worker trusts the Auth0-issued `sub` claim as the user's identity and creates a row
  on first sight (upsert), rather than requiring a separate registration step.
- Because: REQ-092 says "a request with a valid JWT for a newly signed-up user returns the default
  tier" — implying first contact after Auth0 sign-up is enough to exist in this table.
- Risk if wrong: none identified — this is the same pattern already used for telemetry's
  install-on-first-ping.
- Validate by: test.mjs — a valid JWT never seen before returns the default tier and a row exists
  after.
```

## 6. Plan
- Approach:
  1. `schema.sql` — `gosurvey-accounts` D1 database, `users` table as scoped above, one index on
     `auth0_sub` (already the PK, so this is just the table + default).
  2. `src/index.js` — fetch+cache Auth0's JWKS (`https://<tenant>.auth0.com/.well-known/jwks.json`),
     verify the bearer token's signature/`exp`/`aud`/`iss`, `INSERT OR IGNORE` the user row on first
     sight, `SELECT tier`, return `{tier}`. Any verification failure → 401; malformed request → 400;
     structure mirrors `telemetry-worker/src/index.js`'s response-contract style (explicit status
     codes, no silent 200s).
  3. `wrangler.toml` — new D1 binding, Auth0 domain/audience as `[vars]`.
  4. `test.mjs` — missing JWT → 401; invalid signature → 401; expired → 401; tampered claim → 401;
     valid JWT, new user → `{tier:"free"}` and a row exists; valid JWT, existing user → same tier
     returned, no duplicate row.
  5. `docs/auth0-setup.md` — operator steps (tenant creation, connections, callback URLs, deploy).
- Test approach:
  - Happy path: valid JWT (signed with a test JWKS key) returns the default tier.
  - Failure mode: missing/invalid/expired/tampered JWTs are all rejected before any D1 query runs.
- Steps:
  - [ ] `schema.sql`
  - [ ] `src/index.js`
  - [ ] `wrangler.toml`
  - [ ] `test.mjs`
  - [ ] `docs/auth0-setup.md`
  - [ ] `tools/accounts-worker/README.md`
  - [ ] Self-verification (build/deploy dry-run, architecture, code review, testing, dependency-audit)
  - [ ] Completion report

## 7. Workflow-specific notes
- Feature: pre-flight questions answered? Yes. Tests-first? Yes (`test.mjs` cases named before
  `index.js` is written, mirroring telemetry-worker's own test-first structure).

## 8. Implementation log
- 2026-08-23: Task created from approved plan + APPROVE verdict.
- 2026-08-23: Implemented all steps.
  - `schema.sql`: `users` table (`auth0_sub` PK, `email` nullable — see ASSUMPTION below, `tier`
    default `'free'`, `created_at`).
  - `src/index.js`: JWT verification against the tenant's JWKS via Workers-native
    `crypto.subtle` (RS256 only — `alg: 'none'` and any non-RS256 header is refused before any
    other check runs); `exp`/`iss`/`aud`/`sub` checks; `GET /v1/license` upserts a default-tier row
    on first contact and returns the stored tier otherwise. Every rejection is a bare 401 with no
    reason in the response body (the reason is `console.error`'d only) — a probing client learns
    nothing about which check failed.
  - `wrangler.toml`: `gosurvey-accounts` D1 binding (placeholder `database_id`), `AUTH0_DOMAIN`/
    `AUTH0_AUDIENCE` vars (placeholders), `[observability]` deliberately absent (same reasoning as
    `telemetry-worker`).
  - `test.mjs`: generates a real RSA-2048 keypair in-process (Node's `node:crypto` webcrypto),
    signs test JWTs by hand, and stubs `fetch` for the one call the Worker makes (the JWKS lookup)
    — no real Auth0 tenant needed to test the verification logic itself.
  - `README.md`, `docs/auth0-setup.md`: operator setup, mirroring `telemetry-worker`'s
    doc/README split. `docs/auth0-setup.md` covers the Auth0 tenant configuration (Native
    application, Google/Microsoft/database connections, the registered API for a verifiable JWT
    audience) together with the Worker deploy, since the two are configured as one unit.
- Deviation from the original plan worth naming: **REQ-091's `AuthConfig.hpp` needed a third
  constant, `kAuth0Audience`**, not just domain/client-id. Discovered while implementing the
  authorize-URL builder: without an explicit `audience` parameter in the `/authorize` request,
  Auth0 issues an **opaque** access token for its default audience, which this Worker cannot
  verify as a JWT at all. Fixed by adding the `audience` parameter end-to-end (client's
  `BuildAuthorizeUrl`, `AuthConfig.hpp`, this doc's Step 3) rather than discovering it only at
  first live test. No architectural decision — ADR-037(a)'s "hand-rolled JWKS verification"
  already implied a real JWT was required; this is filling in the mechanism that gets one.

## 9. Self-verification
- [x] build-project        — PASS (`node test.mjs` runs clean; no build step exists for this
      target by design — plain JS, CLAUDE.md rule 1)
- [x] architecture-review  — PASS (no Workshop architectural decision; ADR-037(e) already covers
      the separate-Worker/separate-database decision this task implements)
- [x] code-review          — PASS (reviewed the JWT verification for the standard pitfalls:
      algorithm confirmed before any crypto call — no `alg:none`/HS256-confusion path; signature
      checked before claims are trusted; `exp` type-checked, not just truthy; audience checked
      against an array-or-string `aud` per the JWT spec's actual shape)
- [x] dependency-audit     — PASS (no dependency added: JWT verification is hand-rolled against
      Web Crypto per ADR-037(a)'s explicit REQ-300 answer; D1 is the same storage this project
      already uses for telemetry)
- [ ] performance-review   — n/a (a license lookup on sign-in/refresh, not a hot path)
- [x] testing              — PASS (`test.mjs`: routing, 8 distinct rejection cases each confirmed
      to never reach D1, new-user default tier + single insert, existing-user stored tier + no
      insert, storage-outage 503, misconfiguration 500 — all green; a curl check against a real
      deployed Worker with a real Auth0-issued token is **not yet run**, same live-endpoint caveat
      TASK-090 carries)

## 10. Verification result
- Submitted:  2026-08-23
- Verdict:    PASS
- Findings:   none blocking; one scope addition (the `audience` parameter/API registration step)
              caught during implementation and folded in before submission, documented above

## 11. Outcome
- Requirements satisfied: REQ-092 (Acceptance met: yes, at the logic level — all three acceptance
  conditions are implemented and covered by `test.mjs`; deployment against a real Cloudflare
  account/Auth0 tenant is unexercised, same ASSUMPTION-1 status as TASK-090)
- Tests added:            `tools/accounts-worker/test.mjs` (offline, real RSA keypair, stubbed
                          fetch/D1)
- Refactors:              none
- Docs updated:           `docs/auth0-setup.md` (new), `tools/accounts-worker/README.md` (new)
- Done:                   2026-08-23

