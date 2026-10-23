# TASK-883 — Qt telemetry and crash-report workflows

## Authority

- GitHub issue #883 (parent tracker #878).
- Accepted **REQ-080** (usage ping after the launch gate; Settings disclosure; no opt-out) and
  **REQ-425** (crash and startup-failure reports, recorded from 0.7.4 by D-2026-10-17-b).
- Beta 0.7.4 behaviour is authoritative except where REQ-080/REQ-425 say otherwise (the
  disclosure wording, D-2026-10-17-b).

## Scope

- **Usage ping (REQ-080).** Fire one `telemetry::BeginTelemetryPing` per launch once the Qt launch
  gate resolves (signed in, opted out, declined, or the offline exception), with the signed-in email
  or empty. The channel is the launch-time update channel. Switched on by the entry point only, so
  test windows never send a ping.
- **Settings → "What GoSurvey sends" (REQ-080, REQ-425 §6).** A text disclosure, not a toggle (ADR-032: no
  opt-out). It states a ping each launch (not the stale "at most once a day"), the email sentence
  switches with sign-in state, and one sentence discloses crash/startup reports.
- **Crash capture (REQ-425 §1, §7).** `GoSurveyQt.exe` installs the shared handler and honours
  `--gosurvey-test-crash`; the handler relaunches the Qt executable.
- **Crash report window (REQ-425 §2–§3).** `GoSurveyQt.exe --gosurvey-crash-report=<bundle>`
  reads, sends and deletes the bundle (shared code), then shows a Qt report window: read-only report,
  Send This Report (clipboard + GitHub new-issue URL + outcome message, window stays open) and Close.
- **Startup failure (REQ-425 §4).** Before the editor opens, Qt probes an OpenGL 3.3 core context
  and the viewport renderer; a failure logs, sends, shows the Qt report window titled "GoSurvey could
  not start", and exits 1. Skipped in `--smoke-test` (a modal would hang the run).

## Files and boundaries

- `src/platform/CrashReporter.*`: split `TryRunCrashReportUiMode` into reading/sending the bundle
  (`ReadCrashBundle`, `TakeCrashReportFromCommandLine`) and showing it, so Qt reuses the first half.
  ImGui behaviour unchanged.
- `src/platform/StartupFailure.*`: split `FailAndShow` into `RecordAndSend` (log + POST, returns the
  report text) and the Win32 dialog/exit. ImGui behaviour unchanged.
- `src/platform/FailureReportUi.*`: expose the GitHub issue URL builder and the send-outcome text so
  the Win32 and Qt windows share one copy (two concrete uses).
- `src/qt/FailureReport.{hpp,cpp}` (new): Qt report window + OpenGL startup probe.
- `src/qt/EditorWindow.*`, `EditorDialogs.cpp`, `main.cpp`, `cmake/GoSurveyQt.cmake` (link the
  telemetry/crash/startup sources), `tests/QtEditorTests.cpp`, `docs/qt-074-parity-ledger.md`.
- Architecture check: platform code stays UI-toolkit-free; Qt code only presents. No new dependency,
  no new global state beyond what the 0.7.4 handler already has.

## Tests

- Ping fires once after the gate resolves, not before, with email when signed in and empty when
  signed out; never without the entry point's switch.
- Settings disclosure text for signed-in and signed-out, and the crash-report sentence.
- Report window: read-only text, Send copies + opens the URL (injected opener) + reports the
  outcome and stays open; a failed open says so; Close closes.
- Crash bundle read-back and deletion; malformed bundle rejected.
- The launch graphics check passes on a working driver (hidden-desktop run, real GPU).
- Manual: `--gosurvey-test-crash` on the Qt build.

## Decisions

- D-2026-10-17-b (user): record 0.7.4 as REQ-425; accurate disclosure plus crash-report sentence.
- UI direction selected by the user from three integrated options each: report window **B** (navy banner,
  matching the Software Update dialog) and Settings **B** ("What GoSurvey sends" card below Updates).
- No new wire stage names: Qt maps a missing 3.3 context to `glfw_window` and a renderer failure to
  `opengl_init`, so the server's categories are unchanged.
- Known gap: the shared OpenGL-failure summary mentions "Settings → System → Hardware
  Acceleration", which the Qt build does not have; left shared to keep ImGui unchanged.

## Verification

- Qt build (Release, Qt 6.8.3 MSVC) and the ImGui `GoSurvey` + `GoSurveyTests` builds: OK, no new
  warnings.
- `GoSurveyTests`: all 1632 test cases pass (shared platform refactor).
- `GoSurveyQtTests`: the 8 new tests pass; full suite 57/61 before the probe test was added, the 4
  failures being the ones already failing on `beta` (layer manager, two release-ribbon tests, tab
  focus).
- Not automated: a real crash relaunch, a real driver failure, and the live POSTs (they would write
  rows to the production endpoints).
