# TASK-907 — Qt PDF coordinate extraction

- Type: feature
- Status: implemented; awaiting visual approval
- Opened: 2026-10-19
- Owner: Workshop
- GitHub: #907, child of #878

## 1. Authority
- Goal: Qt migration to 0.7.4 parity, extending the existing PDF viewer capability.
- Requirements: REQ-399 (complete extraction behavior and acceptance), REQ-387 (PDF viewer and PDFium ownership), REQ-201 (visible validation/failure), REQ-023 (point ID conflicts), REQ-101 (coordinate fidelity).
- Constraints: REQ-300 (no new PDF dependency), REQ-301 (no one-use abstraction), architecture layer boundaries.
- Issue acceptance: preserve cancel, validation, failure, undo, and read-only semantics; complete Qt usability; focused coverage; update parity ledger. Present 2–3 integrated screenshot options per major affected surface and apply the selected direction.
- Owning subsystem: Qt PDF presentation over shared `pdfview::PdfDocument` and existing drawing command/services.

## 2. Scope
- In scope: Qt region selection, selectable-text extraction, candidate review and Accept/Deny, point/circle commit through existing semantics, focus/zoom after acceptance, failure and cancellation handling, Qt workflow tests, parity ledger and migration-doc updates, screenshot options.
- Out of scope: changing ImGui behavior, OCR, editing parsed candidates, multi-page extraction, new PDF engine/dependency.

## 3. Architectural boundary check
- Keep PDF text reading in the shared PDF document/parser services; Qt owns only page selection, preview, and review presentation. Route entity creation through existing command/domain paths so undo, ID conflicts, read-only rules, and coordinate storage remain consistent. No new layer or one-use abstraction.

## 4. Plan review
- Read the project governance and the relevant specification, existing ImGui REQ-399 implementation, Qt PDF window, Qt tests, and parity ledger.
- Confirm route, cancellation, error, conflict, undo, and read-only behavior against REQ-399 and existing shared command paths before implementing.
- Add focused Qt workflow coverage for accepted candidates, denied/cancelled candidates, empty/unparsed regions, conflicts, undo, and read-only behavior as applicable to the existing architecture.
- Build and run the applicable Qt verification workflow; capture 2–3 integrated screenshot directions for each affected surface, then apply the user's selected direction before final completion.
- Update `docs/qt-074-parity-ledger.md` and `docs/qt-ui-migration.md` with actual behavior and evidence.

## 5. Implementation and verification
- Plan check: REQ-399 fully specifies extraction inputs, parsing shapes, review-before-commit, accept results, numbering/conflicts, focus, and exclusions. REQ-387 owns the PDF viewer, and existing `PdfDocument` APIs provide PDFium access. This is a Qt presentation port with shared domain behavior; no SPEC GAP is identified.
- Added native Qt region selection and review UI. PDF text extraction and parsing reuse `PdfDocument::AuditPageData` and `ExtractCoordinates`; accepted candidates go to the editor for read-only checks, undoable point/circle creation, automatic point numbers, conflict choices, refresh and zoom.
- Added `pdfCoordinateReviewAcceptsAndDeniesThroughDrawingWorkflow`, `pdfCoordinateRegionCancelAndNoTextAreSafe`, and `pdfCoordinateCommitHonorsReadOnlyAndPointConflicts` (read-only refusal, overwrite, and undo).
- Qt Release build: PASS (`.\\build.bat qt`). All three focused issue tests passed in the hidden-desktop log. The full Qt test run crashes afterward in the existing `approvedTopographyStaysInPanelBackgrounds` screenshot test (`QWidget::grab`); overall Qt suite remains FAIL.
- User selected deep navy. Applied it to the review dialog. Captures: `build/artifacts/Release/qt-907-review-deep-navy.png`, `qt-907-region-selection.png`, and `qt-907-no-text-message.png`. Awaiting user visual approval before closing this task.
- Updated the 0.7.4 parity ledger and Qt migration inventory.
