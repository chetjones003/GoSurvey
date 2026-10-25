# TASK-1043 — Retire sign-in and email telemetry

- Type: feature / specification amendment
- Status: complete
- Opened: 2026-10-24
- Owner: Codex

## 1. Authority
- Goal: none (user-directed)
- Requirements: REQ-080 (accepted, amended by D-2026-10-24-a); REQ-091 and REQ-092 (retired by D-2026-10-24-a); REQ-425 (accepted; diagnostics retained)
- Constraints: REQ-201 (reporting failure handling), REQ-300 (no new dependency), architecture §8 (one-shot workers)
- Acceptance:
  - anonymous install once and active on every later launch, exact five-field payload, no email/PII;
  - telemetry launches independently on its own worker and never waits on/gates startup or fails visibly;
  - offline and online app behavior is otherwise identical;
  - remove account/sign-in UI, launch gate, auth networking/credential storage, and license lookup client;
  - remove usage/privacy notice from Settings; retain separately governed startup/crash diagnostics;
  - update `PRIVACY.md` to match these data flows.
- Owning subsystem: Qt UI, application startup, Auth (retired), Telemetry, Platform HTTP, UserPrefs, tests and privacy documentation.

## 2. Scope
- In scope: remove desktop account/sign-in/launch-gate and email attachment; start anonymous install/open pings independently; remove their settings notice; retain diagnostics; document accurate behavior.
- Out of scope: changing the separate diagnostics service (REQ-425), changing the telemetry backend schema, or operating/removing deployed third-party services outside this repository.
- Smallest change: remove Auth runtime and UI paths from the client and build, remove obsolete auth preference state and credential/network helpers when no other callers remain, preserve telemetry's install-ID/event logic and diagnostics.

## 3. Architectural boundary check
- A retired Auth subsystem and startup-independent ping wiring are explicitly recorded in ADR-070/D-2026-10-24-a. No new dependency, abstraction, or data format is planned. Remove `auth/` ownership and its client integration; retain existing Telemetry and diagnostics boundaries.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| 1 | Should independent startup/crash diagnostic uploads also be removed? | 2026-10-24 | No; keep diagnostics uploads. |

## 5. Assumptions
```
ASSUMPTION-1: Existing backend event names `install` and `active` remain unchanged; `active` represents an app open.
- Because: The request changes what is collected and whether it gates, not the established backend event contract.
- Risk if wrong: A backend expecting the literal `open` event would need a coordinated schema/query change.
- Validate by: Preserve event names in the accepted REQ-080 contract and current telemetry tests.
```

## 6. Plan
- Approach: First inspect every auth state, start-screen/account route, main entry path, build dependency and test. Remove those routes and their now-unused implementation, start the existing anonymous telemetry task before/independent of startup gates, and preserve the diagnostics routes. Update UI tests, privacy policy, stale product docs, and task records.
- Files/functions to touch: `src/qt/EditorDialogs.cpp`, `EditorWindow.*`, `EditorProjects.cpp`, start-screen/ribbon code; `src/app/main.cpp` or Qt entry point; `CadCommands.hpp`; `UserPrefs.*`; `TelemetryService.*` / `TelemetryPing.*`; `HttpFetch.*`; `CMakeLists.txt`; `tests/QtEditorTests.cpp`, `TelemetryPingTests.cpp`; `PRIVACY.md`; auth setup/parity docs; this task.
- Test approach: happy path = exact anonymous payload and one install/every later launch; failure = offline/network failure is silent and app/startup remains usable; Qt coverage = no account or telemetry notice UI and no launch gate; diagnostics route coverage remains green.
- Steps:
  - [x] Verify all consumers of Auth, CredentialStore, OAuthListener, auth prefs and HTTP helpers; define precise deletion list.
  - [x] Remove account UI and all sign-in/launch-gate state and startup work.
  - [x] Start anonymous telemetry independently and keep diagnostics intact.
  - [x] Update tests, privacy policy, and stale docs.
  - [x] Build and run applicable tests; capture affected Qt surfaces and present them for user approval.

## 7. Workflow-specific notes
- Feature: requirements and acceptance are explicit in D-2026-10-24-a / REQ-080. Verification plan: **APPROVE**. Review found all requested behaviors mapped to acceptance criteria, one-shot thread architecture already exists, no new dependencies or data formats are needed, and diagnostics remain expressly out of scope. The removed UI surfaces can be verified with the Qt suite/screenshots.

## 8. Implementation log
- 2026-10-24: Recorded D-2026-10-24-a. Amended REQ-080 and retired REQ-091/092. User confirmed diagnostics stay. Plan reviewed and approved before implementation.
- 2026-10-24: Removed desktop Auth0/login client, launch gating and account/settings UI; removed the email from the ping payload; the ping now starts before startup update handling on its own worker. Retained diagnostic POSTs and updated the root and site privacy disclosures.
- 2026-10-24: User approved the UI screenshots. `build.bat` passed; all six `qt.editor` shards passed; `GoSurveyTests.exe [telemetry]` passed (43 assertions, 6 cases); `build.output-layout` and `git diff --check` passed.

## 9. Self-verification
- [x] build-project — `build.bat` passed.
- [x] architecture-review — auth ownership removed from the client and build; telemetry remains a one-shot background worker; diagnostic upload path retained; no new dependency or abstraction.
- [ ] code-review — not run as a separate review.
- [x] dependency-audit — n/a; no new dependencies.
- [x] performance-review — n/a; no new foreground work; network ping remains on its worker.
- [x] testing — all six Qt editor shards, telemetry tests (43 assertions / 6 cases), output-layout test, and diff whitespace check passed.

## 10. Verification result
- Submitted: 2026-10-24
- Verdict: PASS
- Findings: none in the executed build and test checks.

## 11. Outcome
- Requirements satisfied: REQ-080 updated; REQ-091/092 retired; diagnostics under REQ-425 retained.
- Tests added: startup without sign-in, settings without the notice/account category, and anonymous payload without email.
- Docs updated: `PRIVACY.md`, site privacy content, release notes, Cloudflare Pages setup, SPEC and this task.
- Done: user approved screenshots; ready for PR to `beta`.
