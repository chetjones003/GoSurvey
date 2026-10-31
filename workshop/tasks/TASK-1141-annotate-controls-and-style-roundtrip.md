# TASK-1141-annotate-controls-and-style-roundtrip

## 1. Authority and scope

- User-confirmed bug report covers six defects: dimension live preview; MSTY editor access; Centerlines dropdown actions; TABLE prompt/state/rendering/selection/DWG behavior; CREATEPOINTS survey-point snapping; ADDPOINTTABLE dialog fidelity.
- User additionally directed a drawing-wide DIMSTY manager and modify/create dialog matching supplied AutoCAD screenshots.
- User superseded D-2026-10-29-d item 6 (one dimension style) and directed named drawing-owned dimension, text, multileader, table, and survey-point styles to persist in DWG and round-trip with AutoCAD. For point styles, user selected Civil 3D native style objects as the target.
- Existing requirements: REQ-459, REQ-464/465, REQ-466, REQ-474/475/476, REQ-044, REQ-367, REQ-478 (accepted or amended for this scope).
- Constraints: REQ-201 refusal behavior; REQ-101 CAD fidelity; CON-06 smallest sufficient implementation; no change to master/beta directly.

## 2. Files/subsystems affected

- Requirements and decision log for named style tables and DWG/Civil 3D persistence.
- Commands: dimensions, DIMSTY, MSTY, TABLE, CENTERMARK/CENTERLINE, CREATEPOINTS, ADDPOINTTABLE.
- Qt: dimension and multileader style dialogs, centerline ribbon dropdown, point table dialog, viewport dynamic previews and selection.
- Domain style records and DWG IO (DIMSTYLE, STYLE, MLEADERSTYLE, TABLESTYLE and Civil 3D point styles).
- Regression tests for each command/UI/render/IO defect.

## 3. Root causes and implementation evidence

- DIMLINEAR: the aligned/linear/angular draft builders were not called from the viewport preview painter. Draft geometry and dimension text are now drawn in the painter; domain regression `[preview]` covers the new strokes.
- MSTY: dispatch set a dialog flag, but the command alias was absent from `kRegistry`, so the text parser treated `MSTY` as unknown. The missing registry entry and UI consumer are now present. A Qt regression test invokes `MSTY` and captures the dialog.
- Centerlines: both ribbon dropdown commands were empty/disabled. They now dispatch to selection-based CENTERMARK/CENTERLINE implementations and have enabled menu items.
- TABLE: entering a new interactive draw command did not clear MLEADER's in-place text editor state. `BeginInteractive` now resets that state. `cadTables` were pickable but not painted in model-space; the viewport now draws, highlights, and previews a TABLE. TABLE insertion remains exported as line/text geometry because this vendored LibreDWG build has no `dwg_add_TABLE` implementation.
- CREATEPOINTS: the modeless create-points overlay has `active == None`, so its cursor path was excluded from CAD object-snap activation. Its mouse-move snap eligibility now includes the active CREATEPOINTS overlay.
- ADDPOINTTABLE: the old dialog lacked the referenced layer and split controls. Those controls, group filtering, per-point selection and point-table formatting options are present; the style edit button now edits supported table dimensions.

## 4. Verified implementation

- `.gs` serialization now carries named dimension and table style collections; DWG DIMSTYLE export/import maps the fields GoSurvey currently supports. Dimension styles persist with their names and current-style name.
- Text styles already had drawing-owned STYLE persistence and native DWG STYLE import/export before this task.
- Full Release build succeeds. Full Release CTest passes: 194/194 tests, including all six hidden-desktop Qt shards, DWG/command tests, and `build.output-layout` (527.99 seconds).
- Qt capture test `issue804AnnotateDialogsAndTablePreviewScreenshots` passes as part of the full suite and writes labeled screenshots under `build/artifacts/qt-editor/manual/shard-5/artifacts/`.

## 5. Remaining interop gaps (not represented as completed)

- The table entity exports as visible/editable lines and MTEXT, not an AutoCAD native TABLE object. TABLESTYLE collection is stored in `.gs`; native TABLESTYLE export/import is not implemented by the vendored LibreDWG API.
- MLEADER style editing currently edits one active style. The named MLEADERSTYLE collection and native style-record import/export are not implemented; the entity writer still relies on LibreDWG's Standard record.
- Survey label templates and per-point label enums persist in GoSurvey formats, but Civil 3D AECC point/point-label style objects and their native DWG round-trip are not implemented. Plain AutoCAD proxy display is not a substitute for the user-selected Civil 3D target.
- DIMSTY manager/editor has the requested manager and seven-tab organization, but Override/Compare are informational and Fit/Alternate Units/Tolerances fields are not implemented. Unsupported fields are not represented as completed.
- The full Qt/Release suite is green. TABLE insertion still uses visible editable line/MTEXT fallback in DWG; its live in-app table, selection, and placement preview are covered by Qt capture and full-suite tests.

These gaps mean the six reported regressions have code fixes, but the broader REQ-478 DWG/Civil 3D acceptance criteria remain incomplete and must not be reported as fully satisfied.

## 6. Product documentation review

- `README.md` — reviewed; no change needed because installation, privacy, and general product description are unaffected.
- `PRIVACY.md` — reviewed; no new data collection or transmission was added.
- `resources/wiki/` — updated the TABLE command, DIMSTY, ADDPOINTTABLE and annotation pages to describe the new UI and remaining DWG/Civil 3D limits.
- Public GoSurvey GitHub Wiki — updated `Annotation`, `Command-Reference`, `Import-and-Export` and `Known-Limitations`; published as wiki commit `6c12dab`.
- `site/about.html` — reviewed by inventory; it is the standalone About page and has no command or manual content to change.

## 7. Architectural boundary

Style collections affect drawing domain state, undo, serialization, and DWG translation. Keep ownership in drawing state and format mapping in IO; do not add a second UI-owned source of truth. Survey point style encoding is Civil 3D-specific and must remain isolated behind the DWG translation boundary.
