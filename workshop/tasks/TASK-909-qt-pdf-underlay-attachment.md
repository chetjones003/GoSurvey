# TASK-909 — Qt PDF underlay attachment

- Type: feature
- Status: implemented; visual approved; PR open
- Opened: 2026-10-19
- Owner: Workshop
- GitHub: #909, child of #878

## 1. Authority
- Goal: Qt migration to 0.7.4 parity.
- Requirements: REQ-433 (D-2026-10-19-e), REQ-434 (D-2026-10-19-f).
- Constraints: REQ-201 (honest failure), REQ-300 (no new dependency), REQ-301 (no abstraction without two uses), existing command/domain ownership.
- Issue acceptance: preserve cancellation, validation, failure, undo and read-only behavior; complete Qt usability; focused workflow coverage; update parity ledger; present 2–3 integrated screenshot options per affected surface and apply the user's selection.
- Behavior reference: existing 0.7.4 ImGui `PDFATTACH` dialog/command and shared project Copy / Link flow.

## 2. Scope
- In scope: Qt PDFATTACH dialog, preview/options, insertion-point workflow, project attach choice, errors/cancellation/read-only behavior, tests and parity ledger; app-wide Qt Dark appearance update to graphite with aqua and amber accents, including the drawing canvas ground/grid.
- Out of scope: changes to ImGui behavior, Classic light appearance, PDF raster engine, PDF dependency, PDF viewer features, general underlay properties.

## 3. Files / subsystems
- Inspect `src/commands/CadCommands.cpp`, `src/ui/CadUi.cpp`, `src/qt/EditorDialogs.cpp`, `src/qt/EditorWindow.*`, `src/qt/EditorProjectAttach.cpp`, `tests/QtEditorTests.cpp`, `tests/DevShellProjectTests.cpp`.
- Expected Qt changes: command-state servicing/dialog in `src/qt/`; register existing `PDFATTACH` route as needed.
- Verification evidence: `docs/qt-074-parity-ledger.md`, `build/artifacts/` (screenshots/logs only; not committed).

## 4. Architectural boundary check
- Keep PDF construction in existing PDF attachment services and project file resolution in existing project attach logic. Qt owns presentation and user choices only.
- Use the existing drawing command and undo snapshot path for placement. No new storage model, dependency, layer or one-use abstraction.

## 5. Plan review
- **Plan check: PASS.** `PDFATTACH` already owns page/document loading (`PdfDraftCache`), background rasterization (`StartPdfAttachBuild`), Copy / Link routing (`RequestProjectAttach`), viewport placement, and undo (`SubmitPdfAttachInsertPoint`). Qt's `ServiceUiRequests()` currently handles the project attach prompt but does not open a dialog for `pdfAttachDialogOpen`; adding Qt presentation here preserves service ownership and does not require a new abstraction or storage path.
- Qt should mirror the existing dialog's path, page selection, insertion coordinates/on-screen pick, scale, rotation, DPI and snap-line/circle/text controls. It must keep the command live through async build and return to the form on recoverable failure; Cancel/Esc must call the existing cancel cleanup.
- Verification will cover initial attach, page/options, both static placement and insertion-point pick, cancellation, outside-project Copy / Link / Cancel, invalid PDF/failure, read-only state, and undo. Project Copy/Link logic has existing shared coverage; Qt-specific tests will pin the dialog continuation.
- Use focused Qt tests and the hidden desktop runner; build Qt, and run relevant shared project/PDF cases if required. Screenshot options and selected final direction are required because the dialog is visible UI.
- Architectural review: no new layer, dependency, model or abstraction; Qt owns only the modal and connects existing state/service functions.

## 6. Verification record
- The shared async rasterization starter moved from `src/ui/PdfAttachDialog.cpp` into the command layer so both frontends can link it; rasterization and placement still use the existing domain functions.
- Implemented the Qt PDF attach form and continuation after project Copy / Link / Cancel. The form validates/opens PDFs, shows a page preview, exposes page/DPI/snap/placement settings, reports build failure, supports typed or viewport-picked placement, and reuses the shared undo commit.
- Added `issue909PdfUnderlayAttachPreviewCancelFailureAndUndo` and `issue909PdfUnderlayProjectCopyLinkAndCancelContinueCorrectly`.
- Qt Release build: PASS (`.\build.bat qt`). Focused hidden desktop workflows: PASS (7 passed across appearance switching, the #909 flows, PDF viewer and scale-check screenshots).
- Full `ctest --preset ninja-release -R "^qt[.]editor$"` reaches the existing `approvedTopographyStaysInPanelBackgrounds` `QWidget::grab` crash before later tests; the targeted workflows pass independently.
- Screenshot options A (app theme), B (deep navy/aqua), C (graphite/amber) are captured under `build/artifacts/Release/qt-909-option-*`. User selected C and expanded it to the whole Qt Dark appearance. Final captures cover the Start page, drawing workspace/canvas, PDF viewer and scale checks, PDF attach form, and project Copy / Link choice under `build/artifacts/Release/qt-432-graphite-start.png`, `qt-432-graphite-drawing.png`, `qt-432-graphite-pdf-viewer.png`, `qt-906-final-checks.png`, `qt-909-final-graphite-attach-dialog.png`, and `qt-909-final-graphite-project-choice.png`. Visual approval is pending.
