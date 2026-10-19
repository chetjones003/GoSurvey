# TASK-906 — Qt PDF calibration, scale checks and snapping

- Type: feature
- Status: verify
- Opened: 2026-10-19
- Owner: Workshop
- GitHub: #906, child of #878

## 1. Authority
- Goal: Qt migration to 0.7.4 parity (`test/win32UI` migration task).
- Requirements: REQ-430 (new, accepted by D-2026-10-19-b), REQ-390 (scale), REQ-391 clause 5 (snap),
  REQ-394 (scale check), REQ-201 (honest failure), REQ-388 (Save As, session undo).
- Constraints: REQ-300 (no new dependency), REQ-301 (no abstraction without two uses).
- Acceptance: see REQ-430 Acceptance.
- Owning subsystem: Qt presentation (`src/qt/PdfWindow`, `src/qt/PdfScaleUi`) over the shared `src/pdf/`.

## 2. Scope
- In scope: Set scale dialog (preset / typed ratio / page scope / clear), Calibrate tool + Known distance
  box, Check tool + popup, Scale checks dialog (checks, best fit, corrections, report), robust calibration,
  scale display, Snap toggle (F3) + marker for the Qt click tools, on-sheet check drawing, tests, ledger.
- Out of scope: coordinate extraction (#907), compare / Audit scale (#908), dimension handles and the
  Length third click (#910), any change to the ImGui viewer or to REQ-390/391/394.

## 3. Architectural boundary check
- [x] All maths, parsing and verdicts are the existing `pdfview::` services (`PdfMeasure`, `PdfScaleCheck`,
  `PdfSnap`, `PdfAnnotate::AnnotSession`, `PdfDocument::SnapPoints`). The Qt layer presents them only.
- [x] No new dependency, layer, global state or data format.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| 1 | Record REQ-430 and set the scope (Length third click, Audit, extraction, handles excluded)? | 2026-10-19 | User: "Yes, REQ-430 with that scope". |

## 5. Assumptions
- Qt has no Select tool or grips, so "snap while dragging a grip" has nothing to apply to until #910.
- The window logs scale changes through `qInfo()` (the ImGui viewer's in-app log has no Qt equivalent).

## 6. Plan
- `PdfWindow` keeps the file's `ScaleRead`, resolves the effective scale of a page like the ImGui viewer, and
  builds the saved `inForce` map from dimension pages only.
- New `PdfScaleUi.cpp` holds the dialogs and the window's scale / check / snap members.
- Snap: one background `SnapPoints` read per page (at most four kept), `SnapIndex::Nearest` on mouse move
  with the ImGui hysteresis; canvas gets mouse tracking and a marker.
- Tests: `GoSurveyQtTests` for every REQ-430 acceptance bullet.
- Verification: build, tests, 2–3 screenshot options per surface, the user's selection applied.

## 7. Implementation log
- 2026-10-19 — Spec: REQ-430, D-2026-10-19-b.
- 2026-10-19 — Question 2: F3 was claimed by both REQ-429 (find next) and REQ-391 (Snap); the user chose "F3 = Snap everywhere". REQ-429 clause 2 amended, find stays on Enter / Shift+Enter.
- 2026-10-19 — Implemented `PdfScaleUi.cpp` (dialogs, snap, overlay), `PdfCanvas.hpp` (shared canvas), scale members in `PdfWindow`; six `pdf*` Qt workflow tests pass. Found and fixed: radio buttons sharing a dialog parent excluded each other (mode vs scope) - now in button groups.
- 2026-10-19 — Three screenshot options (A app theme, B deep navy, C navy banner) shown; user selected B for all surfaces; option switch removed, final evidence test `qtPdfScaleScreenshotEvidenceForIssue906` writes `build/artifacts/Release/qt-906-final-*.png`.
