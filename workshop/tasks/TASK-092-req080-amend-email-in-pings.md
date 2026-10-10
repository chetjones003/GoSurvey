# TASK-092 — Amend REQ-080: attach signed-in email to telemetry pings

- Type:    feature
- Status:  done
- Opened:  2026-08-23
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         (none — user-directed spec amendment, not roadmap-driven)
- Requirements: REQ-080 (amended, D-2026-08-23-e), REQ-091 (source of the email — read only)
- Constraints:  REQ-201 (no silent failures — extended here to "no silently false disclosure
  text"), REQ-300/301 n/a (no dependency or abstraction added)
- Acceptance (restated from REQ-080's amendment):
  - the payload JSON contains exactly six fields (installId, event, version, channel, os, email);
  - `email` equals the signed-in email when REQ-091's sign-in state is true at the moment the
    ping fires, and is empty otherwise;
  - a ping's firing/throttling is never gated on or delayed by sign-in state;
  - no username, hostname, file path, or hardware fingerprint is ever included;
  - network failure behavior (silent, non-blocking) is unchanged;
  - the settings-panel disclosure accurately describes current behavior, not the pre-amendment
    promise.
- Owning subsystem: Telemetry (payload), Auth (email source, read-only), UI (disclosure text),
  Platform/backend (`tools/telemetry-worker/`, outside `src/`)

## 2. Scope
- In scope:
  - `src/telemetry/TelemetryPing.hpp/.cpp` — `email` field on `TelemetryPayload`, included in
    `BuildTelemetryJson`
  - `src/telemetry/TelemetryService.hpp/.cpp` — `BeginTelemetryPing` gains a `signedInEmail`
    parameter (default empty, so the change is additive)
  - `src/app/main.cpp` — telemetry firing point moved from raw process start to the point
    `cmd.authGateResolved` first becomes true, so sign-in state is actually known before the
    payload is built; fires exactly once per launch via a new `telemetryFired` flag
  - `src/ui/CadUiSettings.cpp` — `DrawSystemUsageData` rewritten to describe current behavior
    (conditionally shows the signed-in-vs-signed-out text); box renamed "Anonymous Usage Data" →
    "Usage Data"
  - `tools/telemetry-worker/schema.sql` — `email TEXT` column added
  - `tools/telemetry-worker/src/index.js` — optional `email` accepted, validated loosely
    (format + length cap), dropped to `NULL` rather than rejecting the ping if malformed
  - `tools/telemetry-worker/test.mjs` — email stored/validated/dropped cases; column-count
    assertions updated (8 → 9)
  - `docs/cloudflare-telemetry-setup.md` — "What is stored" section rewritten; one-time
    `ALTER TABLE` migration instructions for already-deployed tables
  - Live migration: `ALTER TABLE pings ADD COLUMN email TEXT` run against the deployed
    `gosurvey-telemetry` database; updated Worker deployed
  - `tests/TelemetryPingTests.cpp` — email-present and email-empty JSON cases
- Out of scope:
  - Any change to `gosurvey-accounts`'s `users` table or the license-lookup wiring (TASK-090
    addendum 8c, already done)
  - An opt-out toggle (still none, by the original D3 decision — unaffected by this amendment)
  - Retroactively backfilling email onto historical rows (not requested, not attempted)

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API
  or data-format change?
    - [x] No — proceed. This changes a data FORMAT (the payload gains a field) but not in the
          sense §3 of the boundary check is guarding against: it's an additive field with a
          default, both client structs and the wire format degrade gracefully to the pre-amendment
          shape when `email` is empty, and no new subsystem, dependency, or global state is
          introduced. The one genuine design decision — moving telemetry's firing point so
          sign-in state is known — was made explicitly with the user (see decision log), not
          decided unilaterally.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Does a ping now require sign-in to send at all, or is email only attached when the user happens to already be signed in? | 2026-08-23 | Email only when signed in; ping fires/throttles exactly as before regardless of sign-in state |

## 5. Assumptions
```
ASSUMPTION-1: Malformed/absent email on the wire is stored as NULL rather than rejecting the ping.
- Because: telemetry's core guarantee (REQ-080) is that a background reporting call never fails
  the application or gets blocked over a non-essential field; email is additive enrichment, not a
  required field, so validation failure on it should degrade to "no email" not "no ping."
- Risk if wrong: a client bug that always sends a malformed email would silently never populate the
  column — acceptable, since the alternative (rejecting the ping) is worse for REQ-080's core
  promise.
- Validate by: test.mjs's malformed-email case, confirmed status 200 + email stored as null.
```

## 6. Plan
- Approach: implement client (payload + firing-point move), backend (schema + validation),
  disclosure text, then deploy + migrate the live database, in that order — tests before deploy.
- Files/functions to touch: listed in Scope above.
- Test approach: happy path = a signed-in user's ping carries their email end-to-end (unit-tested
  client-side via `TelemetryPingTests`, server-side via `test.mjs`); failure mode = malformed/empty
  email is dropped to null without failing the ping, on both sides.
- Steps:
  - [x] `TelemetryPayload`/`BuildTelemetryJson` gain `email`
  - [x] `BeginTelemetryPing`/`TelemetryWorker` thread `signedInEmail` through
  - [x] `main.cpp`: defer telemetry firing to the auth-gate-resolved point, fire-once flag
  - [x] `TelemetryPingTests.cpp`: email-empty and email-present cases
  - [x] `schema.sql`: add `email TEXT` column (for fresh deploys)
  - [x] `src/index.js`: accept + validate optional `email`, upsert into the 9-column INSERT
  - [x] `test.mjs`: email test cases, column-count assertions updated
  - [x] Live migration: `ALTER TABLE pings ADD COLUMN email TEXT` against the deployed DB
  - [x] Deploy updated Worker; smoke-tested with a real curl POST + direct D1 read-back
  - [x] `CadUiSettings.cpp`: disclosure text rewritten, box renamed
  - [x] Self-verification (build, testing)
  - [x] Completion report

## 7. Workflow-specific notes
- Feature: pre-flight question answered (Q1 above) before implementing. Tests-first for the
  worker side (test.mjs cases written and run before/alongside the index.js change); client-side
  tests updated in the same commit as the payload change, not after.

## 8. Implementation log
- 2026-08-23: User asked directly to add email to `pings`, explicitly naming that it overwrites
  REQ-080's no-PII requirement. Asked Q1 (ping-scope) before writing any code; recorded the
  decision (D-2026-08-23-e) and amended REQ-080's text in `spec/requirements.md` before touching
  code, per CLAUDE.md's spec-changes-are-recorded-decisions rule.
- 2026-08-23: Implemented client + backend + disclosure text; full C++ suite green (214579
  assertions / 514 cases, one new test case); `telemetry-worker/test.mjs` green (all cases,
  including 4 new email-specific ones).
- 2026-08-23: Ran the one-time `ALTER TABLE pings ADD COLUMN email TEXT` against the live
  `gosurvey-telemetry` D1 database (pre-existing table, `CREATE TABLE IF NOT EXISTS` would not
  have added it), then deployed the updated Worker. Smoke-tested with a real POST including
  `email`, confirmed via direct D1 read-back that the row stored it correctly, then deleted the
  test row.

## 8a. Addendum — remove the 24h throttle entirely (2026-08-23, D-2026-08-23-f)
- User noticed the most recent `pings` row still showed no email after the fix above and asked
  why. Root cause: not a bug — the 24h throttle meant no new ping had fired since an earlier
  launch that same day, so there was no new row for the email to appear on at all. Rather than
  just wait it out, the user asked to remove the throttle outright: "we need the telemetry to
  send the ping every time the application opens."
- Asked one scope question before implementing: should the server also drop its per-day dedup
  index, or would the client alone sending more requests be pointless (server already collapses
  same-day rows)? User chose to drop the server-side dedup too.
- Recorded as D-2026-08-23-f; REQ-080's "at most once per rolling 24-hour period" acceptance
  condition replaced with "every launch, no throttle."
- Implemented, and removed the now-dead throttle machinery entirely rather than leaving it
  unused (REQ-201's "no half-finished implementations" read as "no dead code left behind by a
  reversal"):
  - `src/telemetry/TelemetryPing.hpp/.cpp`: `DecideEventToSend` drops its `lastActivePingDate`
    parameter and the "send nothing" case (always returns "install" or "active" now);
    `GetTodayDateString` and `IsAfter24Hours` deleted outright — no remaining caller.
  - `src/io/UserPrefs.hpp/.cpp`: `TelemetryIds` drops `lastActivePingDate`; `GetTelemetryIds`/
    `UpdateTelemetryIds` simplified to installId-only. A pre-existing `gosurvey-user.json` with a
    stale `lastActivePingDate` key is left untouched on disk (harmless, unread) rather than
    migrated — not worth a formal migration for a two-key JSON file with no downstream reader.
  - `src/telemetry/TelemetryService.cpp`: updated to the simplified signatures.
  - `tests/TelemetryPingTests.cpp`: throttle-specific tests removed (5 cases); one consolidated
    case added ("always returns active once an install id exists").
  - `tools/telemetry-worker/schema.sql`: `ux_pings_active_daily` removed from the CREATE-time
    definition (documented, with a `DROP INDEX IF EXISTS` note for already-deployed tables).
  - `tools/telemetry-worker/queries.sql`: stale comment fixed (DISTINCT was described as
    "belt-and-braces" against the now-gone index; now correctly load-bearing).
  - `tools/telemetry-worker/stats.mjs`: also added the `email` column to its display query/table,
    which had been missed when `email` was first wired up earlier this session.
  - Live: `DROP INDEX IF EXISTS ux_pings_active_daily` run against the deployed
    `gosurvey-telemetry` database. `index.js` needed no code change — `INSERT OR IGNORE` simply
    stops having anything to ignore for `active` events once the unique index is gone.
- Verified: full C++ suite green after removal (214558 assertions / 510 cases — 4 fewer than
  before, matching the 5 removed / 1 added test-case net). Checked `queries.sql`/`stats.mjs` for
  any `COUNT(*)`-based "active users" query that the dedup removal would silently break — none
  exist; every such query already used `COUNT(DISTINCT install_id)`.
- Also cleared `lastActivePingDate` in the user's own local `gosurvey-user.json` (as a one-time
  manual step, not a code change) so they could verify a fresh "active" ping immediately rather
  than waiting out the old throttle window from before this change shipped.

## 9. Self-verification
- [x] build-project        — PASS (GoSurvey + GoSurveyTests, clean)
- [x] architecture-review  — PASS (no Workshop architectural decision — see §3)
- [x] code-review          — PASS (reviewed the firing-point restructuring for correctness: fires
      exactly once via `telemetryFired`, uses the resolved `cmd.authEmail` only when
      `cmd.authSignedIn`, degrades to the pre-amendment empty-email behavior for a signed-out or
      offline-skipped user)
- [x] dependency-audit     — PASS / n-a (no dependency added)
- [ ] performance-review   — n/a (telemetry is not a per-frame path; REQ-100 unaffected)
- [x] testing              — PASS (`TelemetryPingTests` 2 new/updated cases; `telemetry-worker`
      `test.mjs` 4 new email cases + updated column-count assertions; full suites green; live
      smoke test against the deployed Worker)

## 10. Verification result
- Submitted:  2026-08-23
- Verdict:    PASS
- Findings:   none blocking

## 11. Outcome
- Requirements satisfied: REQ-080 (amended acceptance conditions met: yes)
- Tests added:            `TelemetryPingTests.cpp` (+2 cases), `telemetry-worker/test.mjs` (+4
                          cases, updated column-count assertions)
- Refactors:              none
- Docs updated:           `docs/cloudflare-telemetry-setup.md`, `tools/telemetry-worker/src/index.js`
                          header comment, `spec/requirements.md` (REQ-080), `spec/project.md`
                          (D-2026-08-23-e)
- Done:                   2026-08-23
