# TASK — Paper-space issue batch PR

- Type: combined feature and DXF interoperability batch
- Status: preparing PR; final build and full suite pending
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
- #1016 remains open pending the AutoCAD UI visible-clipping check. AutoCAD Core Console audited
  the DWG data and round-tripped the DXF clip reference, and GoSurvey's render/plot tests pass;
  Civil 3D is not installed, so the paired product comparison is inconclusive.

## Product documentation pre-PR review — 2026-11-04

- `README.md`: reviewed; no paper-space-specific instructions or claims need changing.
- `PRIVACY.md`: reviewed; unrelated to these drawing and plotting features.
- `resources/wiki/`: updated Paper Space and Layouts, Plotting, ARRAY, OVERKILL, and MVIEW help.
- Public GoSurvey manual wiki: reviewed `Paper-Space-and-Layouts.md`, `Plotting.md`, and
  `Import-and-Export.md`; updated these pages for layout duplication, paper viewport controls and
  MVIEW, scale matching, PDF preview/export and plot styles, plus DXF blocks and INSERTs. Published
  wiki commit `0980578650781c39dd74161ff20268d278b42d1f`.

## PR evidence

- GoSurvey screenshots to attach with `gh pr create --attach`:
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
- Final current-`beta` Release build and CTest result: pending.
- `git diff --check`: previously passed before PR assembly; rerun after base update.
- PR review state, issue close state, branch SHA, and final acceptance matrix: pending PR creation.
