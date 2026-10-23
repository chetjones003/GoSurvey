# TASK-1029 — What's New startup auto-open is suppressed after a completed update check

## Authority
- REQ-336 — Start Screen Billboard (What's New), accepted; specifically auto-open and its
  update-offer/direct-drawing precedence.
- REQ-091 — launch sign-in choice remains an application-modal gate.
- GitHub issue #1029.

## Expected behavior
When normal startup leaves the Start tab active, the installed version has not been dismissed,
and no update offer takes priority, What's New opens once for that launch. Dismissal remains scoped
to the exact version. Starting directly into a drawing and an update offer continue to suppress
the Start-screen opener.

## Root cause and evidence
`EditorWindow::MaybeAutoOpenWhatsNew` originally required `m_State.authGateResolved` in addition
to REQ-336, coupling it to REQ-091 authentication progress. A second precedence defect in
`src/qt/main.cpp` initialized startup before consuming a drawing path argument. Those causes were
fixed and covered by tests.

The real-launch failure remained because `main` exits its splash wait when the update worker marks
its task done, before the UI thread consumes that result through `ServiceUpdate`. The phase is still
`Checking`; `InitializeStartup` treated every non-Idle phase as a confirmed update offer and
permanently suppressed What's New. The regression test reproduces a completed but unprocessed
no-update result, then verifies startup consumes it and opens What's New.

The What's New Markdown also references six bundled images under `resources/whats-new/`. A focused
Qt assertion showed that registering them before `QTextBrowser::setMarkdown` loses the document
resources because parsing clears them. Registering decoded images after parsing fixes the display.

## Files and scope
- `src/qt/EditorWindow.cpp`: remove the unrelated auth-resolution gate, consume the completed
  startup update result before applying offer priority, and register images after Markdown parsing.
- `src/qt/main.cpp`: consume a direct drawing argument before evaluating Start-screen auto-open.
- `tests/QtEditorTests.cpp`: cover unresolved auth, completed no-update startup, dismissal,
  update/direct-drawing precedence, and all six decoded image resources.
- `tools/run-qt-tests-hidden.ps1`: mute Windows notification sound events only during hidden runs
  and restore the user's original registry values afterward.

## Verification plan
- Add a Qt regression assertion that fails with the current `authGateResolved` guard.
- Run the targeted Qt editor suite and the applicable project build/verification checks.
- Capture the What's New startup surface under the hidden Qt test desktop, save under
  `build/artifacts/`, and present it in the required T3 visualizer for user approval.

## Architectural-boundary check
No new state, API, dependency, or policy is introduced. REQ-091's application-modal interaction
gate remains intact; this only decouples the independent REQ-336 display eligibility from its
resolution.

## Status
Implemented and approved for PR by the user.

## Verification results
- `.\build.bat` — PASS (Release application and Qt test executable built).
- `ctest --preset ninja-release --output-on-failure` — PASS (191/191 after the final startup race
  fix and focused regression were added).
- `ctest --preset ninja-release -R "^qt[.]editor[.]shard-" --output-on-failure` — PASS (6/6 after
  the final startup race fix).
- Regressions: startup auto-open failed before the fix in `qt.editor.shard-3`; the image resource
  assertion failed before resource registration moved after `setMarkdown`. Both passed after fixes.
- The six bundled image resources decoded, and the Release test capture showed the Projects image.
- Completed-but-unprocessed no-update startup regression — PASS (3 QtTest checks including setup
  and teardown).
- Hidden-runner notification mute/restore — PASS; notification event values were unchanged after
  the run.
- Startup and drawing precedence: all Qt shards passed, including the direct-drawing guard and
  existing update-offer/dismissal checks.
- `git diff --check` — PASS.
- Architecture/code review: PASS; existing UI and auth boundaries retained, no new state or API.
- The app is running from this checkout's `build/GoSurvey.exe` for live user verification.
