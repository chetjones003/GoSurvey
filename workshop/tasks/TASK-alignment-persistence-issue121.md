# TASK — REQ-496 alignment persistence, DWG round-trip and Civil 3D mapping (issue #121)

## 1. Requirement authority
- **REQ-496** (accepted 2026-11-02), statements 1–4 and acceptance 1–4; GOAL of issue #121 (D-2026-11-02-b, ADR-075 (4)).
- Constraints: ADR-044 (trailer authoritative), ADR-074 (decode Civil records in the LibreDWG path, export ordinary
  entities, never author Civil objects), D-2026-10-31-c (DWG is the persistence target; verify export → AutoCAD
  open/save → GoSurvey import with DWG fixtures), REQ-485 (the conversion used for a foreign re-read), REQ-201.
- User request: "Now req 496" (2026-11-04).

## 2. What already exists (evidence)
- Statement 1: `BuildRoot` / `ApplyDocumentFromJson` (`src/io/GsIo.cpp`) write and read every alignment field into the
  ADR-044 trailer (`src/io/DwgIo.cpp`); tests round-trip the JSON in memory only.
- Statement 2: `ExportLibreCadFile` (`src/io/LibreDwgCad.cpp`) writes LINE / ARC with `GOSURVEY` XDATA
  `{"ALIGNMENT", name}`. Spirals are written as one LINE per 0.001 units of length (≈100 000 LINEs for a 100-unit
  spiral), not as a polyline. Nothing re-reads the tagged entities: a trailer-less DWG imports them as plain lines.
- Statement 3: no `AECC_ALIGNMENT` decoder. A class / object count of every sample DWG (temporary probe, removed)
  found **no `AECC_ALIGNMENT` object** in `samples/rapides-substation-civil3d.dwg` or
  `rapides-substation-civil3d-pointtables.dwg` (they hold 4 alignment styles, 6 label sets and 3 design check sets
  only), nor in `4in Pipe Demo`, `duke-main-clean-r2018`, `example-piping-system`, `surface-demo`,
  `missing-point-file`. **Acceptance 3 cannot be met with the supplied baseline: SPEC GAP, raised with the user.**
- Statement 4: tolerant keys (no `alignments` key without alignments); untouched Standard styles are omitted.

## 3. Plan (statements 1, 2, 4; statement 3 waits for the user)
1. Export spirals as one LWPOLYLINE each, sampled to a chord height of 0.001 drawing units (REQ-495's 0.25 px is a
   screen measure; a file has no screen — GoSurvey rule, recorded). Vertex spacing √(8·tol/κmax).
2. Tag the first exported entity of each alignment with its start point and starting station
   (`{"ALIGNMENT", name, "START", "<x> <y> <station>"}`) so a re-read starts at the right end and station.
3. Import without a trailer: tagged LINE / ARC / LWPOLYLINE entities are collected by alignment name instead of
   being imported as linework, and each group becomes an alignment through `alignment::BuildFromSegments` (the
   REQ-485 conversion; no fillets; pick = the tagged start). Layer from the entity. A group the conversion refuses
   is imported as plain linework with the reason in the log (REQ-201). Spiral polylines come back as tangents (the
   REQ-485 rule for polyline line segments) and the log says so.
4. Tests: DWG-file round-trip through `SaveDrawingDocument` / `ImportDwgFile` with every field (geometry ≤1e-9,
   equations, style, label set, constraints, locks, type, offsets, references); trailer-stripped re-read
   (`ImportLibreCadFile`) giving the alignment back; spiral export is a polyline within tolerance; a legacy drawing
   without alignments saves identically twice (headless `SAMEFILE`); accoreconsole opens the saved DWG without
   Recover and lists the alignment's LINE / ARC / LWPOLYLINE on its layer (acceptance 2).

## 4. Architectural-boundary check
IO layer (`src/io/LibreDwgCad.cpp`), calling the Domain conversion (`alignment::BuildFromSegments`); one Commands
helper (`CadCoordinateFrame.cpp`) and one Domain query fix found by the tests. No Qt change, no new dependency, no
vendored-library change.

## 5. Implementation (as built)
- `src/io/LibreDwgCad.cpp`: `SpiralPolylinePoints` (one LWPOLYLINE per spiral, spacing √(8·0.001/κmax), ≤ 20 000
  vertices); `AlignmentStartTag` / `ParseAlignmentStartTag` (`START` on the first entity); `EnsureExportLayer`
  (shared with `AddNativeLayerViewportColor`) so a layer the drawing table lacks is not dropped to "0";
  `GosurveyEedStrings`, `TaggedAlignment`, `AppendTaggedSegments`, `CollectTaggedAlignmentEntity` (model-space
  import skips tagged LINE / ARC / LWPOLYLINE and files them by name) and `ImportTaggedAlignments` (REQ-485
  conversion from the tagged start, else plain linework with the reason).
- Found by the tests and fixed:
  - `ShiftAllStorageBy` (`src/commands/CadCoordinateFrame.cpp`) never moved alignments or fixed station-label points,
    so a document-origin rebase (reload of a state-plane drawing) left them displaced by the origin change. The old
    JSON round-trip test compared drawing-local numbers; it now compares ground positions (≤1e-9).
  - `Alignment::GetStationOffset` on a spiral returned the nearest grid sample (64 + 32 steps): up to ~0.02 units of
    offset and station error on a 60-unit spiral. `RefineSpiralFoot` adds Newton steps to the exact foot (≤1e-8 in
    the new test; REQ-484).
- Investigated and left alone: LibreDWG `bit_write_DD` compares the addresses of its local pointers, so it never
  chooses a patched encoding; that wastes bytes but is lossless (the polyline precision test passes unchanged).
- Spec: D-2026-11-04-l (`spec/project.md`, REQ-496.2).

## 6. Tests and verification
- `tests/AlignmentDwgTests.cpp` (11 cases + 2 hidden oracle cases): DWG save → reopen with every stored value for
  the curve road with its offset and the spiral road (≤1e-9, ground positions ≤1e-9, no duplicate linework);
  entity inventory (4 LINE, 3 ARC, 6 LWPOLYLINE, ~49 vertices each); polyline vertices exact and chords ≤0.0011;
  state-plane polyline precision; trailer-less re-read (name, layer C-ROAD, station 5+00, kinds, radii, start point);
  spirals as tangents with the log line; a non-chaining group stays linework with the reason; legacy
  `samples/font-demo.dwg` gains no alignment keys and re-saves identically; spiral station/offset exact.
- `[alignment]` 189 cases / 5195 assertions pass.
- AutoCAD oracle (acceptance 2): `build/artifacts/alignment-audit/SUMMARY.md` — both fixtures open without Recover,
  AUDIT "Total errors found 0 fixed 0", entities on C-ROAD with XDATA; AutoCAD's SAVEAS copy (no trailer) reopens in
  GoSurvey with both alignments rebuilt. **PASS.**
- Build: `build.bat` Release, no warnings. Full suite `ctest --preset ninja-release` 194/194
  (`build/artifacts/ctest-496.log`).
- No visible Qt UI change, so no screenshot panel.
- Acceptance 3: the Rapides baseline has no alignment (§2); resolved in §8.

## 8. Civil 3D alignment mapping (statement 3, acceptance 3) — the user's "Yes save a scratch drawing"
- Fixtures (Civil 3D 2026 COM on this machine; the user's three open drawings untouched; each scratch drawing saved
  and closed): Drawing11 → `samples/civil3d-alignment-fixture.dwg` + `-reference.json` (PIs (6500000,2100000) …
  (6501400,2100500); spiral-curve-spiral 60 / R 237.5 / 75; free curves R 412.25 and R 180; start 5+00; equation
  14+00 → 16+50 increasing; Civil 3D's 24 station coordinates); Drawing12 (groups in another order, other values,
  `build/artifacts/civil3d/fixture-order.dwg`, analysis only); Drawing13 → `samples/civil3d-alignment-equations.dwg`
  (equations 1234.5 → 1345.25 increasing, 1876.75 → 1765.5 decreasing).
- Layout found by locating the known doubles in the AECC_ALIGNMENT record and comparing the three records bit by
  bit (D-2026-11-04-m): BD starting station at bit 354; tangent = 12-bit marker, BL own / previous / next id, 2 × 2RD;
  after three -1.0 sentinels a free curve (BL 29, B, ids, BD radius) or a free SCS group (BL 0, ids, BL, BD spiral
  in, BD radius, BD spiral out); equations = BD raw, 128-bit id, BD raw again, BD ahead, B direction; the name is the
  first non-empty string after "AeccImpAlignment" in the string stream. The geometry itself is not stored; it is
  the definition, as Civil 3D's layout holds it.
- Code: `libredwgcivil::DecodeAlignment` (`src/io/LibreDwgCivil.*`); `ImportCivilAlignments` / `CivilAlignmentPis` /
  `TangentsMeet` (`src/io/LibreDwgCad.cpp`), run before the model-space pass so an imported alignment's proxy
  preview is not also read; `alignment::BuildFromPis` with per-PI `CurveSettings` (`AlignmentLayout.cpp`).
- Tests (`tests/AlignmentDwgTests.cpp`): the fixture imports as "GS Fixture", 9 entities, radii and spirals exact,
  start 5+00, the equation; Civil 3D's 24 stations: exact (≤1e-6) before the spirals, ≤2e-5 after (GoSurvey's
  clothoid places the group's ends ~7e-6 ft from Civil 3D's — REQ-503 open question 3); two equations keep their
  direction; the decoder refuses no data / zero bits and drops a record whose sentinel is broken; per-PI
  `BuildFromPis` and its one-setting-per-PI rule.
- Build `build.bat` Release with no warnings; full suite 194/194 (`build/artifacts/ctest-496b.log`).
- Docs: `resources/wiki/Alignments.md` "Civil 3D drawings" paragraph, mirrored to the public wiki and the site.
- Not decoded (named in the log, alignment not imported): fixed / floating curves and spirals, free-standing
  spirals, groups defined by A values; Civil 3D styles, label sets and label groups (their previews stay display
  geometry).

## 7. Product documentation review (REQ-453)
- `README.md`, `PRIVACY.md`: reviewed; no change needed (no new data or feature summary).
- `resources/wiki/Alignments.md`: what a saved DWG contains, and what happens after AutoCAD saves it.
- Public wiki `Alignments.md` (commit ee1978c) and gosurveycad.com `docs/alignments.html` (site commit ec41979): the
  same paragraph.
