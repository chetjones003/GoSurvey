# TASK-1003 — LandXML import

## Requirement authority
- GitHub issue #1003 (sub-issue of #802): import supported LandXML data.
- **REQ-506** (accepted 2026-11-06, decision D-2026-11-06-a). The user decided on 2026-11-06 that
  the import covers all three contents (points, TIN surfaces, feature lines), that point-number
  conflicts use the existing conflict handling, and that unsupported items are reported and skipped.
- Two points were proposed by Claude and are open for the user to confirm at PR review (recorded in
  REQ-506 and D-2026-11-06-a): surface source triangle faces are re-triangulated rather than kept
  (item 6), and a headless run stores no conflicting point (item 5).
- Constraints: REQ-448 (the export contract this reads back), REQ-101 precision, REQ-201 observable
  failures, REQ-300 (no new dependency), REQ-022/REQ-357 units, ADR-039 (surfaces are definitions).

## Plan and what was built
- `ImportLandXmlFile(AppCommandState&, path, log)` in `src/io/LandXmlIo.{hpp,cpp}`, reading with the
  vendored pugixml. Reads the file into memory, then parses; an unreadable or non-LandXML file returns
  false before any change.
- Points: `CgPoint` → `SurveyPoint` (name = number, `desc`, `code` → `rawDescription`). New numbers go
  through `ResolveConflictingWorldSurveyPoints` (the resolver the Extract Coordinates and DXF paths use).
  Existing numbers are held in `st.pendingLandXmlConflictPoints` for the Overwrite/Offset answer.
- Surfaces: `Surface` with `surfType="TIN"` → `CadSurface` with its `Pnts` as `addedPointXyz`,
  then `BuildSurfaceFromSources` (REQ-144 added points). Source faces are not kept.
- Feature lines: `PlanFeature` `Line` segments chain into runs; a closing segment marks closed; gaps
  split; curves and other geometry skipped and logged. Appended in the same parallel arrays the
  WATERDROP extract uses.
- Units: file `linearUnit` ÷ drawing unit (INSUNITS 1/2/4/6, US survey foot per Drawing Settings).
  Unknown or unitless values taken as written, with a log line.
- Command: `IMPORTLANDXML "<path>"` in the headless idle dispatch beside `EXPORTLANDXML`; refuses when
  no drawing is open or the project is read-only.
- Qt: ribbon Insert → Import → LandXML runs `IMPORTLANDXML`; the bare verb opens an open dialog then
  submits the quoted form; `AskLandXmlPointConflicts` asks Overwrite/Offset after the command.
- Docs: `resources/wiki/Import-and-Export.md` and the public GitHub wiki (REQ-453).

## Test approach
- `tests/LandXmlImportTests.cpp` (24 cases, tag `[landxml]`): export→import round trip, far-from-origin
  exactness, US survey foot conversion, unitless drawing, conflict hold/overwrite/offset, skip-and-report,
  unreadable file, and the IMPORTLANDXML command path.
- `tests/RibbonWiringGapTests.cpp`: the Insert → Import → LandXML button is wired and not a placeholder.
- Qt editor shards (`ctest -R "^qt[.]editor"`) run for the ribbon and dialog wiring.

## Decisions made while implementing
- Conflicts are resolved in the Qt editor only. A headless run leaves them in the pending list and
  logs them; nothing is overwritten without an answer.
- A point number repeated within one file keeps its first occurrence.
- A feature line gap produces one feature line per run, all with the same name.
- The DXF import's own pending-conflict modal (`dxfPointConflictModalOpen`) has no Qt consumer; it is
  not changed here (see the #1003 completion note).

## Architectural-boundary check
- IO reads into the document state through the same services the CSV and DXF paths use. No new store,
  no global, no UI types in `src/io`.
