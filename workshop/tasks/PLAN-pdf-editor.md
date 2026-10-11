# PLAN — Built-in PDF viewer / editor (not started)

**Status:** PLANNING ONLY. No accepted REQ exists, so this is a **SPEC GAP**: nothing here may be coded until
the user accepts a REQ, its Acceptance Criteria and an ADR for the PDF library decision.

## Why
In the Project tab, double-clicking a PDF currently opens it in the machine's default PDF viewer. The user
wants GoSurvey to open and edit project PDFs itself.

## Interim behaviour (shipped with the Project tab)
Project Files rows act like a file explorer: double-click a `.dwg` opens it in a tab; double-click a `.pdf`
opens it with the system default viewer; right-click offers Open, Open Containing Folder, Copy Full Path.

## Existing assets
- `pdfium.dll` already ships beside the executable (PDF *underlay* rendering) — a rendering engine exists.
- PDF attach / placement records already live in the project (`ProjectFiles`, associations).

## Questions the SPEC must answer (one at a time, with a recommendation)
1. Scope of "editor": view only, markup (highlight/text/line/measure), page operations (rotate/reorder/split),
   or true content editing? Recommended first slice: **viewer + markup**.
2. Where do markups live: inside the PDF (standard annotations) or in a sidecar in the project?
   Recommended: standard PDF annotations, so other readers show them.
3. Library: extend PDFium (already in tree; annotation API is limited) vs. adding a second library. Needs an ADR.
4. Presentation: a document tab beside drawing tabs, or a dockable panel?
5. Turnover / pack interaction: a marked-up PDF changes the file's hash — does the turnover record the saved version?

## Proposed phases (after the REQ is accepted)
- P1 PDF tab: render pages, zoom/pan, page navigation, open from the Project tab double-click.
- P2 Markup tools: highlight, text note, line/arrow, rectangle, save as PDF annotations.
- P3 Measure on a scaled PDF sheet (reuses drawing scale conventions).
- P4 Page operations.

## Architectural-boundary check
UI tab + a PDF service in `io/`; no change to the drawing model. Needs the dependency audit if a library is added.
