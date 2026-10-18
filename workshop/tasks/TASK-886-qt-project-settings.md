# TASK-886 — Port project settings to Qt

## Authority

- GitHub issue #886 (parent tracker #878); accepted **REQ-375** and its decisions D-2026-10-05-d/e.
- Existing 0.7.4 behavior in `DrawDrawingSettingsWindow` is the parity reference, including staged edits, validation, Apply/OK/Cancel, atomic project save failure handling, project enforcement, and read-only behavior.
- Issue-level acceptance additionally requires a Qt-native usable workflow, focused workflow coverage, and the `docs/qt-074-parity-ledger.md` update.

## Scope

- Add a Qt Project Settings surface and project menu/ribbon route, operating on the open project's existing settings.
- Preserve the legacy Units and Zone + Object Layers behavior for project settings; settings remain staged until Apply/OK, Cancel discards staged edits, invalid values block saving, and persistence failure leaves the session's settings unchanged and reports the failure.
- Respect project read-only locks; preserve REQ-375 enforcement/inheritance behavior through existing services.
- Add Qt workflow coverage and mark this workflow accurately in the parity ledger.
- Before treating UI work as complete, present 2–3 integrated screenshot options for the Project Settings surface; apply the user's selected direction and capture final affected UI states inline in chat.

## Files / subsystem

- Qt UI and route: `src/qt/EditorWindow.*`, `src/qt/EditorProjects.cpp`, `src/qt/EditorDialogs.cpp` and/or a focused Qt source file.
- Qt workflow tests: `tests/QtEditorTests.cpp`.
- Parity evidence: `docs/qt-074-parity-ledger.md`.
- Domain and persistence remain in existing `ProjectSettings` / `gsproj` services. No spec, schema, dependency, or architecture changes.

## Implementation approach

1. Inspect project settings and `.gsproj` save APIs, current Qt routes, and legacy staged workflow.
2. Build integrated visual options for the Project Settings surface and get the user's direction before final UI styling.
3. Implement the selected Qt surface using existing services and save behavior.
4. Add workflow coverage for valid Apply/OK, Cancel, invalid input, save failure, and read-only behavior where these can be controlled deterministically.
5. Update the parity ledger with routes and evidence.

## Verification

- Build the Windows Qt target and run focused Qt workflow tests using the documented hidden desktop runner.
- Review the change against REQ-375 and project architecture/coding standards; record pass/fail and any unautomated cases.
- Capture meaningful final screenshots under `build/` and show them inline; pause for user approval before marking the UI complete.

## Plan review

- Authority check: REQ-375 directly requires the Project Settings window, project persistence, enforcement, and inherited defaults; the issue asks for a Qt port, not new domain behavior.
- Boundary check: domain, schema, and project storage stay unchanged; only Qt routes/presentation and workflow evidence change.
- Risk check: project-save failures and read-only locks can otherwise make a visually functional form mutate only in memory or claim success, so these paths need explicit coverage.
- Status: implemented in the user-selected design B (section navigation + impact panel); focused workflows pass. Full `qt.editor` suite reports four unrelated ribbon focus/context failures; see verification record below.
- User-approved visual follow-up: extend the Project Settings neutral charcoal treatment across the app, with darker workspace backgrounds and lighter gray panels/ribbon.

## Implementation and verification record

- Added `File → Project Settings…` and `PROJECTSETTINGS` routing for the active project drawing.
- Added the selected design B as a native Qt staged dialog with Units and Zone plus Object Layers sections and a project-wide effect panel.
- Project persistence uses the existing `.gsproj` save path. Apply/OK persist only after a successful save; Cancel discards staged edits; validation, save failure, and read-only locks are covered.
- Qt now depends on the existing CS-MAP staging target so coordinate-system dictionaries are available to the picker.
- Focused hidden-desktop workflows: all 3 pass (Apply/reload + Cancel; invalid input; save failure + read-only).
- Full `ctest --test-dir build-issue886 -R qt.editor --output-on-failure`: 53 passed, 4 failed. The failures are ribbon/focus/context tests (`tabsAppearInitiallyAndTypingRoutesFromEditorChrome`, `nativeLayerManagerEditsAreUndoable`, `releaseRibbonContextTracksSelectionAndRestoresNormalTab`, `releaseRibbonHatchDefaultsAndSelectionEditsAreSeparateAndUndoable`); the three issue-specific workflows pass.
- Final dialog captures are in `build/qt-886-project-settings-{units,object-layers,validation,save-error,read-only}.png`; modal warning captures are in the matching `*-message.png` files. These are local verification artifacts and are not committed.

## User-requested app-wide appearance follow-up

- Extended the selected neutral charcoal palette through the existing **Neutral gray** appearance: darker workspace/window surfaces, lighter gray ribbon and panels, neutral gray tabs, menus, docks, and controls. **Classic light** remains available.
- Updated the appearance workflow to assert the window and button palette colors; the main workspace and start page captures are `build/qt-886-neutral-gray-app.png` and `build/qt-886-neutral-gray-start.png`.
- Focused hidden-desktop workflows `customTitleBarAndFloatingCommandLineWork` and `appearanceChangesKeepDrawingAndToolspaceState`: pass (3 QtTest phases each). `git diff --check`: pass.
- The app-wide appearance is awaiting the user's visual review before this UI work is considered complete.
