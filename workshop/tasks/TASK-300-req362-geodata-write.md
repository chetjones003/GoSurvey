# TASK-300 — REQ-362 item 2: write GEODATA (the map pin) into GoSurvey's DWG

- Type:    feature
- Status:  submitted
- Opened:  2026-09-30
- Owner:   Workshop
- GitHub:  #582 (increment 6, write side); builds on #590 / PR #595 and TASK-299 / PR #598

## 1. Authority

- REQ-362 item 2 (accepted 2026-09-30, D-2026-09-30-d): a drawing with both a geographic marker and
  a zone writes one `GEODATA` (class `AcDbGeoData`) on model space's extension dictionary under
  `ACAD_GEOGRAPHICDATA`, R2000: design point = marker (WCS); reference point = the marker's
  latitude / longitude from the zone (CS-MAP); north = the marker's north; horizontal unit = the
  drawing unit; definition = the zone code; REQ-360's scale settings. No marker, or a marker but no
  zone → no GEODATA, and the save log says which and why. No geo mesh.
- REQ-362 acceptance (write): AutoCAD 2027 opens a `TX83-CF` drawing with a marker with no error and
  reads back design point (REQ-101), reference point (1e-7° of CS-MAP), north (0.001°), `TX83-CF`,
  `CGEOCS` = `TX83-CF` (accoreconsole, recorded here); the same file with its trailer removed reopens
  in GoSurvey with zone, marker, north and scale settings from GEODATA alone (automated); no marker /
  no zone → no GEODATA and the log says why; Civil 3D shows the location (manual, the user).
- Also: REQ-359 item 4 (the marker is WORLD, default the drawing origin with grid north), REQ-358,
  REQ-360, REQ-201, D-2026-09-30-c (R2000), ADR-041 (h).
- Owning subsystems: Commands (`CadCommands_Geo.cpp`: what the pin says), IO (`LibreDwgCad.cpp`:
  encoding it) — the mirror of the read side (`ReadDwgGeoData` → `ApplyDwgGeoData`).

## 2. Scope

- In scope: DWG save (Export DWG / Save / Save As, all through `ExportLibreCadFile`).
- Out of scope: DXF export (REQ-362 names the R2000 file; LibreDWG's DXF GEODATA output is untested
  against AutoCAD); the geo mesh; the coordinate type AutoCAD reports (1, local grid, vs 2 for its own
  copy) beyond one more investigation attempt — reported, not a blocker.

## 3. Architectural boundary check

- [x] No new dependency, layer or format. `DwgGeoData` (the existing IO ↔ Commands record) gains the
  horizontal unit. The object is created with LibreDWG's exported `dwg_add_class` / `dwg_add_object` /
  `dwg_add_DICTIONARY` plus its internal `dwg_set_next_objhandle` (non-static in `dwg.c`, not in
  `dwg.h`), declared locally: LibreDWG has no `dwg_add_GEODATA` (`HAVE_NO_DWG_ADD_GEODATA`).

## 4. Assumptions

```
ASSUMPTION-1: "has a marker" is always true for a geolocated drawing.
- Because:       REQ-359 item 4 — the marker defaults to the drawing origin with grid north; Remove
                 Location clears marker and zone together. Only a zone makes a pin possible.
- Risk if wrong: none for AutoCAD (a pin always has a design point); the log wording only.
ASSUMPTION-2: a zone whose latitude/longitude cannot be computed (a code the dictionary does not
              know, REQ-358 item 5, or a marker outside the zone's domain) writes no GEODATA, logged.
- Because:       the reference point is required and the user chose "never a half-filled pin".
- Validate by:   the user reviewing the PR.
```

## 5. Plan

- `DwgGeoData`: + `horizontalUnits` (AutoCAD UnitsValue = INSUNITS code) and `horizontalUnitScale`
  (meters per unit).
- Commands: `BuildDwgGeoData(st, &g, &why)` — false + reason when there is no zone or the marker's
  lat/long fails; else design = marker, reference = `DrawingPointToLatLong(marker − origin)`, north
  vector from `markerNorthDeg`, units from `drawingInsUnits`, scale settings from the Transform
  (Reference Point → 3, User Defined → 2), definition = zone code.
- IO: `WriteDwgGeoData(dwg, g)` — the class, the object (class-version-1 layout at R2000: reference
  stored (lat, lon), north as radians from +Y in all three angle fields, coordinate type 0 as AutoCAD
  writes it), the extension dictionary on `*Model_Space`, owner / reactor links. Called by
  `ExportLibreCadFile` for DWG only, logging what was written or why nothing was.
- Tests (GoSurveySnapTests, `[req362]`): build rules (no zone, unknown zone, north/units/scale mapping);
  DWG round trip from GEODATA alone (trailer stripped) reproduces zone, marker, north, scale settings;
  a drawing without a zone writes no GEODATA and the log says so; DXF export writes none.
- Oracle: accoreconsole read-back of a `TX83-CF` export (values + `CGEOCS`).

## 6. Implementation log

- 2026-09-30: first run wrote no GEODATA. Two LibreDWG return conventions differ from what the
  scratch spike assumed: `dwg_add_class` returns the class number (500+), not 0 (its header comment is
  wrong), and `dwg_add_object` returns -1 when `dwg->object[]` moved, which is not an error — the
  caller re-resolves references (`dwg_resolve_objectrefs_silent`), as LibreDWG's own NEW_OBJECT does.
  Both handled; only DWG_ERR_OUTOFMEM fails.
- A new document has DWG_OPTS_IN set, so `dwg_free` frees an object's `name` and `dxfname`: they are
  heap copies (`_strdup`), and the text fields come from `dwg_add_u8_input`.
- The old test "…opens from the trailer; no GEODATA opens as before" asserted the read-only era's
  body without a location; it passed only because its marker (12.5, -7.25) lies outside Texas Central,
  so no latitude/longitude could be computed. Rewritten: its trailer half stays, with an in-zone marker;
  the body half is the new round-trip test.
- Self-review: `WriteDwgGeoData` refuses (returns false, logged) when *Model_Space already has an
  extension dictionary, rather than replacing it; a new document never has one.
- Coordinate type: AutoCAD reports **2** (projected grid) for the GEODATA this save writes; the spike's
  1 came from a reference point outside the zone. REQ-362 item 3 note added.

## 7. Completion report

- Tests (GoSurveySnapTests, `[req362]`, 10 cases / 115 assertions, pass): what the save writes
  (marker, AG9976 lat/long within 1e-7°, north 33°, US-foot unit, zone, Reference Point / User
  Defined, sea level, radius); no GEODATA for no zone / an unknown zone / a Unitless drawing, with the
  reason; the DWG body alone (no trailer) reopens zone, marker, north and scale settings from GEODATA;
  a DWG without a zone logs "no GEODATA written: the drawing has no zone"; DXF export writes none;
  a GoSurvey DWG with a trailer still opens from the trailer. Mutation: with the write disabled, 3 of
  the 10 cases fail.
- Build clean; `./dev/test` 2016/2023, the same 7 headless transcripts that fail on beta. All 214 DWGs
  the transcripts wrote, plus the two map-pin files, open in AutoCAD 2027 with no error.
- Oracle (AutoCAD 2027 `accoreconsole`, `TX83-CF`, marker at AG9976's grid, north 33°, from both
  `ExportDwgFile` (with trailer) and `ExportLibreCadFile` (body only)): opens with no error; design
  point 3115243.140, 10077391.260 (exact); reference -97.739365931, 30.286253464 (CS-MAP's, to 1e-9°);
  north (0.838671, 0.544639) = 33°; units feet 0.3048006; scale estimation 3; sea level on, 500;
  radius 6378137; definition `TX83-CF`; `CGEOCS` = `TX83-CF`; coordinate type 2.
- Assumptions: ASSUMPTION-1 and -2 above (a geolocated drawing always has a marker; no GEODATA when
  lat/long cannot be computed, including a Unitless drawing).
- Not done here: the Civil 3D check (manual, the user — Civil 3D is not on this machine).
