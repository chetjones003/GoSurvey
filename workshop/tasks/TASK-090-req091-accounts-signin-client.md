# TASK-090 — User accounts sign-in (client side): Auth0, PKCE + loopback, Credential Manager

- Type:    feature
- Status:  implement
- Opened:  2026-08-23
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         (none — feature request, not roadmap-driven)
- Requirements: REQ-091 (must be `accepted`)
- Constraints:  REQ-300 (dependency policy — answered in ADR-037(a)), REQ-301 (no unnecessary
  abstraction), REQ-201 (no silent failures)
- Acceptance (restated from REQ-091):
  - clicking "Sign In" opens the system browser to Auth0 Universal Login showing Google, Microsoft,
    and email/username/password
  - completing sign-in by any method returns control to the app (loopback redirect caught) and the
    settings panel shows "Signed in as `<email>`"
  - reopening the app does not require interactive sign-in again while the refresh token is valid
    (silent refresh)
  - an expired/revoked refresh token forces interactive sign-in on next launch
  - the refresh token never appears in `gosurvey-user.json` or any plaintext file — verified via
    Credential Manager
  - no application feature is gated by sign-in state or tier
  - REQ-080's telemetry ping is unchanged
- Owning subsystem: Platform (`OAuthListener`, `CredentialStore`, SHA-256/base64url), new `auth/`
  peer to `telemetry/` (pure logic + orchestration), UI (`CadUiSettings.cpp`)

## 2. Scope
- In scope:
  - `src/platform/OAuthListener.hpp/.cpp` — loopback HTTP listener (Winsock), single connection,
    parses `code`/`state`/`error` from the callback query string
  - `src/platform/CredentialStore.hpp/.cpp` — `CredWriteW`/`CredReadW`/`CredDeleteW` wrapper for the
    refresh token
  - `src/platform/HttpFetch.hpp/.cpp` — add `ComputeSha256(bytes)` byte-buffer overload beside the
    existing `ComputeFileSha256`; add a base64url-encode helper
  - `src/auth/AuthPing.hpp/.cpp` — pure logic: PKCE pair generation, `/authorize` URL construction,
    silent-refresh-vs-interactive decision
  - `src/auth/AuthService.hpp/.cpp` — orchestration: one-shot worker thread driving the listener,
    `ShellExecute` browser launch, token exchange/refresh via `HttpPostJson`
  - `src/ui/CadUiSettings.cpp` — Sign In entry point beside the existing "Anonymous Usage Data" box
    (~line 387); "waiting for browser…" modal with cancel/timeout; "Signed in as `<email>`" once
    authenticated
  - `CMakeLists.txt` — add new sources; link `ws2_32` and `advapi32` alongside the existing
    `winhttp bcrypt ole32` (line ~304)
  - `tests/AuthPingTests.cpp` — Catch2, no network/window
- Out of scope:
  - The backend (`accounts-worker`) — TASK-091
  - Any feature gating on tier — REQ-091 explicitly excludes this
  - Auth0 tenant configuration itself (operator task, like the Cloudflare Worker deploy step for
    telemetry) — this task assumes a tenant/client-id exists to point at, exactly as
    `TelemetryEndpoint` assumed a deployed Worker
- Smallest change: the files above; `AuthPing` unit-tested like `TelemetryPing`, `AuthService`
  mirrors `TelemetryService`'s worker-thread shape exactly

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API
  or data-format change?
    - [x] No — proceed. (ADR-037 already recorded the one architectural decision this touches —
          Auth0 as a dependency, the PKCE+loopback flow, and Credential Manager storage. This task
          implements that decision; it introduces no further one. `src/auth/` is a peer module, not a
          new layer — same relationship `src/telemetry/` already has to `src/platform/`.)

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| (none — REQ-091/ADR-037 already resolved the open questions during spec review) | — | — | — |

## 5. Assumptions
```
ASSUMPTION-1: An Auth0 tenant, application (client id), and callback configuration will be provisioned
  by the operator before this ships, the same way TelemetryEndpoint assumed a deployed Cloudflare
  Worker.
- Because: Tenant creation/configuration is an operator action outside the codebase, not something
  Workshop can do from source control.
- Risk if wrong: sign-in fails end-to-end until the tenant exists; the client code is still correct
  and testable independently (AuthPingTests need no live tenant).
- Validate by: a docs/auth0-setup.md operator guide, mirroring docs/cloudflare-telemetry-setup.md,
  plus a compile-time Auth0 domain/client-id constant analogous to TelemetryEndpoint.

ASSUMPTION-2: The loopback listener binds an OS-assigned ephemeral port (port 0) rather than a fixed
  port.
- Because: A fixed port can already be in use by another process; RFC 8252 recommends ephemeral
  loopback ports for exactly this reason.
- Risk if wrong: none identified — this is the standard-recommended approach, not a judgment call.
- Validate by: OAuthListener unit/manual test binds successfully on a machine with common dev ports
  already occupied.

ASSUMPTION-3: The redirect_uri sent to Auth0's /authorize must be registered in the Auth0 application's
  Allowed Callback URLs as a wildcard loopback pattern (http://127.0.0.1:*/callback) or equivalent,
  since the port is chosen at runtime.
- Because: Auth0 (like most OAuth providers) validates redirect_uri against a registered allowlist.
- Risk if wrong: token exchange is rejected with a redirect_uri mismatch error at runtime.
- Validate by: manual end-to-end sign-in test against the dev tenant.
```

## 6. Plan
- Approach:
  1. Add `ComputeSha256(bytes)` overload + base64url-encode helper to `HttpFetch.*` (small, reuses
     the existing BCrypt call already used by `ComputeFileSha256`).
  2. Write `src/auth/AuthPing.hpp/.cpp` — pure: PKCE verifier/challenge, `/authorize` URL builder,
     `DecideAuthAction(refreshTokenPresent, tokenExpired) -> "silent-refresh"|"interactive"`. Fully
     unit-testable, no network/window, mirrors `TelemetryPing`'s shape.
  3. Write `tests/AuthPingTests.cpp` (Catch2) covering PKCE/base64url correctness and the decision
     function's four cases (no token / valid token / expired token / revoked-detected-at-refresh).
  4. Write `src/platform/OAuthListener.hpp/.cpp` — bind ephemeral port, accept one connection,
     parse query string off the request line, write a static "you can close this tab" HTML response,
     close. Blocking, called from a worker thread only (matches `HttpFetch`'s existing contract).
  5. Write `src/platform/CredentialStore.hpp/.cpp` — `StoreRefreshToken`/`LoadRefreshToken`/
     `DeleteRefreshToken` over `CredWriteW`/`CredReadW`/`CredDeleteW`, named credential
     `GoSurvey/Auth0RefreshToken`.
  6. Write `src/auth/AuthService.hpp/.cpp` — one-shot worker thread (architecture §8 pattern):
     launches the listener, opens the browser via `ShellExecute`, awaits the callback, exchanges the
     code for tokens (`HttpPostJson` to Auth0's `/oauth/token`), stores the refresh token, holds
     access/ID token in memory. A second entry point does silent refresh at startup from a stored
     token.
  7. Wire into `src/app/main.cpp` alongside the existing `telemetryTask`/`updateTask` poll loop —
     independent one-shot worker, no gating.
  8. Add the Sign In UI to `CadUiSettings.cpp`: button, "waiting for browser…" modal with cancel +
     timeout, "Signed in as `<email>`" state once authenticated.
  9. `CMakeLists.txt`: add new sources to `GoSurvey` and `GoSurveyTests` targets; link `ws2_32`,
     `advapi32`.
- Test approach:
  - Happy path: `AuthPingTests` — PKCE pair is well-formed (43-128 char verifier, correct S256
    challenge), `/authorize` URL contains all required params, decision function picks
    silent-refresh when a valid token is stored.
  - Failure mode: decision function picks interactive when no token, when the stored token is
    expired, and when a refresh attempt is rejected (simulated, not live network).
  - Manual: full sign-in against a dev Auth0 tenant (Google, Microsoft, email/password), restart
    persistence, Credential Manager inspection, revoked-token fallback.
- Steps:
  - [ ] `ComputeSha256(bytes)` + base64url helper in `HttpFetch.*`
  - [ ] `src/auth/AuthPing.hpp/.cpp`
  - [ ] `tests/AuthPingTests.cpp`
  - [ ] `src/platform/OAuthListener.hpp/.cpp`
  - [ ] `src/platform/CredentialStore.hpp/.cpp`
  - [ ] `src/auth/AuthService.hpp/.cpp`
  - [ ] Wire into `src/app/main.cpp`
  - [ ] Sign In UI in `CadUiSettings.cpp`
  - [ ] `CMakeLists.txt` updates
  - [ ] Self-verification (build, architecture, code review, testing)
  - [ ] Completion report

## 7. Workflow-specific notes
- Feature: pre-flight questions answered? Yes (spec review + user decisions recorded in
  D-2026-08-23-c). Tests-first? Yes (`AuthPingTests` written alongside `AuthPing`, before
  `AuthService`/UI wiring).

## 8. Implementation log
- 2026-08-23: Task created from approved plan + APPROVE verdict.
- 2026-08-23: Implemented all steps.
  - `ComputeSha256Bytes` added to `HttpFetch.hpp/.cpp` (byte-buffer BCrypt SHA-256, refactored core
    from the existing `ComputeFileSha256`).
  - `src/auth/AuthPing.hpp/.cpp`: PKCE verifier generation, base64url encode **and** decode (decode
    added beyond the original plan — needed to read the ID token's email claim client-side for
    display), authorize-URL builder, `DecideAuthAction`. All pure, no network/window.
  - `tests/AuthPingTests.cpp`: 10 cases / 124 assertions, all green.
  - `src/platform/OAuthListener.hpp/.cpp`: loopback listener over raw Winsock, ephemeral port,
    single-connection accept with a `select()`-based timeout, query-string parsing with percent-
    decoding, static HTML response.
  - `src/platform/CredentialStore.hpp/.cpp`: `CredWriteW`/`CredReadW`/`CredDeleteW` wrapper under
    target name `GoSurvey/Auth0RefreshToken`.
  - `src/auth/AuthConfig.hpp` (new, not in the original plan list): compile-time
    `kAuth0Domain`/`kAuth0ClientId`/`kAuth0Audience`, mirroring `TelemetryEndpoint`'s pattern
    exactly (ADR-032 (f)/ADR-037). Added because the orchestration code needs *some* place to read
    tenant config from, and a compile-time constant is what ADR-037 already decided.
  - `src/auth/AuthService.hpp/.cpp`: `BeginInteractiveSignIn`, `BeginSilentRefresh`, `PollAuthTask`,
    `SignOut`. Uses `nlohmann::json` (already a project dependency, not new) for the token-exchange
    request/response bodies and ID-token payload decoding.
  - Wired into `src/app/main.cpp`: silent refresh at startup, per-frame handling of
    `authSignInRequested`/`authSignOutRequested`, result capture into `AppCommandState`.
  - `src/commands/CadCommands.hpp`: added `authSignedIn`, `authBusy`, `authInteractiveBusy`,
    `authEmail`, `authError`, `authSignInRequested`, `authSignOutRequested` to `AppCommandState`
    (flags/strings only, no thread — stays copyable, same split as `updatePrefs`).
  - `src/ui/CadUiSettings.cpp`: `DrawAccountsSignIn`, an "Account" box beside "Anonymous Usage
    Data" in the System settings tab.
  - `CMakeLists.txt`: new sources added to `GoSurvey` and `GoSurveyTests`; `ws2_32`/`advapi32`
    linked; `src/auth` added to both targets' include paths.
- 2026-08-23: **User-discovered defect while configuring the real Auth0 tenant**: Auth0's Allowed
  Callback URLs field rejects a wildcard in the port position outright (`"callbacks" must be a
  valid uri`) — its documented placeholder support is "subdomain or domain name only," never the
  port. The original design (`OAuthListener` binding an OS-assigned ephemeral port per RFC 8252)
  cannot be registered with Auth0 at all, so it would have failed sign-in end-to-end on first live
  attempt. Fixed: `OAuthListener::Start` now takes a caller-supplied fixed port;
  `AuthService::BeginInteractiveSignIn` tries a short list of candidates
  (`kOAuthCallbackPorts` in `AuthConfig.hpp`) in order. ADR-037 (b) amended in `architecture.md` to
  record this (a real correction to a previously-recorded decision, not a silent patch);
  `docs/auth0-setup.md` Step 1.3 updated to the three exact URLs to register. Full suite re-run
  green after the fix (214577 assertions / 513 cases). This is exactly the kind of thing
  ASSUMPTION-1/3 flagged as unvalidated until a real tenant existed — now validated, and wrong in
  the way ASSUMPTION-3 anticipated ("redirect_uri mismatch"), just caught one layer earlier (at
  Auth0's dashboard validation) than expected (at token exchange).
- 2026-08-23: Self-review caught a real correctness gap before it shipped: `ApplyTokenResponse`
  would have set `task->ok = true` on ANY token-endpoint response, including one missing
  `access_token` entirely (e.g. a malformed 2xx). Fixed by requiring `access_token` to be present
  before reporting success (REQ-201 — a 2xx alone is not proof of a usable result, same reasoning
  `TelemetryService` applies to the literal `"ok":true` check). Full suite re-run green after the
  fix (214577 assertions / 513 cases, no regressions).

## 8b. Addendum — launch-time sign-in gate (2026-08-23, D-2026-08-23-d)
- User request, after live end-to-end testing against the real Auth0 tenant succeeded: keep the
  Settings → Account UI, but also present a mandatory sign-in window if the user isn't signed in.
- This reverses REQ-091's original "no application feature is gated" acceptance condition for
  SESSION ACCESS specifically (not individual features/tiers, which remain ungated) — asked and
  recorded as a decision (D-2026-08-23-d) before implementing, since it's a real behavior reversal,
  not a UI tweak. Two clarifying questions asked and answered: blocking (not dismissible), and an
  explicit offline exception (no connectivity → skip the gate, matching REQ-077's own precedent).
- Implemented:
  - `AuthTask` gained `skippedNoNetwork` (`src/auth/AuthService.hpp`).
  - `AuthService::BeginSilentRefresh` now calls `HasInternetConnectivity()` before spawning its
    worker (same call, same placement pattern as `UpdateService::BeginStartupCheck`) and sets
    `skippedNoNetwork` + resolves immediately with no thread spawned when offline.
  - `AppCommandState` gained `authGateResolved` (`src/commands/CadCommands.hpp`).
  - `main.cpp`'s existing auth-task-completion handling now sets `authGateResolved = true` on
    `ok || skippedNoNetwork`.
  - New `src/ui/CadUi_SignInGate.cpp` / `DrawSignInGate` declared in `CadUi.hpp` — a blocking
    `ImGui::BeginPopupModal`, no close button, no click-away dismissal, same shape as
    `DrawUpdateDialog` (REQ-078). Called from `main.cpp` right after `DrawUpdateDialog`.
  - `CMakeLists.txt`: new source added to the `GoSurvey` target.
- REQ-091 and the traceability matrix updated in `spec/requirements.md`; decision recorded in
  `spec/project.md` (D-2026-08-23-d).
- Verified: full suite green after the change (214577 assertions / 513 cases, no regressions).
  Not yet verified live: the actual blocking behavior with a real Auth0 tenant and the offline
  skip path (would require disconnecting network during a manual test) — logic-level correctness
  only, same caveat as the rest of TASK-090.

## 8c. Addendum — menu bar email display, license-lookup wiring (2026-08-23)
- User confirmed the sign-in round trip works live against the real Auth0 tenant. Two follow-up
  requests:
  1. **Show the signed-in email in the application chrome**, not just Settings → Account —
     modelled on Civil 3D's top-right account display. Added a right-aligned
     `cmd.authEmail` label to the far right of `DrawMainMenuBar` (`src/ui/CadUi.cpp`), shown only
     when `authSignedIn`. No new requirement — this is the same REQ-091 "Signed in as `<email>`"
     acceptance condition surfaced in a second, more visible location.
  2. **Wire the client to actually call REQ-092's `/v1/license` endpoint.** Discovered while
     investigating why the user's sign-in didn't appear anywhere in Cloudflare: (a) `pings`
     (gosurvey-telemetry) is unrelated to accounts by design (ADR-037 (e)) — expected, not a bug;
     (b) `gosurvey-accounts`'s `users` table was empty because nothing in the client called
     `/v1/license` yet — this was a documented, deliberate deferral (`docs/auth0-setup.md`), not a
     defect, but the user asked for it to be wired up now.
- Implemented:
  - `src/platform/HttpFetch.hpp/.cpp`: `HttpGetString` gained an optional trailing `bearerToken`
    parameter (default empty, so both existing call sites are unaffected); `OpenRequest` gained an
    optional `extraHeaders` parameter carrying the `Authorization: Bearer` header when set.
  - `src/auth/AuthConfig.hpp`: added `kAccountsApiBaseUrl` (compile-time, same pattern as
    `kAuth0Domain`), and filled in the real deployed URL.
  - `src/auth/AuthService.hpp`: `AuthTask` gained a `tier` field (best-effort; empty on any lookup
    failure; never affects `ok`).
  - `src/auth/AuthService.cpp`: new `FetchLicenseTier(accessToken)` helper, called from
    `ApplyTokenResponse` after every successful token exchange (interactive sign-in AND silent
    refresh) — REQ-092's identity/tier separation is preserved: a failed lookup is logged, not
    surfaced, and does not undo a successful sign-in.
  - No new REQ needed for either change: (1) is the existing REQ-091 acceptance condition in a
    second location; (2) is REQ-092's own designed hook, deferred at TASK-091 time and now wired
    up — not a new capability.
- Verified: full suite green (214577 assertions / 513 cases). Confirmed via direct D1 query
  (`wrangler d1 execute gosurvey-accounts --remote --command "SELECT * FROM users"`) that the
  table was empty before this change — this is the evidence the gap was real, not assumed.
  Live re-verification (sign in again, confirm a row now appears) is the user's next manual step.

## 9. Self-verification
- [x] build-project        — PASS (`cmake --build build --target GoSurvey GoSurveyTests`, clean;
      MSVC/Ninja via `vcvars64.bat`, warnings only, all pre-existing patterns e.g. C4530 already
      present elsewhere in the codebase — not introduced by this task)
- [x] architecture-review  — PASS (no Workshop architectural decision; ADR-037 already covers the
      one this task implements; layering, ownership, no new global state, no new abstraction — see
      Step 2 Verification analysis)
- [x] code-review          — PASS (self-review found and fixed the `ApplyTokenResponse` gap above;
      no other findings — reviewed OAuthListener socket lifetime/WSAStartup pairing, CredentialStore
      error handling, and AuthService's task-result propagation for correctness)
- [x] dependency-audit     — PASS (Auth0 already recorded in the D-2026-08-23-c decision log entry
      and ADR-037(a); `nlohmann::json` is an existing project dependency, not new; `ws2_32`/
      `advapi32` are OS libraries, same status as `winhttp`/`bcrypt`/`ole32`)
- [ ] performance-review   — n/a (startup/interactive-only, not a per-frame path; REQ-100 unaffected)
- [x] testing              — PASS (`AuthPingTests`: 10 cases / 124 assertions, happy path +
      failure-mode covered; full suite 214577 assertions / 513 cases, no regressions; manual
      end-to-end against a real Auth0 tenant is **not yet run** — ASSUMPTION-1, no tenant
      provisioned yet)

## 10. Verification result
- Submitted:  2026-08-23
- Verdict:    PASS
- Findings:   one self-caught correctness finding (`ApplyTokenResponse` missing-token-data gap),
              fixed same day, re-verified green — see Implementation log

## 11. Outcome
- Requirements satisfied: REQ-091 (Acceptance met: mechanism-level yes — every acceptance
  condition is implemented and unit-tested at the logic level; the two conditions that require a
  live Auth0 tenant — end-to-end sign-in, Credential Manager round-trip across a real restart, and
  the actual expired/revoked-token fallback — are implemented but unexercised, per ASSUMPTION-1)
- Tests added:            `AuthPingTests.cpp` (10 cases, 124 assertions)
- Refactors:              `ComputeFileSha256`'s core BCrypt hashing logic informed
                          `ComputeSha256Bytes`'s shape (no shared code extracted — two call sites
                          with different I/O shapes: file vs. in-memory buffer — REQ-301 does not
                          justify it yet)
- Docs updated:           none in this task (docs/auth0-setup.md is TASK-091's deliverable, since
                          it covers the Auth0 tenant + Worker setup together)
- Done:                   2026-08-23

