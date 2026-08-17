# TASK-057 — Anonymous install and active-usage telemetry

- Type:    feature
- Status:  implement
- Opened:  2026-08-16
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         (none — feature request, not roadmap-driven)
- Requirements: REQ-080 (must be `accepted`)
- Constraints:  REQ-300 (no new HTTP dependency), REQ-301 (no new abstraction), REQ-201 (no silent failures — narrow exception for background calls)
- Acceptance:
  - on first run, an `install` event is sent exactly once; subsequent runs do not resend it
  - on any run, an `active` event is sent at most once per rolling 24-hour period
  - the payload JSON is well-formed and contains exactly the five fields (installId, event, version, channel, os)
  - no PII is included in any payload (absence of username, hostname, path, email, hardware ID)
  - network failures do not raise an exception, log a message, or otherwise fail the application
  - killing network access does not hang or freeze the startup
  - a privacy disclosure is present in the UNITS dialog or settings panel
  - the current build sends pings to the configured endpoint and an inspector tool confirms payload shape and timing
- Owning subsystem: Platform (`PostJson`), Telemetry (pure logic), IO (persistence)

## 2. Scope
- In scope: 
  - `src/telemetry/TelemetryPing.hpp/.cpp` — pure logic for payload building, install-vs-active decision, 24h rate limiting
  - `src/platform/HttpFetch.hpp/.cpp` — new `PostJson(url, body)` helper alongside existing GET/download
  - `src/io/UserPrefs.hpp/.cpp` — extend `gosurvey-user.json` with `installId` and `lastActivePingDate` fields
  - `src/app/` — startup wiring to spawn telemetry worker alongside update check
  - Unit tests for `TelemetryPing` (Catch2/ctest, no network/window)
  - Privacy disclosure text in settings panel
  
- Out of scope:
  - The backend receiving the pings (user responsibility — e.g. Cloudflare Worker)
  - License-key enforcement or gating (tracking only, enforcement deferred)
  - Opt-out toggle or consent UI (always-on, no PII)
  - CI/CD endpoint configuration (compile-time constant)

- Smallest change: the four files above, with `TelemetryPing` unit tested and the worker thread wired to startup

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change?
    - [x] No — proceed. (`PostJson` extends existing `HttpFetch` / pure `util/` module / existing `UserPrefs` store)

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| (none at this stage) | — | — | — |

## 5. Assumptions
```
ASSUMPTION-1: The endpoint URL is a compile-time constant, not configurable at runtime
- Because: Settings persistence must survive a reset; user-preference redirect could compromise telemetry fidelity
- Risk if wrong: User could reconfigure pings to a different host, but the constant cannot be changed post-build
- Validate by: Compile-time constant `TelemetryEndpoint` in `TelemetryPing.hpp`; confirm no UserPrefs lookup reaches it

ASSUMPTION-2: 24-hour rate limiting uses calendar date (YYYY-MM-DD), not elapsed seconds
- Because: Calendar dates are easier to reason about and don't require a monotonic clock; simple string comparison
- Risk if wrong: If a user's clock is adjusted, pings could re-fire or be skipped within the same calendar day
- Validate by: Test with known date strings and clock-adjustment scenarios
```

## 6. Plan
- Approach:
  1. Create `src/telemetry/TelemetryPing.hpp` — pure logic only, no network/window
     - Function to generate a random 128-bit hex install ID
     - Function to decide install-vs-active based on presence/absence of `lastActivePingDate` and current date
     - Function to build the JSON payload (installId, event, version, channel, os)
     - All testable with Catch2/ctest
  2. Create `src/telemetry/TelemetryPing.cpp` — implement the above
  3. Extend `src/platform/HttpFetch.hpp/.cpp` with `PostJson` helper
  4. Extend `src/io/UserPrefs` to load/save `installId` and `lastActivePingDate`
  5. Wire into `src/app/` (alongside `UpdateService` spawn) — one-shot worker thread, independent
  6. Add privacy disclosure to settings panel
  7. Unit tests: payload shape, install event once, active event 24h throttling, no network errors

- Files to touch:
  - `src/telemetry/TelemetryPing.hpp` (new)
  - `src/telemetry/TelemetryPing.cpp` (new)
  - `src/platform/HttpFetch.hpp`
  - `src/platform/HttpFetch.cpp`
  - `src/io/UserPrefs.hpp`
  - `src/io/UserPrefs.cpp`
  - `src/app/` (likely `CadUi.cpp` or similar startup entry point)
  - `src/ui/CadUiSettings.cpp` (add disclosure text)
  - `CMakeLists.txt` (add TelemetryPing source)
  - `tests/TelemetryPingTests.cpp` (new Catch2 tests)

- Test approach:
  - Happy path: generate ID, check it persists, fire `install` once, then `active` once per 24h
  - Failure mode: network unavailable (timeout, DNS, TLS), should not crash or log (silently drop)
  - Edge case: clock adjustment within same calendar day (both should not re-fire)

- Steps:
  - [ ] Create `src/telemetry/TelemetryPing.hpp` with pure API
  - [ ] Implement `TelemetryPing.cpp` with ID generation, date parsing, payload building
  - [ ] Create unit tests for `TelemetryPing` (Catch2)
  - [ ] Extend `HttpFetch` with `PostJson` helper
  - [ ] Extend `UserPrefs` to persist telemetry fields
  - [ ] Wire startup (one-shot worker) in `src/app/`
  - [ ] Add privacy disclosure to settings panel
  - [ ] Run self-verification (build, architecture, code review, testing)
  - [ ] Write completion report

## 7. Workflow-specific notes
- Feature: pre-flight questions answered? Yes (user provided 3 binding decisions in spec DECISION LOG). Tests-first? Yes (TelemetryPing logic is testable; write Catch2 before wiring).

## 8. Implementation log
- 2026-08-16: Task created from approved plan.

**Completed implementation (steps 1-7 ready for testing):**
  - [x] TelemetryPing.hpp/cpp: pure logic module (no network, no window)
    - GenerateInstallId(): 128-bit random hex ID
    - DecideEventToSend(): install vs. active vs. nothing
    - BuildTelemetryJson(): JSON payload builder
    - GetTodayDateString(): calendar date (YYYY-MM-DD)
    - IsAfter24Hours(): rate-limit decision
  - [x] HttpFetch extension: PostJson(url, body, timeout) WinHTTP helper
  - [x] UserPrefs extension: GetTelemetryIds() and UpdateTelemetryIds() JSON file I/O
  - [x] CMakeLists.txt updates: added src/telemetry/ sources and includes to all targets
  - [x] TelemetryPingTests.cpp: 9 test cases covering ID generation, event logic, date parsing, JSON building
  
**Completed (wiring + service):**
  - [x] Created TelemetryService.hpp/cpp (fire-and-forget pattern like UpdateService)
  - [x] Wired into src/app/main.cpp:
    - Include TelemetryService.hpp and Version.hpp
    - Initialize telemetryTask at startup (line ~274)
    - Poll telemetryTask in main loop (line ~381)
  - [x] Updated CMakeLists.txt: added TelemetryService.cpp to main executable

**Remaining (2-3 small tasks):**
  - [ ] Add privacy disclosure text to CadUiSettings.cpp settings panel (1 text string)
  - [ ] Update TelemetryEndpoint constant with your backend URL (1-line change)
  - [ ] Verify build passes in MSVC Developer Command Prompt
  - [ ] Optional: `/code-review` to audit the telemetry subsystem
  - [ ] Optional: `/testing` TelemetryPingTests
  - [ ] Smoke test with request inspector before going live
