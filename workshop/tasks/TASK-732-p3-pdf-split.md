# TASK-732-p3 - Split a PDF: save chosen pages as a new PDF (REQ-389, ADR-067, D-2026-10-06-a)

- Status: submitted
- Requirement authority: REQ-389 (accepted); depends on REQ-387 (merged, PR #734/#736). Constraints: REQ-201 (honest messages), REQ-300 (no new dependency - PDFium only), CLAUDE.md section 7 (no abstraction without two uses).
- Scope: `src/pdf/PdfSplit.{hpp,cpp}` (pure: `ParsePageList`, `SplitPdf`); a **Split...** toolbar button and modal dialog in the viewer window; typed command `PDFSPLIT`; tests `[pdfsplit][req389][issue732]`.
- Approach: page list parsed with a small hand-written parser (no regex). The source is read into memory and closed, pages are imported one at a time with `FPDF_ImportPagesByIndex` (keeps size, content, annotations; one at a time so progress can be shown), saved with `FPDF_SaveAsCopy` to `<dest>.gssplit.tmp` and renamed into place. The copy runs on a one-shot `std::async` worker; the dialog shows a progress bar.
- Architectural-boundary check: Domain/IO module is window-free and takes the shared `PdfiumMutex()`; UI only owns the dialog; Commands only sets a request flag (same pattern as `PDFVIEW`).
- Assumptions: while a split runs it holds the PDFium lock, so page renders in any viewer wait for it (the UI does not). `PDFSPLIT` targets the viewer that last had focus (or the last open one).
- Verified: unit tests (page list accept/refuse cases, 15-page output order/size/text, source bytes unchanged, replace-source refused, no partial or temporary file left). Not verified by a person: the dialog and the Save As window.
