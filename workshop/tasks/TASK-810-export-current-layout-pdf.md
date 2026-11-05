# TASK-810 — Export current paper layout to PDF

- Type: feature
- Status: done (Current-layout PDF Export slice; #810 remains open for other controls)
- Opened: 2026-11-04
- GitHub: #810 (Current-layout PDF Export slice)
- Branch: `chore/triage-paper-space-issues` (shared paper-space batch)

## 1. Authority

- Goal: M-PaperSpace outcome; no numbered GOAL covers this Output-tab action.
- Requirements: REQ-488 (accepted), REQ-029, REQ-445, REQ-485.
- Constraints: CON-06 (smallest sufficient scope), REQ-300 (dependency discipline).
- Acceptance: REQ-488 Acceptance 1–5.
- Owning subsystem: Qt UI invokes existing IO plotter; no drawing-data ownership changes.

## 2. Scope

- In scope: enable Output ▸ Export (DWF/PDF group) for active paper layouts; open a PDF filename dialog; generate that layout using its saved Page Setup and selected CTB/STB through `PlotLayoutsToPdf`; stage and publish the completed PDF so failures do not leave a partial final file; remain available to read-only drawings.
- Out of scope: DWF, Display-area export, Plotter Manager/device configuration, changes to Display/Current controls, new plot algorithms, and changes to Plot or Batch Plot routes.
- Smallest change: bind the existing Export action, gate it to active paper layouts, stage the plot in a temporary file, and publish it to the chosen path only after successful plotting.

## 3. Architectural boundary check

- New abstraction, layer, dependency, data format, public API, ownership change, global state, or plot algorithm: **No**. Qt owns the action and filename selection; the existing plotter owns PDF generation; a temporary file plus atomic Qt file commit protects the selected destination.

## 4. Decision

- D-2026-11-04-h / REQ-488: user selected PDF Export for the Current layout and confirmed the complete feature request on 2026-11-04.

## 5. Plan

- Approach: route the existing `##OutDwfxExport` action to a Current-layout PDF save flow. Validate the active layout at invocation; stage `PlotLayoutsToPdf` output in a unique temporary file; copy it to a `QSaveFile` for the selected destination and commit only after the complete staged PDF is available. Keep plot failure logs and add a visible error where the Export action otherwise has no command-line feedback.
- Files/functions:
  - `spec/requirements.md`, `spec/project.md` — accepted REQ-488 and D-2026-11-04-h.
  - `src/qt/RibbonCatalog.hpp`, `src/qt/EditorRibbon.cpp` — bind the action, gate it, and run the PDF export flow.
  - `tests/QtEditorTests.cpp` — export, cancellation, failure/no partial output, read-only operation, and model/floating-space guards.
  - `resources/wiki/Plotting.md` — document the new Output-tab entry point.
- Test approach:
  - Happy path: choose a path from a paper layout; verify a readable one-page vector PDF at the active sheet dimensions, unchanged layout and undo state, and saved CTB/STB path honored by the plotter.
  - Failure/edge paths: cancel creates no file; invalid selected CTB/STB reports failure and does not publish a PDF; model and floating model space disable the action; confirm read-only drawing is allowed.
  - Regression: existing Plot and Batch Plot tests remain unchanged and pass as applicable.
- Steps:
  - [x] Confirm feature request and record accepted requirement/decision.
  - [x] Prepare implementation plan and review it before code.
  - [x] Implement Output action and safe destination publication.
  - [x] Add/run focused tests, build, capture UI evidence, and update help.

## 6. Verification review

See `verification/reviews/TASK-810-export-current-layout-pdf-plan-review.md`.

## 7. Implementation log

- 2026-11-04: User selected the Current-layout PDF Export slice and confirmed the complete feature request. Repository inspection found the existing Plot action calls `PlotLayoutsToPdf` directly; the PDF writer can leave a partial file if the final write itself fails. This implementation will stage output and publish only on success, leaving Plot and Batch Plot routes unchanged.
- 2026-11-04: Autodesk/AutoCAD/Civil 3D evidence was already recorded in `TASK-810-output-tab-triage.md`: AutoCAD Core Console did not settle UI semantics, Civil 3D is unavailable, and the repository has no DWF or PC3 backend. Those controls remain out of scope under D-2026-11-04-h.

## 8. Outcome

### Completion report — 2026-11-04

- Requirements satisfied: REQ-488 Acceptance 1–5. Export uses only the active paper layout, existing PDF plotter, saved setup/style table, and an atomically committed final destination. Model and floating model space are gated; read-only export works.
- Files changed for this slice: `spec/requirements.md`, `spec/project.md`, `src/qt/RibbonCatalog.hpp`, `src/qt/EditorRibbon.cpp`, `tests/QtEditorTests.cpp`, and `resources/wiki/Plotting.md`. Other paper-space batch changes were preserved.
- Tests: `issue810CurrentLayoutPdfExportWritesPdfWithoutChangingDrawing` and `issue810CurrentLayoutPdfExportFailureAndSpaceGuards` — **PASS**, 4 QtTest totals including init/cleanup, 0 failures. The tests cover valid one-page vector PDF dimensions, read-only export, unchanged layout and undo state, cancel and temp cleanup, invalid selected CTB failure with no final or staged file, error display, and model/floating guards.
- Existing plotter regression: `GoSurveyTests.exe "[plot]"` — **PASS**, 123 assertions in 20 test cases. Existing Plot and Batch Plot routes were not modified.
- Full Qt regression: `ctest --preset ninja-release -R "^qt[.]editor[.]shard-[0-9]+$" --output-on-failure` — **PASS**, 6/6 shards, 0 failures (443.07 seconds).
- Build: `.\build.bat` — **PASS**, Release build of both frontends and test/helper targets. The environment printed existing nonfatal notices for absent `vswhere.exe` and optional Vulkan headers.
- `git diff --check`: **PASS**; Git emitted working-copy LF-to-CRLF notices only.
- UI evidence: `build/artifacts/Release/qt-810-current-layout-pdf-export.png`, `build/artifacts/Release/qt-810-current-layout-pdf-export-dialog.png`; focused log `build/artifacts/qt-810-current-export.log`.
- Civil 3D comparison: **INCONCLUSIVE** because Civil 3D is unavailable on this machine. No product-parity pass is claimed.
- Self-review: the Output action is a Qt UI route to existing IO plotting. `QTemporaryFile` and `QSaveFile` ensure failed plotting/publication leaves no partial final file and preserves an existing destination. No output algorithm, plotter backend, dependency, persistent state, public API, or layer was added.
- Assumptions: none.
- Architectural decisions made by Workshop: none; D-2026-11-04-h is the accepted authority.
- Technical debt: none identified in this slice. DWF, Display-area export, Plotter Manager/device configuration, and Display/Current controls remain explicitly excluded; #810 stays open for the other controls.
- 2026-11-05 CI follow-up: MSVC `/analyze` flagged `RunRibbonAction` for 65,976 bytes of stack use due to its local 64 KiB PDF transfer buffer (C6262). The buffer now uses heap-backed `QByteArray`, preserving chunked publication while reducing handler stack use. Release build passed after the change; the PR analysis job will rerun on the pushed commit.
- Product docs: updated the in-app Plotting guide. README and PRIVACY have no affected content. The public manual update and remaining documentation review are recorded in `TASK-paper-space-issue-batch-pr.md`.

## 9. Self-verification

- [x] build-project — Release build passed.
- [x] architecture-review — PASS; Qt action remains in its owning layer and uses existing IO API.
- [x] code-review — PASS; validates active layout, reports errors, cleans staging through RAII, and commits via QSaveFile.
- [x] dependency-audit — PASS; no dependency added.
- [x] performance-review — N/A beyond chunked 64 KiB copy; no drawing-scale computation or hot render path changed.
- [x] testing — PASS; focused Qt and plotter suites recorded above, plus all six Qt editor shards passed.
