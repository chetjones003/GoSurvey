# TASK-916 — Qt point-cloud attach, import progress and export

- Type: feature
- Status: implemented; visual approved; PR open
- Opened: 2026-10-19
- Owner: Workshop
- GitHub: #916, child of #878

## 1. Authority
- Goal: Qt migration to 0.7.4 parity.
- Requirements: REQ-172 (E57 / LAS / LAZ / PTS / PTX interchange), REQ-171 (point-cloud entity), REQ-379 clause 1 (project Copy / Link / Cancel).
- Constraints: REQ-001 / REQ-201 (malformed files refuse, honest failure), REQ-300 (no new dependency), REQ-301 (no abstraction without two uses).
- Issue acceptance: preserve 0.7.4 cancel, validation, failure, undo and read-only behavior; the Qt workflow is fully usable (a shared route or visible button alone is not parity); update the parity ledger; focused workflow coverage; 2–3 screenshot options per major surface and apply the user's selection.
- Behavior reference: ImGui `POINTCLOUDATTACH` (native picker → background `.gscloud` build with progress/ETA/Cancel → one undo step "Import point cloud") and `EXPORTPTS` / `EXPORTPTX` / `EXPORTLAS` / `EXPORTLAZ` (selected clouds, or all when none selected, merged into one file).

## 2. Scope
- In scope: Qt attach dialog (Insert → Point Cloud → Attach, typed `POINTCLOUDATTACH`), validation before import, background-import progress window with Cancel and failure reporting, Qt export dialog (Output → Export → Export Point Cloud, typed `EXPORTPTS/PTX/LAS/LAZ`), project Copy / Link prompt continuation, read-only refusal, tests, ledger.
- Out of scope: E57 export (no E57 writer exists in 0.7.4; the export dialog says so), ImGui behavior changes, new dependencies, point-cloud display/centerline tools (already present).

## 3. Files / subsystems
- `src/qt/EditorPointClouds.cpp` (new): the two dialogs and the progress window. `src/qt/EditorWindow.*`: entry points, `Submit` routing, service tick. `src/qt/RibbonCatalog.hpp`: enabled Attach, added Export Point Cloud.
- `src/commands/CadCommands.*`: `ExportPointCloudsToFile` / `PlanPointCloudExport` (the four typed export commands now share one writer behind it, they only differ in how the path is chosen) and `UpdatePointCloudImportProgress` (phase / fraction / ETA moved out of the ImGui modal, which now uses it).
- Tests: `tests/QtEditorTests.cpp` (`issue916*`).

## 4. Architectural boundary check
- Reader/writer/cache code is untouched. Qt owns only presentation and user choices; import still goes through `RequestProjectAttach` → `StartPointCloudImportAsync` → `TickPointCloudImport`, export through the shared writer. The two shared helpers each have two concrete users (ImGui + Qt; four typed commands + Qt dialog), so they satisfy the two-uses rule. No new storage, layer or dependency.

## 5. Plan review
- **Plan check: PASS.** The shared command layer already provides every behavior; the gap is purely that Qt exposed no route (the ribbon Attach was a disabled placeholder and the typed commands opened a Win32 picker).
- Read-only: attaching changes the drawing, so a read-only project drawing is refused with a message, matching the Qt PDF attach rule (#909). Export does not change the drawing and is allowed.
- Undo: the import already pushes one "Import point cloud" snapshot in `TickPointCloudImport`; export is not an edit.

## 6. Verification record
- Qt Release build: PASS (`build.bat qt`). ImGui Release build and full Catch2 suite: PASS (2,501 cases, including the new `[issue916]` cases for the shared export and progress helpers).
- Focused Qt workflows PASS: `issue916AttachDialogValidatesThenImportsEveryFormat`, `issue916ImportFailureCancelAndReadOnlyLeaveTheDrawingUnchanged`, `issue916AttachRefusesReadOnlyProjectDrawingsAndAsksCopyOrLink`, `issue916ExportDialogWritesEveryFormatAndRoundTrips`, `issue916ExportConfirmsReplacementAndReportsWriteFailure`, `issue916RibbonOffersAttachAndExport`, plus the ribbon-inventory test (button count 284 → 285) and the two existing point-cloud project-attach tests.
- Full `qt.editor` run exceeds its 280 s limit; before it stopped, two unrelated tests failed (`customTitleBarAndFloatingCommandLineWork`, `issue903PolarSettingsAndOneShotSnap`). Not re-run on `beta`.
- Screenshot options A (app theme), B (deep navy/aqua), C (graphite/amber) were captured under `build/artifacts/Release/qt-916-*`. User selected C; it is now the dialogs' own style and the final captures are `qt-916-*-final-graphite-amber.png`. Visual approval received.
- Known limits: E57 is import-only (no writer in 0.7.4). Export runs on the UI thread, as in ImGui.
