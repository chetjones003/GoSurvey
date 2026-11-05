# TASK — Paper-space issue batch PR

- Type: combined feature and DXF interoperability batch
- Status: verification passed; ready to publish the PR
- Branch: `feat/paper-space-issues-batch`
- Base: `beta`

## Included work

- #780 — DXF block definitions and model/paper INSERTs.
- #781 — DXF paper-space entity import and unsupported-type reporting.
- #1005 — paper-space ARRAY and active-layout OVERKILL.
- #1007 — viewport on/off, display lock, hide-in-plots, and DWG/DXF persistence.
- #1009 — accepted PDF-only CTB/STB plot-style support.
- #1015 — duplicate a layout.
- #1016 — MVIEW Fit, 2/3/4 arrangements, and closed-polyline Object viewport clipping.
- #1017 — named scale dropdown and `VPMATCHSCALE`.
- #810 — active-layout PDF Preview, View Details, and current-layout PDF Export slices.
- Slash-command discovery fix for `.agents/skills/implement-issue`.

## Excluded / still open

- #1000 is not included: its view-frame prerequisite (#999) is blocked by #121, and the repository
  has no view-frame model or workflow yet.
- #810 remains open because its other Output controls have not been specified or implemented.
- #1016 is implemented and linked in this PR. GoSurvey render/plot tests and screenshots, plus
  AutoCAD Core Console DWG audits and DWG-to-DXF clip-boundary round-trip, pass. Civil 3D is not
  installed, so the paired product comparison is inconclusive; no product-parity pass is claimed.

## Product documentation pre-PR review — 2026-11-04

- `README.md`: reviewed; no paper-space-specific instructions or claims need changing.
- `PRIVACY.md`: reviewed; unrelated to these drawing and plotting features.
- `resources/wiki/`: updated Paper Space and Layouts, Plotting, ARRAY, OVERKILL, and MVIEW help.
- Public GoSurvey manual wiki: reviewed `Paper-Space-and-Layouts.md`, `Plotting.md`, and
  `Import-and-Export.md`; updated these pages for layout duplication, paper viewport controls and
  MVIEW, scale matching, PDF preview/export and plot styles, plus DXF blocks and INSERTs. Published
  commits `0980578650781c39dd74161ff20268d278b42d1f` and
  `579cbc5b5cd4a04e6be258e691985d702a2a5fd4`.

## PR evidence

- GoSurvey screenshots attached to PR #1159 with `gh --attach`:
  - `qt-1007-viewport-controls.png`
  - `qt-1015-layout-duplicate-context-menu.png`
  - `qt-1017-viewport-scale-dropdown.png`
  - `req480-paper-viewport-clipping.png`
  - `qt-897-page-setup-editor.png` (CTB/STB selection)
  - `qt-810-output-preview-paper-layout.png`
  - `qt-810-pdf-preview-viewer.png`
  - `qt-810-preview-plot-error.png`
  - `qt-810-output-view-details.png`
  - `qt-810-view-details-dialog.png`
  - `qt-810-current-layout-pdf-export-dialog.png`
  - `qt-810-current-layout-pdf-export.png`
- Screenshots are test evidence only; no Civil 3D screenshots exist in this environment.

## Verification

- Focused checks and per-feature verification are recorded in the linked `TASK-*.md` files.
- `.\build.bat`: PASS on the merged current-`beta` tree (Release; both frontends and all test/helpers).
- `ctest --preset ninja-release --output-on-failure`: PASS, 194/194.
- `git diff --check`: PASS after the merge and again before push.
- Issues #780, #781, #1005, #1007, #1009, #1015, #1016, and #1017 are closed. #810 remains open for its remaining Output controls. #1016 is closed with the AutoCAD UI and Civil 3D visual verification limitations disclosed in the PR.
- PR: https://github.com/chetjones003/GoSurvey/pull/1159 (open against `beta`). The final head includes the SPEC ID collision corrections; see GitHub for its current SHA.

## Follow-up review findings — 2026-11-05

- Fixed normalized blue-channel packing for model viewport RGB, legacy no-table RGB, and paper filled-region RGB in `PdfPlot.cpp`. Added PDFium assertions for blue model strokes and filled regions.
- PDF plotting now tessellates any stored closed clip boundary with at least two vertices, then uses the tessellated polygon only when it has nonzero area. Added measured clipping coverage for a two-vertex, two-semicircle circle outline (REQ-490).
- Fixed the subsequent PR review finding in the GoSurvey document JSON loader: it now retains finite clip boundaries with at least two vertices only when the bulge-tessellated closed outline has nonzero area. Added a save/reopen regression for the two-arc circle and a degenerate two-line refusal case.
- The reviewer proposed clipping PDF text glyphs to polygonal viewports. This conflicts with the user-approved REQ-490 behavior: keep PDF text searchable, omit labels whose insertion anchor is outside, and allow glyphs from inside anchors to extend beyond the edge. Preserve the specified behavior; no text clipping change is made.
- Verification: `.\build.bat` PASS on this follow-up; `[payload][req490]` PASS (13 assertions / 1 case) for valid two-arc boundary retention and degenerate boundary clearing. PDF-tagged GoSurvey tests PASS (134 assertions / 22 cases) on the prior follow-up; full `ctest --preset ninja-release --output-on-failure` PASS (194/194) before these isolated loader changes.
