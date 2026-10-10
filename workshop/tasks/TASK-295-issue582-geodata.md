# TASK-295 — GEODATA read (REQ-362): feasibility spike, then the read side

- Type:    spike + feature
- Status:  done — read side delivered (D-2026-09-29-g); write deferred to #590
- Opened:  2026-09-29
- Owner:   Workshop
- GitHub:  #582 (increment 6 of 6)

## Requirement authority

REQ-362 (accepted 2026-09-29, D-2026-09-29-b). Item 3: the increment opens with a spike proving a
GEODATA written into GoSurvey's DWG is read by AutoCAD / Civil 3D; if not, work stops and a SPEC GAP
is raised.

## Method

Standalone C programs against the vendored LibreDWG 0.13.4 (scratch, not committed), and AutoCAD
2027's `accoreconsole.exe` driven by LISP scripts (run from PowerShell: Git Bash rewrites `/s`).

## Findings

1. **Reading works.** LibreDWG decodes the GEODATA in `samples/duke-main-clean-r2018.dwg` (a Civil 3D
   drawing): class version 2, projected grid, design point (1846238.730, 13629548.130), reference
   point lon −99.383333 / lat 29.225, horizontal and vertical unit US foot, north direction, scale
   estimation "grid scale at reference point". AutoCAD's own `entget` shows the same values.
2. **That Civil 3D GEODATA has no coordinate-system definition.** The definition string is empty in
   both LibreDWG and AutoCAD (`entget` 301 = ""), and AutoCAD's `CGEOCS` is "". The zone Civil 3D
   shows must live in Civil 3D's own settings objects, not in GEODATA. So REQ-362 item 1's "sets the
   zone from its coordinate-system definition" cannot be met for this file; the marker, north, units
   and scale method can.
3. **Writing: LibreDWG can build a GEODATA by hand** (`dwg_add_class` + `dwg_add_object` + an
   extension dictionary on `*Model_Space`), but in an R2000 file it encodes the **2009 (class version
   1) layout**: the vertical unit and the north direction are not stored.
4. **AutoCAD does not open GoSurvey's DWG files directly.** `accoreconsole /i` fails with
   ErrorStatus 53 = `eDwgCRCDoesNotMatch` for (a) a DWG saved by GoSurvey's real writer
   (`build/headless-out/req071-contour-extract/extracted-a.dwg`), (b) a bare LibreDWG R2000 file with
   no GEODATA, and (c) the spike file with a GEODATA. RECOVER opens (a); the recovered GEODATA spike
   file crashes AutoCAD's LISP at the first model-space lookup. The CRC problem predates REQ-362 and
   affects every GoSurvey DWG; whether the full AutoCAD GUI silently recovers was not checked.
5. The spike therefore does **not** prove AutoCAD / Civil 3D reads a GEODATA GoSurvey writes.

## Status log

- 2026-09-29: spike run; SPEC GAP raised to the user (write path unproven; Civil 3D GEODATA carries
  no zone).
- 2026-09-30: user accepted the trailer-clause wording fix (D-2026-09-30-a); read side implemented and tested.

## Plan (after D-2026-09-29-g, D-2026-09-30-a)

- Requirement authority: REQ-362 items 1-3 (accepted, narrowed to READ); REQ-358 item 5 (unknown
  code kept); REQ-359 item 4 (marker is WORLD); REQ-360 (Transform); REQ-201 (log what happened).
- IO (`src/io/LibreDwgCad.cpp`): `ReadDwgGeoData` copies the model-space GEODATA (else the first)
  into a plain `DwgGeoData`; called from the DWG body importer only, so a GoSurvey DWG with a
  trailer (ADR-044) never reaches it and DXF is untouched.
- Commands (`src/commands/CadCommands_Geo.cpp`): `ApplyDwgGeoData` sets the marker, north, zone
  (`GeoDataCoordinateSystemCode`: bare code or the XML `...CoordinateSystem id`), and the Transform
  with `apply` off; logs the result. No undo step: it is part of opening the file.
- BLOCKIMPORT (`src/commands/CadBlocks.cpp`) reads a DWG into a scratch drawing; the GEODATA log
  lines describe the scratch, so they are dropped there.
- Architecture check: IO copies raw values, the command layer interprets them; no new dependency,
  no new abstraction, CS-MAP only through `src/geo/`.

## Completion report

- Tests (`tests/GeoDataReadTests.cpp`, GoSurveySnapTests, `[req362]`, 5 cases / 58 assertions, pass):
  definition parsing; the Civil 3D sample (marker 1846238.730, 13629548.130; north 89.8122 deg;
  Reference Point; no zone; "names no coordinate system" logged); XML-id and bare-code zones with
  AG9976's grid reference within 0.01 ft; unknown code kept verbatim; north wrap and fallbacks;
  trailer DWG reopens with identical settings and no GEODATA log; a body-only DWG opens at the
  defaults; BLOCKIMPORT leaves the drawing's location alone.
- Build: `./dev/build` clean. `./dev/test`: 1991/1998 pass; the 7 failures are the headless
  transcripts already failing on beta (offset / isolines / surface-selection / feature-line /
  solid-primitives / issue233), none touch DWG or geolocation.
- Assumptions: GEODATA `coord_proj_radius` is meters and `sea_level_elev` is drawing units, as the
  Transform stores them (AutoCAD's documented units).
- Technical debt: none new. The write side waits for #590.
- Manual check: none needed for read; the write-side AutoCAD check moves to #590.
- Final review (PR #591): GEODATA coordinate type 2 (projected grid) was read as longitude /
  latitude and projected a second time. Its reference point is already easting / northing in the
  zone's unit, so it is now stored as the grid reference directly. Type 3 (geographic) is still
  projected, and types 0 / 1 give no grid reference. There are two new `[req362]` sections, and
  `./dev/test` still shows the same 7 failures that are already on beta.
