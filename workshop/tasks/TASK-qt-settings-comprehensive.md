# TASK: Comprehensive Qt Settings window (issue #1032)

## Requirement authority
REQ-449 (accepted), decision D-2026-10-21-a. Related: REQ-447 account/update settings.

## Files / subsystems
- `src/qt/EditorDialogs.cpp` (`EditorWindow::ShowSettings`): sidebar + stacked pages (Account, Updates and Privacy, Display, Drafting, Survey Points), staged Apply/OK/Cancel.
- `src/qt/EditorViewport.cpp`: mouse-wheel zoom uses `displayWheelZoomFactor`.
- `tests/QtEditorTests.cpp`: new and updated tests.
- `spec/requirements.md`, `spec/project.md`: REQ-449, D-2026-10-21-a.

## Approach
Each control registers a commit closure; Apply/OK run them all then save `gosurvey-user.json`; Cancel discards. Only prefs with a Qt feature behind them are exposed. Drawing/project/traverse settings, cursor controls and unwired legacy prefs are out of scope.

## Tests
`issue1032SettingsWindowStagesAndPersists`, `issue1032WheelZoomUsesPreference`, `settingsUpdatesAreStagedUntilApply`; full `qt.editor` suite.

## Architectural-boundary check
Qt UI only; reuses `AppCommandState` and `UserPrefs` persistence. No new dependencies.

## Completion report
- Build: `.\build.bat` clean. `qt.editor` passes.
- Assumption: prefs with no Qt consumer are not exposed (disclosed in D-2026-10-21-a).
- Technical debt: legacy ImGui-era prefs remain saved but unused.
