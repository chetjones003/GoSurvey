# TASK-295 — GEODATA round-trip (REQ-362): feasibility spike

- Type:    spike
- Status:  blocked — SPEC GAP (REQ-362 item 3)
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
