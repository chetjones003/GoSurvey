# TASK-1002 — LandXML export

## Requirement authority
- GitHub issue #1002 (sub-issue of #802): export supported drawing data to LandXML. The user
  dispatched it with its acceptance criteria as the agreed scope on 2026-10-20.
- **SPEC GAP, handled with a proposal:** no accepted requirement covered LandXML (#802 recorded it as
  a SPEC GAP). REQ-448 and decision D-2026-10-20-i were proposed from the issue's
  acceptance criteria and accepted by the user on 2026-10-20. Nothing was added to the spec
  beyond what the issue and the implementation needed.
- Constraints used: REQ-101 precision, REQ-201 observable failures, REQ-300 dependency policy,
  ADR-025 local XY / absolute Z, ADR-028 surfaces, ADR-035 feature lines, ADR-054 double geometry.
- Acceptance criteria: points, TIN surfaces and feature lines of the current drawing are exported;
  drawing units and object names are preserved; the file opens in a compatible LandXML reader.

## Plan
- `ExportLandXmlFile(const AppCommandState&, path, log)` in `src/io/LandXmlIo.{hpp,cpp}`, using the
  vendored pugixml already compiled into the domain library. No new dependency. The module is the
  seam #1003 (import) will extend with a reader; the writer exposes no XML types.
- LandXML 1.2: `Units`, `Application`, `CgPoints/CgPoint`, `Surfaces/Surface/Definition/{Pnts/P,
  Faces/F}`, `PlanFeatures/PlanFeature/CoordGeom/Line`.
- Typed `EXPORTLANDXML "<file>"` in `ProcessCommandLineSubmit` (registry row included) so headless
  and Qt share one path. In Qt, the bare verb opens a save dialog (`EditorWindow::Submit`) and then
  submits the quoted form; the ribbon button runs that verb. Tests use the quoted form, so no
  dialog opens under test.
- Architectural boundary: IO reads the document state read-only; no new store, no global.

## Decisions made while implementing
- World coordinates: stored local X/Y plus `worldDocumentOrigin` added in double; Z absolute.
  Numbers use `std::to_chars` (shortest round-trip, fixed notation, locale-independent).
- Units: INSUNITS 1/2/4/6 map to inch / USSurveyFoot or foot (Drawing Settings foot definition) /
  millimeter / meter. Unitless writes no `Units`.
- An empty drawing writes **no file** (assumption; avoids overwriting a good file with an empty one).
- Items that cannot be represented are skipped and logged: grid surfaces, unbuilt or inconsistent
  TINs, feature lines with fewer than two vertices, non-finite coordinates.
- XML text: invalid UTF-8 and characters outside the XML 1.0 set become U+FFFD and are counted.
- Write goes to `<file>.tmp` and is renamed over the target, so a failure never leaves a partial or
  truncated export.
- A review of the earlier uncommitted draft found `cubicFoot` (not a LandXML 1.2 value; the schema
  says `cubicFeet`), no `elevationUnit` for imperial files (schema default is `meter`), a swallowed
  write result, and no surfaces, feature lines, escaping or tests beyond one point test.

## Verification evidence
- Unit tests: `tests/LandXmlExportTests.cpp` (16 cases incl. real `surface-demo.dwg` TIN),
  `RibbonWiringGapTests` `[issue1002]`, `QtEditorTests::issue1002RibbonExportsLandXmlThroughTheCommand`,
  headless transcript `issue1002-landxml-export`.
- Independent reader: the transcript's output file validates against the official LandXML 1.2 XSD
  (`LandXML-1.2.xsd`, posted 2008-07-29) with the .NET validating XML reader:
  `tools/validate-landxml.ps1 -File <file> -Schema LandXML-1.2.xsd` -> `VALID`. The same script
  rejects a file with `cubicFoot`, so it is not vacuous. The XSD is not vendored (download it from
  landxml.org).
- Final build/ctest results are in the pull request description.

## Technical debt
- Not exported: grid surfaces, surface source data (breaklines, boundaries, point files),
  `CoordinateSystem`/zone, feature-line elevation-point flags (written as ordinary vertices).
- Surfaces whose triangulation is stale (a source could not be read) export the older mesh and log it.
- `EXPORTLANDXML` has no options (selection-only export, point groups, precision).
- No user-facing release note yet.
