# TASK-479 — Decode Civil 3D data into GoSurvey objects

- Type: feature
- Status: in progress
- Opened: 2026-10-31
- Owner: GoSurvey IO / Domain

## 1. Authority

- Goal: drawing interchange and survey-data integrity, as stated by REQ-478/479.
- Requirements: REQ-170, REQ-201, REQ-478, REQ-479 (accepted).
- Constraints: Windows/MSVC; LibreDWG remains the DWG codec; no AutoCAD/Civil 3D runtime, Object Enabler,
  external bridge, or ODA Civil SDK required by the product (D-2026-10-31-a, ADR-074).
- Acceptance: satisfy the amended REQ-478/479 mapping criteria against the supplied Rapides Substation
  baseline; export ordinary AutoCAD entities/styles accepted without Recover; list unmapped Civil data.
  REQ-478 also governs drawing-owned style collections and the DIMSTY UI.
- Owning subsystem: DWG IO for raw object decoding and ordinary-entity export; Domain for mapped points,
  surfaces, alignments, labels, and styles; Commands/UI consume domain types only.

## 2. Scope

- In scope: replace the production Civil 3D runtime bridge with an in-process parser; use REA and the
  existing bridge/API output as investigation evidence; decode the supplied model's 353 COGO points,
  TIN topology, supported alignments, labels, and style settings into GoSurvey objects; export those mapped
  objects as ordinary AutoCAD entities/styles; persist supported values in the DWG-embedded GoSurvey data.
- Out of scope: authoring Civil 3D custom objects, preserving native Civil 3D identity, and lossless retention
  of unsupported classes. Unmapped classes/fields must be named in the import/export log.
- Smallest change: semantic COGO point and TIN decode from real source records, map to current GoSurvey
  SurveyPoint/CadTin models, and verify DWG export/re-import; proxy graphics may help display but cannot stand
  in for semantic data. Plant 3D custom objects and shipped CAD runtimes/SDKs remain out of scope.

## 3. Architectural boundary check

- In-process Civil data decoding into GoSurvey objects is approved under D-2026-10-31-a/b / ADR-074.
- No new shipped dependency or external process is allowed. REA and Civil 3D may be used as development
  investigation/verification tools only.

## 4. Questions

| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| 1 | Must GoSurvey open the file with no AutoCAD/Civil 3D installed? | 2026-10-31 | Yes, user confirmed; recorded in D-2026-10-31-a. |
| 2 | Should the product build its own Civil 3D object parser instead of shipping the runtime bridge? | 2026-10-31 | Yes, user explicitly directed an in-house parser and authorized reverse engineering. |

## 5. Assumptions

None. The file version and class inventory will be read from the supplied baseline; they will not be inferred.

## 6. Plan

- Approach: inspect raw class/object payloads through in-tree LibreDWG; correlate fields with the captured
  bridge/API oracle and controlled baseline probes; decode into domain values; write ordinary AutoCAD entities
  through LibreDWG. Keep the bridge out of the product path and report every unsupported mapping.
- Files/functions to touch: `src/io/LibreDwg*`, vendored LibreDWG only where required to expose raw records,
  survey/surface/style domain types, `.gs` and DWG serialization, and baseline fixture tests.
- Test approach: (1) parse the exact baseline through GoSurvey in an environment with no CAD runtime and
  assert point count/numbers/coordinates/elevations/descriptions, TIN vertex/triangle topology, supported
  labels/styles and alignment geometry;
  (2) reject malformed/truncated custom records without mutating the drawing; (3) GoSurvey DWG save/reopen
  preserves mapped values and relationships; (4) AutoCAD opens ordinary entity output without Recover;
  (5) unmapped source classes/fields are reported by name/count.
- Steps:
  - [x] inventory raw class IDs, handles, payload bytes and references from baseline DWG
  - [ ] correlate raw records with bridge/API exports and document the observed schema
  - [ ] implement decoder/mapping and no-runtime baseline read
  - [ ] implement mapped object/style persistence and ordinary-entity DWG write
  - [x] remove the bridge from all GoSurvey runtime routes
  - [ ] add failure and unmapped-class log tests
  - [x] build Debug, run the `[civil3d]` tests, and launch GoSurvey with the baseline from this worktree
  - [ ] complete semantic field/topology validation and external AutoCAD open-without-Recover check

## 7. Verification review

- Architecture: APPROVE. ADR-074 assigns decoding and encoding to DWG IO and domain mapping to Domain.
- Dependency: APPROVE. LibreDWG remains the existing dependency; REA is not shipped.
- Correctness risk: high. Civil 3D payloads are versioned and reference many drawing-owned records; each
  accepted class needs field-level fixture assertions and round-trip evidence.
- Performance risk: import-time only; do not add work to frame/render paths.
- Verdict: APPROVE the in-process implementation plan. Do not report REQ-479 complete until every acceptance
  criterion passes.

## 8. Implementation log

- 2026-10-31: user clarified that Civil 3D drawing support must be independent of Civil 3D/AutoCAD and
  explicitly directed an in-house parser using reverse-engineering evidence if needed.
- 2026-10-31: user narrowed the output requirement: decode enough data to construct GoSurvey objects
  (COGO point location/style/number/description; surfaces with triangulation and label locations), then save
  ordinary valid DWG entities AutoCAD accepts. Native Civil 3D object authoring/identity is not required.
  D-2026-10-31-b, REQ-478/479 and this task plan were amended to match.
- 2026-10-31: found that PR #1141's current bridge loads `AeccDbMgd` inside Civil 3D and is not a valid
  product implementation for this requirement. REQ-479 already existed and was accepted, but ADR-026 and
  File Format Specs contradicted it; D-2026-10-31-a and ADR-074 record the user's clarification.
- 2026-10-31: added the first supplied 5,058,309-byte DWG as `samples/rapides-substation-civil3d.dwg`
  (SHA-256 `D8DF451C16F1C595B8AD65D026B52D826837D67E845E05A0855529A0EF320635`). The in-process
  class inventory passes for the R2018 file (40,123 objects, 721 classes), including 353
  `AECC_COGO_POINT`, one `AECC_TIN_SURFACE`, 7,464 `AECC_PARCEL`, 180 point styles, and 10 surface
  styles. Raw fallback payloads are exposed by LibreDWG: the sampled COGO point has 59 bits of custom
  tail plus 684 bytes of graphics preview; the TIN surface has 596,667 bits plus a 688-byte preview.
  The COGO point custom tail alone does not contain the full point record, so decoder work must follow
  its object references and/or the proxy graphics instead of treating that tail as the point data.
- 2026-10-31: a direct `dwg_read_file` → `dwg_write_file` experiment with the same R2018 file returned
  a readable DWG with 721 class slots, but its reread class table no longer exposed any `AECC_*`
  class names. This is a LibreDWG writer limitation to resolve or work around before using it as the
  native writer; it is not a passing Civil 3D round-trip.
- 2026-10-31: an app-level `ImportLibreCadFile` → `ExportLibreCadFile` experiment also did not retain
  the source Civil 3D objects in the new DWG. REQ-479 remains unimplemented; no user-facing parser or
  unsupported-class preservation claim is made from class-table recognition alone.
- 2026-10-31: REA statically inspected the installed `AeccDbMgd.dll` (SHA-256
  `3742562d832386be431d0692bba581809d81b39326ae685b8f998eac6c732646`, MVID
  `ca486c8c-3bb2-43a4-a474-05379fdea6c8`). It is a mixed C++/CLI .NET 8 assembly; this confirms the
  previous bridge's managed API dependency, but static metadata inspection did not decode DWG payloads
  and did not execute the assembly. The product path remains in-process.
- 2026-10-31: inspection of the removed bridge source confirmed that it writes a script which starts
  `accoreconsole.exe`, `.NETLOAD`s GoSurvey's managed bridge, and invokes the bridge's Civil 3D API
  command. The bridge did not itself parse the DWG custom record format; Civil 3D loaded the drawing
  and supplied typed API values. The independent GoSurvey parser therefore has to decode the AECC
  records from the DWG data directly. Autodesk's [Civil 3D User Guide](https://images.autodesk.com/adsk/files/civil3d_ug.pdf)
  describes proxy graphics as display/plot-only without object manipulation; the supplied TIN proxy
  was measured to contain only a bounds box. The [ODA Civil SDK](https://www.opendesign.com/products/civil)
  advertises native Civil object access, but adding it would violate the approved no-external-CAD-SDK
  constraint, so it is not used by GoSurvey.
- 2026-10-31: a new baseline test compares every `AECC_*` record's handle, class name, unknown payload
  bits and entity proxy bytes before and after same-version `dwg_read_file` → `dwg_write_file`; the
  records compare equal across more than 8,000 instances. This validates LibreDWG's raw pass-through
  capability only. The application still reconstructs its DWG from the GoSurvey domain model, so its
  save path still needs a source-object retention/merge implementation before Civil 3D round-trip passes.
- 2026-10-31: added a bounded in-process decoder for the R2018 proxy-graphics container's 3D-polyline
  chunks. The supplied `AECC_TIN_SURFACE` proxy contains six such preview paths (18 vertices); those
  are only display fallback geometry, not the TIN definition/topology. Added fixture-backed valid-stream
  and malformed-chunk coverage.
- 2026-10-31: user clarified the product boundary: decode supported Civil 3D data into GoSurvey domain
  values and export ordinary DWG entities that AutoCAD accepts; do not author Civil 3D custom objects or
  require native object identity. Amended D-2026-10-31-b, REQ-478/479, ADR-074, roadmap, and this plan.
- 2026-10-31: removed source-DWG byte preservation and the save refusal that blocked edits to Civil 3D
  drawings. Export now routes through LibreDWG's ordinary entity writer. The import log explicitly names
  COGO description/style fields not yet decoded and proxy-only Civil classes as display geometry, not
  semantic GoSurvey objects. Replaced the unchanged-source preservation test with an edited ordinary
  DWG export test.
- 2026-10-31: full `build.bat debug` completed successfully; the `[civil3d]` filter passed (5 cases,
  463 assertions). Launched the latest `build/debug/GoSurvey.exe` with the Rapides baseline. The app is running from
  this worktree. This does not complete semantic COGO descriptions/styles, TIN topology, or external
  AutoCAD-open acceptance checks.

## 9. Self-verification

- [x] build-project (full Debug build)
- [ ] architecture-review
- [ ] code-review
- [ ] dependency-audit
- [ ] performance-review
- [x] testing (`[civil3d]` filter: 5 cases, 463 assertions; full suite was not rerun after this increment)

## Style import increment — 2026-10-31

- User scope clarification: standalone `.GS` drawings are no longer supported. DWG is the only drawing
  persistence/interchange acceptance target; this does not remove GoSurvey-specific data from DWG.
- Added independent `markerVisible` / `labelVisible` values to `SurveyPointStyleDefinition`, DWG trailer
  JSON persistence, and point viewport rendering. Marker color now resolves through the assigned style
  and point layer. This makes a decoded hidden point style render without a marker or ID overlay.
- Expanded ordinary DWG table import for text STYLE height/width/oblique/font, dimension colors and text
  style, multileader text style, and basic TABLESTYLE name/header/data-row text dimensions. Import now
  resets drawing-owned collections before loading another DWG.
- Specs now require point-group and style references, per-component COGO visibility, drawing-owned style
  mapping, and DWG-only round-trip coverage.
- **Still incomplete:** LibreDWG leaves Civil 3D point/point-label/surface styles, point groups, COGO
  descriptions and style references, Civil TIN topology, surface labels and alignments opaque. The baseline
  import currently assigns Standard point style and therefore cannot yet match hidden source components.
  Native TABLESTYLE import maps only fields GoSurvey currently models, and Civil TABLESTYLE export has not
  been implemented. This increment must not be described as satisfying REQ-478/479 until those gaps and
  AutoCAD open/save/re-import acceptance are closed.
- [ ] Verify the updated build and DWG tests against the supplied baseline; rerun affected requirements.

## Resaved point-table baseline — 2026-10-31

- Added the user's resaved 3,765,575-byte drawing as
  `samples/rapides-substation-civil3d-pointtables.dwg` (SHA-256
  `9BB529D7AF9F71A7D500FFDF40AD794AA2272F365B1D9612D7CED2A9CD124413`), preserving the original
  baseline unchanged. The original fixture SHA-256 is
  `D8DF451C16F1C595B8AD65D026B52D826837D67E845E05A0855529A0EF320635`.
- The resaved file contains 18 `AECC_POINT_TABLE` custom entities, zero ordinary AutoCAD `TABLE`
  entities, and three ordinary `TABLESTYLE` objects. Each point-table entity has 332 bytes of proxy
  preview data; the current generic Civil proxy-polyline decoder does not recognize that preview.
- Baseline import currently yields 353 survey points, no `CadTable` objects, and logs all 18 point
  tables as skipped. This confirms the new file exposes an additional custom-record format that must be
  decoded before point tables can be shown in GoSurvey. The existing table-style records alone do not
  provide the point-table cell contents.
- Added a fixture inventory test asserting the 18 custom entities and presence of preview payloads.
  `build.bat debug` and the `[civil3d]` filter pass (503 assertions in 6 cases). This is fixture
  characterization only; point-table import and display remain incomplete.
- Ordinary AutoCAD TABLE import now stores the table's referenced TABLESTYLE name on the resulting
  `CadTable`. This does not decode the Civil 3D `AECC_POINT_TABLE` custom objects or their cell values.

## 10. Outcome

- Requirements satisfied: pending. The current increment maps COGO identity and placement into GoSurvey
  survey-point objects and exports edited data through the ordinary DWG writer. Semantic point
  descriptions/styles, TIN definition/topology and labels, alignment mappings, and AutoCAD external-open
  acceptance remain open.
- Tests added: baseline class inventory, COGO number/coordinate decode, proxy-polyline decode, edited
  ordinary DWG export, and handle-keyed AECC payload equality in `tests/LibreDwgCadTests.cpp`; targeted
  Civil 3D tests pass.
  The app-level edit/write path and full semantics for descriptions, surfaces, parcels, styles and
  alignments remain open.
- Docs updated: spec/project.md, spec/requirements.md, spec/architecture.md, spec/file-format-specs.md,
  spec/roadmap.md.
- Done: pending.

## Independent point coordinates and layer-zero correction — 2026-10-31

- Continued this existing task in assigned checkout `t3-ea40cd1c`, carrying forward the PR commit
  and its uncommitted changes. The previous checkout's source changes were preserved.
- Added a bounded decoder for the observed AECC_POINT_NODE coordinate-section revision. Both
  source fixtures decode 353 identities and locations, checked against each COGO entity's preview.
  Drawing import now uses node coordinates before consulting display proxies.
- Fixed layer 0 import and export: both previously discarded the drawing's color, linetype and
  lineweight. Regression failed for all three fields before correction and passes afterward.
- Removed automatic Civil bridge compilation from the normal application build. Autodesk API probes
  remain development-only diagnostic tools under build/artifacts.
- Release build passes. Focused Civil/style tests pass: 5,521 assertions in 14 cases.
- Acceptance remains incomplete: description/style/group associations, Civil point-table cell data,
  TIN topology and surface labels still require decoding and source comparison.
- Added a second appearance regression for explicit ENC RGB colors. Before the fix #000080 became
  #00007F and #000001/#000100 became black. Explicit RGB flags now bypass CMC palette heuristics;
  the DWG export/import regression passes for all four colors including black.
- Broad Release CTest: 193/194 passed; the only failure was an obsolete log expectation excluding
  layer 0. Updated it to all 155 source layers, rebuilt, and reran both full unit shards: 2/2 pass.
  All six Qt shards and the headless/deployment/output-layout checks passed in that broad run.
- Development-only source probe reports the resaved baseline explicitly overrides point marker
  styles to Basic, while point labels remain no points. Basic's marker is a white plus; no points
  hides marker and label. Controlled SaveAs changed Basic's plan-marker visibility at raw bit 388,
  but this is characterization, not yet a general style-field decoder.
- Located all 218 TIN vertices in the custom surface payload (raw XYZ records), including four
  preceding boundary/superstructure vertices. Topology indexing is not yet decoded; no replacement
  triangulation is introduced because it would change the source surface.

## TIN surface topology — 2026-10-31

- Root cause of "surfaces not created": `AECC_TIN_SURFACE` was only imported as its 6-polyline proxy
  outline; no `CadSurface` existed. Decoded the stored triangulation instead of guessing one.
- Layout (revision signature 0x5430 / 0x718ea, same in the supplied drawing and in surfaces made by the
  Civil API): vertex table = 256-bit records (3 doubles + 64-bit separator), first four are the
  superstructure corners; then a compact triangle count; then 256-bit triangle records (3 vertex
  indices, 3 neighbour indices, flag, 0xffffffff). Flag bit 0 = hidden (superstructure or outside the
  boundaries); bits 0x38 are edge attributes. Surface name follows the table by a fixed 29 bits.
- Derived with controlled Civil API surfaces (3-6 points, `accoreconsole`) and confirmed on the real
  drawing: 222 stored vertices, 438 stored triangles, 40 hidden -> 398 visible, equal to the Civil API's
  398 triangles / 218 vertices as sets, in orientation and in order.
- `libredwgcivil::DecodeTinSurface` (atomic: signature/revision gate, index/neighbour range checks,
  known-flag mask) -> importer creates a fixed-triangulation `CadSurface`. Style, boundary and edit
  definition are NOT decoded; the surface keeps the default style and cannot be re-derived by rebuild.
- `ShiftAllStorageBy` now shifts built TINs (it skipped surfaces).
- LibreDWG `dwg_add_POLYLINE_PFACE` kept a pointer into `dwg->object` across ~600 object additions
  (realloc -> stale pointer -> SIGSEGV on saving this drawing). Re-resolved after each addition.
- Tests: import equals Civil API triangles (samples/rapides-tin-reference.json), save/reopen keeps the
  surface, decoder refuses truncated/mismatched payloads. Release CTest 194/194.
- Still open: surface style/labels/boundaries, point descriptions/groups/tables, alignments, parcels.

## Surface style decode — 2026-10-31

- Why the imported surface looked wrong: it drew with GoSurvey's default style (2 ft / 10 ft contours) over a
  2.5 ft elevation range, so almost nothing showed. The drawing's own style draws 0.5 ft minor / 1 ft major
  contours, a boundary, no triangles.
- `libredwgcivil::DecodeSurfaceStyle`: version 1214; name = built-in token (30 = Standard, 0xcd8 = Design) or
  UTF-16 in the string stream; plan components (Points, Triangles, Boundary, Major, Minor) from the 25
  0x54150950 component records; contour intervals from ten 0x460102-tagged values (index 8 = major; the
  minor/base pair is (base, minor) in original styles and (minor, base) in copied styles, so the one
  non-zero member is the minor interval). Verified against the Civil API for all 9 styles
  (samples/rapides-surface-style-reference.json).
- TIN -> style link: the TIN's last stored handle is its style's handle minus one (Standard is exact);
  confirmed on 6 Civil API surfaces. Resolved as H+1 if that is a surface style, else H.
- Not decoded: contour smoothing flag/factor (field position differs between style layouts), labels,
  slopes, analysis, border/contour linetypes and layers. Style table entries use ByLayer linetype.
- Release CTest 194/194. Screenshot of the imported drawing: build/artifacts/surface-check/app-open.png.

## Contour smoothing, surface elevation labels, contour labels — 2026-10-31

- Smoothing: every style field is introduced by a 0x4601TT tag (TT = type). Smoothing factor = first
  type-1 tag's compact short; smoothing flag = bit 7 after the end of the fifth of seven type-3 tags.
  Verified on 22 styles/variants in both layouts (original and copy-command), then matched the API on
  all 9 styles (samples/rapides-surface-style-reference.json). GoSurvey passes = clamp(round(log2(f+1)),1,5)
  (factor 7 -> 3). Smoothing TYPE (add vertices vs spline) is not decoded.
- Surface elevation labels (11): location+elevation stored twice (raw (x,y) at bit 347, compact (x,y,z) at
  811; must agree). Imported as a circle r=1 + cross +/-2 + MTEXT "%.2f" 2 units high, middle-left, 2.5
  right of the point (what the "Elevation Only" style produced, read back via the Civil API explode).
  Label style itself is not decoded; precision/offset are fixed to what that style does.
- Contour labels (44 populated + 6 empty groups of 50): elevation/x/y are raw doubles (559/757/821) and must
  match the preview rectangle's centre and elevation. Imported as MTEXT "%.1f", 1.6 high, middle-centre,
  ACI 9. The rotation is NOT stored (Civil derives it from the smoothed contour); GoSurvey uses the TIN
  facet's contour direction: median 1 deg, p90 5 deg, max 12 deg against the API.
- Proxy-graphics header word 2 is a chunk count, not a version 8 (decoder accepted only 8-chunk previews).
- Found: all 7,464 AECC_PARCEL (and the 6 empty label groups) imported Civil's empty-label placeholder box
  (1.5 x 0.12 at world 0,0) = ~15,000 stray polylines three million feet from the site. Now skipped.
- Release CTest 194/194.
- Not verified visually: labels at working zoom (GUI wheel/drag automation was unreliable); verified by
  data tests against the API.
- Still open: label styles, point descriptions/groups/tables (the point-table frames are the large boxes
  east of the site; their cell text is not decoded), alignments, parcels, smoothing type.

## Point styles, group priority and opaque-stream correction — 2026-10-31

- Point groups now retain drawing-owned style names, override flags and display priority. Controlled
  Civil saves reversing DrawOrder and toggling the two overrides establish their binary fields.
  Priority is a compact BL; assuming ten bits broke membership for priority zero (WATER).
- Both source drawings now resolve all 353 point marker assignments: the original inherits the hidden
  `no points` style; the resaved drawing preserves its explicit `Basic` overrides. All descriptions,
  including the predefined CP string token, match the Civil API reference.
- Imported all 10 point-label styles with source visibility and all 20 serialized component expressions.
  Expressions are UTF-16 in the MAIN stream with numeric Civil property IDs, not TU name strings.
  Label visibility now suppresses the independent viewport ID overlay. These properties and group
  style metadata persist in the drawing payload. Component placement/font/height inheritance and full
  label rendering/editing are still open; retaining expressions alone is not full label-style support.
- Point DWG export now writes style-specific ordinary marker blocks (custom glyphs, overlays, symbol
  references, size, rotation and color) and hidden attributes when the source label style is hidden.
  A regression failed with a circle and visible labels before the fix, then passes with two plus-marker
  lines and two invisible attributes. Block definitions are written before point-symbol INSERTs.
- Civil drawing scale imports from AEC_VARS_DWG_SETUP: the source is 20 model units per plotted inch,
  previously replaced by the application default of 50. Controlled 20 -> 44 saves isolate the field.
- Found the underlying cause of apparently reduced final handles: bit_read_bits packed its partial
  final byte LSB-first, while every consumer and bit_write_bits use MSB-first. The new all-alignment
  regression fails 400/4480 assertions before the correction and passes afterward. Updated the paired
  opaque encoder, preserving raw custom-record round trips. Removed the erroneous surface-handle +1
  workaround. The point table's final group handle now resolves EXACTLY to ALL POINTS (1ebda).
- Focused Civil/opaque tests pass: 28,380 assertions, 22 cases. Release build passes. A broad CTest run
  passes 193/194; its sole failure was the old expectation that CP remained empty, now corrected.
  All six Qt shards passed. Full verification must be rerun after the stream correction.
- Native AutoCAD side-database reading materializes the one-point custom/legacy exports successfully
  (3 blocks; 5/4 entities). Direct accoreconsole /i opening still returns eFilerError for these exports;
  external open acceptance is NOT complete. A plain LibreDWG R2004 control also fails side-database
  reading while R2000/R2010/R2018 controls pass. These are separate writer/open issues to diagnose.
- Table investigation: decoded the parent/child chain, source group reference and the two affine
  matrices. Segment row-capacity and first-row fields are immediately before a fixed tagged gap value;
  source segments hold capacity 20 and first rows 0,20,...340. Table style/cell layout still open.
- Autodesk reference checked: DrawOrder property and point-group override behavior:
  https://help.autodesk.com/cloudhelp/2020/ENU/Civil3D-API/files/html/f75bb49e-8121-e147-f95c-1bd9f0ad87ae.htm
  https://www.autodesk.com/support/technical/article/caas/sfdcarticles/sfdcarticles/Cogo-points-of-a-point-group-are-not-displayed-with-the-point-group-style-in-Civil-3D.html

### 2026-10-31 continuation: native attribute acceptance and point tables

- Native packet comparison proved ATTRIB has no trailing keep_duplicate_records byte, while ATTDEF
  does retain it. ATTDEF's explicit source normal takes seven bits; the writer's default normal takes
  one. The regression verifies native ATTRIB body 229 bits and ATTDEF 243 source / 237 rewritten bits.
- Corrected attribute ownership, child hard-owner references, default single-line type and width, and
  object-array relocation in the vendored constructors. A native-authored synthetic control and the
  GoSurvey one-point export now open directly in accoreconsole and AUDIT reports zero errors for both.
  The full baseline export's open/audit is still being investigated; this does not establish full acceptance.
- Civil point-table style evidence: controlled edits of data/header/title text height and column width
  in both PNEZD and NCRS styles isolate the compact-width header and three subsequent text heights.
  The decoder validates revision, 22 display components, the complete TU column-expression footer and
  one width record per column. All four source styles' names, headers, expressions and sizes match API.
- User's explicit point-table scope is now recorded in REQ-479. Plan verification: derive cells only
  from decoded group IDs, point values and formatting expressions; the preview bounds can supply the
  table frame, never semantic cell text. Retain style expressions and source names independently of
  the UI key, so Civil and AutoCAD Standard styles do not overwrite one another. No Autodesk runtime
  dependency. Reject unsupported expressions rather than silently fabricate table values.
- Point-table import is in progress: current implementation connects split segments through their
  previous-table handles to the root's group/style, then constructs editable cells in stored row order.
  Merged title, precise row/column sizing and component presentation remain open before visual acceptance.

### Native DWG acceptance investigation (continued)

- AutoCAD directly opens the separate 353-point Basic-marker export, all 18 point tables, the complete
  ordinary linework/block export, the TIN-only export, and the complete baseline with only its three
  multileaders removed. Each of those task-generated exports reports AUDIT: zero errors, zero fixed.
- Native FOMKR symbol control isolated MTEXT's invalid zero-version embedded header. Constructor now
  uses native class version 4/default flag 1; a centered native control also proves the duplicated
  embedded attachment must follow the entity's current attachment value. Synthetic native fixtures
  and regression tests cover these fields.
- TIN's initial native-open failure was a MATERIAL error, not topology. Six required mapper matrices
  were null, causing the writer to omit 16 values per mapper while the reader consumed them. Filled
  each with identity and added a strict error==0 round-trip check. Native AUDIT then identified missing
  dictionary ownership/reactors; corrected both, including pointer relocation when creating the dictionary.
- Two AutoCAD-authored mesh/material controls establish R2007+ entity common flag order:
  linetype, material, shadow, plot style. The old order parsed a native material as plot style and put
  the written material handle into the polyface's vertex vector. A control with both shadow flags off
  proves the eight-bit shadow position (old reader got 12 instead of 3). TIN now opens and audits cleanly.
- Full CTest exposed an unrelated consumer of the corrected opaque-byte format: Plant's bit reader
  still reversed its partial final byte. Removed that obsolete workaround; the existing independent
  AutoCAD placement-reference regression must pass again before completion.
- Remaining full-baseline native-open failure was isolated to MULTILEADER (extra R2007 arrowhead
  array written on R2010+). `dwg2.spec` now gates arrowheads to R2007 and earlier; `dwg_add_MULTILEADER`
  zeroes label/arrowhead counts. Catch2 round-trips the AutoCAD R2018 control and Rapides' three exported
  leaders (`[issue993]`). AutoCAD 2027 Core Console AUDIT: see
  `build/artifacts/mleader-audit/accore-audit-summary.txt` (0 errors on native control, LibreDWG
  roundtrip, Rapides three-leader export, and full mapped export).
- Current full test run is not green (one Plant placement case failed before its fix). A concurrent
  evidence-build relink collided with the running unit executable; that build is not accepted as a
  successful build despite build.bat printing its success footer. Rebuild sequentially after the suite.
- Table importer tests pass: 18 editable segments, four source Civil styles, all 353 rows, source order,
  raw description and coordinate precision. Merged title and separate row/font sizes persist in DWG
  payload and are used in Qt and ordinary-entity export. Precise auto column sizing, component display
  layers/colors, and complete point-label component geometry still require work/verification.

## Point descriptions and point groups — 2026-10-31

- Descriptions: the point node stores (point number, token) pairs; a token with the high bit takes the
  next literal from the string stream (one empty string, one per entry of the preceding "$*" list, then
  one per described point). 352 of 353 match the Civil API raw descriptions; point 1's "CP" is a
  built-in token with no stored text and stays empty.
- Point groups (25): name is a string-stream literal or one of three built-in tokens (0x17 Surface,
  0x21 _All Points, 0x25 Alignment, each seen once). Members = compact count at name-token end + 178
  bits followed by raw 32-bit numbers, accepted only if every number is an imported point. 24/25
  match the API as sets; WATER (empty) has a different layout and imports empty and reported.
  GoSurvey keeps the stored membership as explicit ids (Civil's include/exclude queries are not decoded).
- Point tables: imported by the concurrent work in this checkout (table style column ids 16797697,
  33574914/17/18, 167792643 = Point #, Elevation, Northing, Easting, Description).
- Point label style TEXT templates (<[Point Number(Sn)]> etc.) are not stored as text in the label
  collector/text-style objects; not decoded. Names exist (AECC_LABEL_COLLECTOR_STYLE).
- Release CTest 194/194 with the concurrent point-table work included.
