# TASK-1141-annotate-controls-and-style-roundtrip

## 1. Authority and scope

- User-confirmed bug report covers six defects: dimension live preview; MSTY editor access; Centerlines dropdown actions; TABLE prompt/state/rendering/selection/DWG behavior; CREATEPOINTS survey-point snapping; ADDPOINTTABLE dialog fidelity.
- User additionally directed a drawing-wide DIMSTY manager and modify/create dialog matching supplied AutoCAD screenshots.
- User superseded D-2026-10-29-d item 6 (one dimension style) and directed named drawing-owned dimension, text, multileader, table, and survey-point styles to persist in DWG and round-trip with AutoCAD. For point styles, user selected Civil 3D native style objects as the target.
- Existing requirements: REQ-459, REQ-464/465, REQ-466, REQ-474/475/476, REQ-044, REQ-367, REQ-478, and REQ-479 (accepted or amended for this scope).
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
- The final Release build succeeds. Full Release CTest passes 194/194 (526.90 seconds), including the six hidden-desktop Qt shards, DWG/command tests, fuzz smoke, and `build.output-layout`.
- The six-defect group passes: 128 assertions in 23 `[issue804]` cases. The drawing style payload tests pass: 49 assertions in 6 `[styles]` cases; named/native MLEADERSTYLE tests pass: 49 assertions in 7 `[issue619]` cases; REQ-478 native style test passes: 11 assertions.
- Qt capture test `issue804AnnotateDialogsAndTablePreviewScreenshots` passes and writes labeled captures under `build/artifacts/qt-editor/manual/shard-5/artifacts/`, including `qt-804-dimlinear-live-preview.png`, `qt-804-dimstyle-manager.png`, `qt-804-mleader-style.png`, `qt-804-insert-table-dialog.png`, `qt-804-table-placement-preview.png`, and `qt-804-add-point-table.png`.

## 5. Remaining interop gaps (not represented as completed)

- The table entity exports as visible/editable lines and MTEXT, not an AutoCAD native TABLE object. TABLESTYLE collection is stored in `.gs`; native TABLESTYLE export/import is not implemented by the vendored LibreDWG API.
- MLEADERSTYLE now has a named drawing collection, imports/exports native named records, and new MULTILEADER entities reference the selected native style. Active-only legacy payloads are merged into that collection on save so load no longer replaces their edited values with Standard.
- DIMSTYLE active-only legacy payloads are likewise merged into the named collection on save. A regression test now checks both active-only paths.
- The optional Civil 3D host bridge now imports/exports native COGO points, TIN surfaces, and alignment plan geometry through Civil 3D's managed API. Point numbers, coordinates, elevations, raw descriptions, names, and matching style names round-trip when the target drawing already contains those styles. The bridge does not yet clone custom Civil 3D point, point-label, surface, alignment, or label-set style definitions into a target drawing; unmatched style names fall back to Civil 3D defaults. Alignment geometry is sampled by station at one drawing-unit intervals (capped at 10,000 intervals), so source curves become editable native alignment tangents with bounded tessellation error; vertical profiles, corridors, parcel networks, and the wider Civil 3D object catalog are not implemented.
- DIMSTY manager/editor has the requested manager and seven-tab organization, but Override/Compare are informational and Fit/Alternate Units/Tolerances fields are not implemented. Unsupported fields are not represented as completed.
- The six in-app defect workflows are covered by the passing release suite. TABLE insertion still exports visible lines and MTEXT instead of a native editable AutoCAD TABLE object; TABLESTYLE records are not written natively. Its live in-app table, selection, and placement preview are covered by Qt captures.
- Required paired Civil 3D UI comparison has not been completed. Civil 3D 2026 is installed, and the Core Console plus an isolated hidden GUI/COM session successfully opened a copy of the supplied drawing. The first managed API probe compiled and ran inside Civil 3D and returned 353 COGO points, one surface, and zero alignments. The required paired captures and three independent score sheets remain outstanding; the earlier “Civil 3D is not installed” note was incorrect.

### Supplied baseline DWG check (2026-10-30)

- The provided `Rapides Substation Model.dwg` opens in the GoSurvey headless DWG importer. The import log reports 566 layers, 65 polylines, 5 HATCH fills, and 1 paper layout with 2 viewports. A follow-up class-aware loss report identifies 7,464 `AECC_PARCEL`, 353 `AECC_COGO_POINT`, 1 `AECC_TIN_SURFACE`, 2 `AECC_GENERAL_SEGMENT_LABEL`, 11 `AECC_SURFACE_ELEVATION_LABEL`, and 50 `AECC_SURFACE_CONTOUR_LABEL_GROUP` records. No COGO points or Civil 3D styles are imported. A Civil 3D 2026 in-process .NET API probe successfully read the same drawing copy and reported 353 COGO points, one surface, and zero alignments. Probe output and baseline copy are under `build/artifacts/`. The baseline therefore fails the expanded REQ-479 object round-trip acceptance.
- Civil 3D 2026 is installed at `C:\Program Files\Autodesk\AutoCAD 2026\C3D`, with `AeccDbMgd.dll`, `AeccCogoMgd.dll`, and `Civil3D.swidtag`. REA identified `AeccDbMgd.dll` as targeting .NET 8. The machine also has .NET 10; compiling the bridge against the highest installed runtime caused `accoreconsole.exe` access violations in `coreclr.dll`. The bridge compiler now matches `acdbmgd.runtimeconfig.json` and builds against .NET 8. Using the supplied baseline's 353-point/one-surface payload, Core Console created 353 native COGO points and one native TIN surface in a Civil 3D drawing in 2.6 seconds at 0.26 GB peak working set. Reopening and exporting verified 353 points and 406 triangles. A separate synthetic three-vertex alignment was created natively, reopened, and exported with its plan coordinates; source curves are currently station-sampled into native tangent segments. These checks prove native points/surface/alignment integration, while style-definition cloning and broader Civil 3D classes remain incomplete.

These gaps mean the six reported regressions have code fixes and baseline-sized COGO/TIN plus synthetic alignment transfers have native Civil 3D evidence, while the broader REQ-478 DWG/Civil 3D acceptance criteria remain incomplete and must not be reported as fully satisfied.

## 6. Product documentation review

- `README.md` — reviewed; no change needed because installation, privacy, and general product description are unaffected.
- `PRIVACY.md` — reviewed; no new data collection or transmission was added.
- `resources/wiki/` — updated the TABLE command, DIMSTY, ADDPOINTTABLE and annotation pages to describe the new UI and remaining DWG/Civil 3D limits.
- Public GoSurvey GitHub Wiki — updated `Annotation`, `Command-Reference`, `Import-and-Export` and `Known-Limitations`; originally published as wiki commit `6c12dab`, with MLEADERSTYLE support clarified in follow-up wiki commit `814614d`.
- `site/about.html` — reviewed by inventory; it is the standalone About page and has no command or manual content to change.

## 7. Architectural boundary

Style collections affect drawing domain state, undo, serialization, and DWG translation. Keep ownership in drawing state and format mapping in IO; do not add a second UI-owned source of truth. Survey point style encoding is Civil 3D-specific and must remain isolated behind the DWG translation boundary.

## 8. Completion status

**Incomplete against the user-approved full scope.** The six reported UI/command defects are implemented. COGO point and TIN surface transfers have full baseline-sized Core Console evidence; a synthetic alignment plan-geometry transfer has a passing Core Console round trip. Native table/table-style round-trip, full AutoCAD DIMSTY fields, custom Civil 3D style-definition cloning, alignment curve/profile fidelity, and broad Civil 3D object coverage remain incomplete. Do not describe the broad DWG/Civil 3D round-trip requirement as satisfied until the remaining objects and styles are integrated and verified in AutoCAD/Civil 3D.

### Resaved point-table fixture (2026-10-31)

- Added the user's resaved 3,765,575-byte DWG as `samples/rapides-substation-civil3d-pointtables.dwg`,
  SHA-256 `9BB529D7AF9F71A7D500FFDF40AD794AA2272F365B1D9612D7CED2A9CD124413`. The original baseline
  remains at `samples/rapides-substation-civil3d.dwg`, SHA-256
  `D8DF451C16F1C595B8AD65D026B52D826837D67E845E05A0855529A0EF320635`.
- The new file contains 18 `AECC_POINT_TABLE` records, zero native AutoCAD `TABLE` entities, and three
  `TABLESTYLE` records. Current import recognizes 353 COGO points but skips the 18 point tables;
  their 332-byte Civil proxy previews use a different format from the already decoded point/TIN proxy
  graphics. A fixture test checks the 18 records and that they carry preview data.
- `build.bat debug` and `[civil3d]` pass (503 assertions in 6 cases) with the updated fixture present.
  Point tables and Civil 3D style definitions are still not mapped into editable GoSurvey objects;
  REQ-478/479 remain incomplete.

### Style modification follow-up (2026-11-01)

Authority: REQ-478 statements 2, 5, 7; user explicitly requests the supplied Text, Dimension,
Multileader and Table Modify dialogs and completion of their editable settings.

Diagnosis: EditorDialogs still constructs the old sparse forms. The interrupted StyleEditors.cpp
is not a CMake source and implements only dimensions. Newly added property maps have no serializer,
so accepting additional controls without wiring IO would silently discard settings.

Plan: finish the four copy-on-accept editors, preserve cancel semantics, replace the old forms,
serialize additional properties in the drawing payload, map equivalent native fields, and apply
supported appearance properties to regeneration. Reuse existing drawing-owned styles and undo flow;
no new dependency or Autodesk runtime requirement. Verify with modal dialog interaction tests,
style-payload/native-DWG regressions, Release build and suite, and paired UI captures/panel.

Plan self-review: APPROVE within existing REQ-478 architecture. Saving properties alone does not
establish rendering/native interoperability; these paths require separate evidence before completion.

#### Follow-up verification evidence (2026-11-01)

- First Release builds of the four editors succeeded. Modal accept/cancel and all seven DIMSTY tabs
  passed (`style-latest-qt.log`, 4 Qt cases); domain/native style tests passed 4,619 assertions in 30 cases.
- Native DIMSTYLE checks cover suppression, offsets, alternate units, tolerance values and gaps;
  native STYLE checks cover zero height, generation flags and vertical text. MLEADERSTYLE checks
  cover spline type, landing length, frame and max points. Additional properties persist in DWG JSON.
- AutoCAD opened `build/artifacts/style-baseline-roundtrip.dwg` directly; AUDIT reported zero errors
  and zero fixes (`style-autocad-audit.out`). This validates native-file acceptance, not every style field.
- The full CTest run exposed two pre-existing failing assertions: Civil table import widened the saved
  frame from 93.42857 to 114.54 using a generic character-count heuristic and doubled description
  width; a normal test required GOSURVEY_ORACLE_DIR despite having an artifact-directory default available.
  Fix: preserve the source frame, derive table auto-fit bands from actual SHX ink bounds/cap heights
  when available, and report missing-font metric estimates. The audit test gets a checkout-local default.
  Full verification must be rerun after these corrections; the initial failing run is retained.
- No completion claim: TABLESTYLE native encoding, wider modifier coverage, paired Civil 3D UI review,
  final full-suite verification, docs updates and PR publication remain under active work.

#### Native TABLESTYLE decoder root cause (2026-11-01)

The resaved baseline's native Standard TABLESTYLE reports numoverrides=3, but LibreDWG's schema
consumes only one `ovr` record and prints TODO. A raw decoder probe shows the first is `_TITLE`
(id 1), followed by two unconsumed cell-style records. This prevents complete data/header/title
style mapping and produces a value-out-of-bounds read result. The codec now repeats the native
per-cell version + CellStyle + id/type/name record numoverrides times, with a bounded count.
Native baseline decoding, a controlled style fixture, native export and AutoCAD AUDIT are required
before this decoder/export path can be marked verified.

#### Native TABLESTYLE encoding evidence

- Added a native constructor and encoded title/header/data style arrays for R2010+ and legacy rows for
  R2004/R2007. GoSurvey exports drawing table styles into ACAD_TABLESTYLE, with native ownership/reactors.
- `native-table-styles-audit.out`: AutoCAD opened the Rapides export with these records and AUDIT found
  zero errors/zero fixes. The source dictionary restores the formerly lost Legend and Proposed names.
- `civil-decoder/table-api-generated-reference.txt` and `table-api-methods.txt`: AutoCAD's managed API
  reads the generated Survey Schedule style as distinct title/header/data heights, a yellow title fill,
  BottomRight header alignment, and hidden Data top/bottom borders. Query individual GridLineType values;
  AutoCAD's getter with AllGridLines returns True even when individual borders are hidden.
- A codec ownership regression appeared immediately when the legacy first-record alias was freed as an
  owner as well as the new array. The legacy free path now skips that borrowed alias. Focused native/style
  tests pass after the correction. Retain this case in regression coverage; run the complete suite again.
