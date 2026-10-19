# TASK-905 — Qt PDF thumbnail strip and text search

- Type: feature
- Status: implement
- Opened: 2026-10-18
- Owner: Workshop
- GitHub: #905, child of #878

## 1. Authority
- Goal: Qt migration to 0.7.4 parity (`test/win32UI` migration task, scope reaffirmed 2026-10-18).
- Requirements: REQ-428 (new, accepted by D-2026-10-18-e), REQ-387 clause 2 (thumbnail strip),
  REQ-201 (honest failure), REQ-388 (annotation session untouched).
- Constraints: REQ-300 (PDFium already in tree; no new dependency), REQ-301 (no one-use abstraction).
- Acceptance: see REQ-428 Acceptance.
- Owning subsystem: Qt presentation (`src/qt/PdfWindow`) over shared `pdfview::PdfDocument`.

## 2. Scope
- In scope: Qt thumbnail strip (dock, virtualised, off-thread, bounded cache, follows page);
  Qt find bar; `PdfDocument::SearchText`; tests; parity ledger; migration doc.
- Out of scope: changing the ImGui viewer, annotations, calibration/snapping (#906+), a new dependency.

## 3. Architectural boundary check
- [x] Search is a shared PDFium operation next to `TextBoxes`/`SnapPoints` (same lock rule); the
  Qt layer only presents it. No new layer, dependency, global state or data format.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| 1 | 0.7.4 has no PDF text search and REQ-387 cl. 6 excludes it; issue #905 asks for it. | 2026-10-18 | User: "Thumbnails + new search". |
| 2 | Approve the search design (find bar, messages, wrap, highlights, Qt only). | 2026-10-18 | User: approved as proposed. Recorded as REQ-428 / D-2026-10-18-e. |

## 5. Assumptions
- Thumbnail renders are drafts with a time budget, refined later for visible pages (as ImGui does).
- Search starts on Enter, not while typing, so a long document is not rescanned per keystroke.

## 6. Plan
- `PdfDocument::SearchText(u16 query, matchCase, cancel)` → hits {page, viewer-coordinate boxes}, flag for
  "no text layer", cap of 10,000 hits.
- `PdfWindow`: find bar (Ctrl+F, Enter/F3/Shift+F3, Esc), background search future, overlay highlights.
- `PdfWindow`: thumbnail dock with a lazy list model; one background render at a time, newest visible first.
- Tests: `[req428]` Catch2 for `SearchText`; `GoSurveyQtTests` for the find bar and the strip.
- Verification: build, tests, 2–3 screenshot options per surface, user selection applied.

## 7. Implementation log
- 2026-10-18 — Spec: REQ-428, D-2026-10-18-e, REQ-387 clause 6 note.
- 2026-10-18 — Implemented `PdfDocument::SearchText` (not `FindText`: `FindText` is a Windows macro),
  the Qt find bar and the thumbnail dock. Catch2 `[req428]` (52 assertions) and the Qt tests
  `pdfFindBar*`, `pdfThumbnail*` pass. Three integrated screenshot options captured under
  `build/artifacts/Release/qt-905-option-{a,b,c}.png`; waiting for the user's selection.
- 2026-10-19 — User selected screenshot option B (deep navy); baked into `PdfWindow` as `kPdfWindowStyle`.
  Options test replaced by `qtPdfSearchAndThumbnailScreenshotEvidenceForIssue905`.
