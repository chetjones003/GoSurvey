# TASK-908 — Qt PDF compare and audit workflows

- Type: feature
- Status: implemented — visual approved
- Opened: 2026-10-19
- Owner: Workshop
- GitHub: #908, child of #878

## 1. Authority
- Goal: Qt migration to beta 0.7.4 parity (`test/win32UI` migration task).
- Requirements: REQ-392 (revision overlay and alignment), REQ-393 (change detection and markups),
  REQ-395 (automatic scale audit), REQ-387/388 (PDF viewer/session and Save As), REQ-201 (honest failure).
- Constraints: REQ-300 (no new dependency), REQ-301 (no one-use abstraction), architecture layer boundaries.
- Issue acceptance: preserve beta 0.7.4 cancel, validation, failure, undo, and read-only behavior where applicable;
  complete Qt workflows; update the parity ledger and add focused workflow coverage; present 2–3 integrated
  screenshot options for each major affected surface and apply the selected direction.
- Owning subsystem: Qt PDF presentation over shared `src/pdf/` compare, diff, and audit services.

## 2. Scope
- In scope: inspect existing 0.7.4 compare and audit workflows; port fully usable Qt compare/alignment/display,
  automatic change detection/review/markup save, and automatic scale audit workflows; preserve failure and
  cancellation behavior; add focused Qt coverage and update the parity ledger/migration inventory.
- Out of scope: changing ImGui behavior, changing shared algorithms or PDF formats, OCR, and features not
  present in the 0.7.4 workflows or accepted REQ-392/393/395.

## 3. Architectural boundary check
- Reuse existing `pdfview::` services for alignment, diff, scale audit, rendering, and annotation sessions.
- Qt owns workflow presentation and user input only. Route saved markups through the existing Save As/session
  path. No new dependency, layer, format, or shared algorithm.

## 4. Plan
1. Inventory beta 0.7.4 ImGui routes, shared service APIs, and current Qt PDF window; record parity gaps.
2. Implement the smallest Qt workflows matching REQ-392/393/395 and 0.7.4 cancel/failure semantics.
3. Add focused Qt tests for completed workflows and failure/cancel/read-only/undo behavior where applicable.
4. Update `docs/qt-074-parity-ledger.md` and `docs/qt-ui-migration.md` with implemented routes and evidence.
5. Build and run relevant Qt verification; capture integrated screenshot options for major affected surfaces and
   wait for the user's selected direction before finalizing.

## 5. Plan review
- Accepted requirements define the comparison, alignment, change list/markup, and scale-audit behavior. REQ-430
  explicitly assigned Qt compare and Audit scale to issue #908. No behavior-changing specification gap found.
- Architecture check: UI-only port over existing shared PDF services; no architecture decision required.

## 6. Implementation and verification
- Inventory: Qt `PdfWindow` currently exposes no compare or audit action. The shared PDF layer has `PdfAlign`,
  `PdfDiff`, `PdfDimAudit`, and `PdfDocument::AuditPageData`; the ImGui reference workflow is in `ui/PdfCompare`
  and `ui/PdfViewerWindow.cpp`. The Qt window already owns rendering, annotations, and page scale state.
- Added the Qt Compare revisions dialog with base/revision page selection, background rendering and automatic
  alignment, one- and two-point manual alignment, Tint / Opacity / Base / Revision views, confidence warnings,
  configurable diff tolerance/minimum size, cancellable change detection, region navigation/highlights, and
  Save As markups to a new copy of the revision. Compare work is bounded to 4M pixels per page pair.
- Added the Qt Audit scale dialog with background read/match, cancellation without partial results, verdict and
  consensus summary, offender/all-dimension table, on-sheet selected dimension, and undoable consensus apply.
- Selected visual direction: deep navy, approved by the user on 2026-10-19. Both workflow dialogs now use
  the PDF viewer's navy surfaces, blue selection states, readable table headers and progress indicators.
- `.\build.bat qt` passes after applying the selected style. Focused tests pass individually:
  `pdfScaleAuditShowsMatchesAndAppliesConsensusAsOneUndoableChange`,
  `pdfCompareRunsDiffAndSupportsManualAlignmentAndFailure`, and
  `qtPdfCompareAndAuditDeepNavyScreenshotsForIssue908`.
- `.\\build.bat qt` passes. Full hidden-desktop `ctest --preset ninja-release -R "^qt[.]editor$"` still crashes in
  the existing `approvedTopographyStaysInPanelBackgrounds` screenshot test (`QWidget::grab`), also recorded in
  TASK-907. The Qt test harness then reports a PowerShell signed-int conversion error for the crash exit code.
- Three integrated visual directions were presented for both dialog surfaces; deep navy was selected and
  the final integrated screenshots were approved. Captures are at
  `build/artifacts/Release/qt-908-audit-deep-navy.png` and
  `build/artifacts/Release/qt-908-compare-deep-navy.png`.
