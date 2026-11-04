# TASK-810 — PDF plot preview

- Type: feature
- Status: implemented; focused verification passed (Civil 3D comparison inconclusive)
- Opened: 2026-11-04
- GitHub: #810 (PDF Preview slice)
- Branch: `chore/triage-paper-space-issues` (shared paper-space batch)

## 1. Authority

- Goal: no numbered GOAL covers this UI workflow; the applicable paper-space output goal is represented by accepted requirements below.
- Requirements: REQ-486 (accepted), REQ-029, REQ-445, REQ-485, REQ-201.
- Constraints: REQ-200 (deterministic/reproducible output), REQ-300 (dependency discipline), CON-06 (smallest sufficient scope).
- Acceptance: REQ-486 Acceptance 1–5.
- Owning subsystems: Qt UI / IO; reuse the existing PDF plot writer and PDF viewer.

## 2. Scope

- In scope: enable the Output-tab Preview action for a paper layout; generate one temporary vector PDF with that layout's saved paper size, placement, and CTB/STB; open it in the existing editable PDF viewer; preserve the drawing and undo state; report plot errors without opening a false result.
- Out of scope: DWF/DWFx, raster or printer output, Plotter Manager/device setup, batch preview, editing plot styles, new PDF viewer functionality, and changes to the existing export workflow.
- Smallest change: invoke `PlotLayoutsToPdf` for the active layout, own its temporary directory for the preview window lifetime, and open the generated page through the existing PDF viewer.

## 3. Architectural boundary check

- New dependency, data format, output device, or layer: no.
- Resource ownership: the preview viewer will own the temporary preview directory for exactly its lifetime; this is a local extension of the existing PDF viewer's file ownership.
- Existing ownership remains: Qt routes the command, IO creates the PDF, `PdfWindow` displays it and owns the temporary file lifetime.

## 4. Questions and decisions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Which #810 slice should be implemented first? | 2026-11-04 | PDF Preview. |
| Q2 | Should Preview use GoSurvey's PDF viewer and avoid the export filename prompt? | 2026-11-04 | Yes; use the viewer. |
| Q3 | Should the preview be read-only? | 2026-11-04 | No; allow annotations and saving through the viewer. |
| Q4 | Are the proposed feature request and acceptance checks complete? | 2026-11-04 | Confirmed. |

## 5. Plan

- Approach: route the existing Preview ribbon item to an active-paper-layout-only action. Plot the active layout to a unique temporary PDF using `PlotLayoutsToPdf`; pass the temporary directory to the viewer as owned lifetime state; open only on success. Surface the plotter's error in the editor UI/log. Keep the PDF viewer's current annotations and Save As behavior.
- Files/functions to inspect and likely touch:
  - `src/qt/RibbonCatalog.hpp` — bind and enable the Output Preview item.
  - `src/qt/EditorRibbon.cpp` and, if ownership requires, `src/qt/EditorWindow.{hpp,cpp}` — route the action and open the viewer.
  - `src/qt/PdfWindow.{hpp,cpp}` — accept ownership of the generated preview directory and clean it up with the viewer.
  - `tests/QtEditorTests.cpp` — active layout, error, viewer, annotation/save-as, cleanup, and drawing immutability coverage.
  - `resources/wiki/Plotting.md`, `resources/wiki/Paper-Space-and-Layouts.md`, and the Output-tab help text — document Preview.
- Test approach:
  - Happy path: render an active layout with non-default saved page setup and plot style; assert the viewer loads one page and Save As/annotation controls are available, with no export dialog.
  - Failure: invalid/missing selected CTB/STB returns a plot error, opens no viewer, and leaves drawing/undo state unchanged.
  - Space guard: model space cannot preview a paper layout by accident.
  - Lifetime: temporary preview files exist while the viewer is open and are cleaned when it closes; Save As output survives independently.
  - Full plot geometry/style behavior remains covered by REQ-445/485 tests.
- Steps:
  - [x] Plan and architecture review approved before implementation.
  - [x] Implement the ribbon route, temporary plot ownership, and PDF viewer integration.
  - [x] Add focused UI happy/failure/lifetime and immutability coverage; update in-app help.
  - [x] Build and run focused tests; capture the Preview and viewer surfaces.
  - [x] Record Civil 3D comparison status and the remaining combined-PR documentation preflight.

## 6. Verification review

See `verification/reviews/TASK-810-pdf-preview-plan-review.md`.
Implementation self-review: `verification/reviews/TASK-810-pdf-preview-implementation-review.md`.

## 7. Implementation log

- 2026-11-04: The user confirmed the PDF Preview feature request, including the existing editable PDF viewer and its Save As workflow.
- 2026-11-04: SPEC added REQ-486 and decision D-2026-11-04-b. No architecture/data-format decision is needed; Verification approved the plan before implementation.
- 2026-11-04: Enabled and space-gated the Output ribbon Preview command. It writes the active layout through `PlotLayoutsToPdf` into a unique temporary directory and transfers that directory to the existing PDF viewer. The viewer keeps annotation and Save As available and removes the temporary files when it closes. Plot failures are shown with the plot log and do not open the viewer.
- 2026-11-04: Updated `resources/wiki/Plotting.md` with Preview and Save As instructions.
- 2026-11-04: Added Qt coverage for the ribbon route, one-page paper dimensions, annotation and Save As, unchanged layout/undo state, temporary-file lifetime, missing-table failure cleanup, and model-space refusal.
- 2026-11-04: `.\build.bat` passed (Release, both frontends and test/helpers). Focused hidden-desktop Qt run passed: 4 passed, 0 failed (`issue810PlotPreviewOpensEditablePdfAndKeepsDrawingUnchanged`, `issue810PlotPreviewReportsInvalidStyleAndRejectsModelSpace`; log `build/artifacts/qt/issue810-preview-focused-final.log`). An earlier runner invocation bound the test filter to the wrong positional parameter and timed out in unrelated update tests; the corrected invocation ran only the two named tests and passed.
- 2026-11-04: `ctest --preset ninja-release -R "^unit[.]catch[.]shard-"` passed both shards (2/2, 0 failed; 95.94 seconds).
- 2026-11-04: Screenshots: `build/artifacts/Release/qt-810-output-preview-paper-layout.png`, `qt-810-pdf-preview-viewer.png`, and `qt-810-preview-plot-error.png`. Civil 3D is not installed in this environment, so the required product comparison is inconclusive; no visual pass is claimed.
- 2026-11-04: `git diff --check` reported no whitespace errors; Git emitted only the repository's LF-to-CRLF working-copy notices. No new dependency or Workshop architectural decision was introduced. README and PRIVACY are not affected by this workflow; public manual wiki review/update remains part of the combined PR preflight.

## 8. Outcome

REQ-486 implementation is complete and its focused tests pass. The Civil 3D visual comparison remains inconclusive because Civil 3D is unavailable on this machine. The feature is ready for the shared paper-space PR after the combined pre-PR documentation review, including the public manual wiki update, is completed.
