# TASK-291 — Coordinate-system zone from the CS-MAP catalogue (geolocated drawings)

- Type:    feat
- Status:  review
- Opened:  2026-09-29
- Owner:   Workshop
- GitHub:  #582 (increment 2 of 6)

## Requirement authority

REQ-358 (accepted 2026-09-29, D-2026-09-29-b; item 1 narrowed by D-2026-09-29-d), ADR-063, REQ-357
(the window and `drawingSettings`), REQ-101 / local-storage invariant, REQ-201, REQ-300.

## Starting state (measured)

| Item | Before |
|---|---|
| Coordinate systems | none in the tree; the Zone group was one greyed "No Datum, No Projection" combo |
| CS-MAP | not vendored; GitHub mirrors stale (2019, r2842); OSGeo Subversion trunk is r3078 |
| Payload | dictionaries ~25 MB compiled; US grids ~225 MB; other countries ~140 MB; geoids larger |

## Decision taken during planning

**D-2026-09-29-d** (user, 2026-09-29): commit the data files to the repository, **US grids only**.
The spec did not say where the ~350 MB of grid files live before the installer packs them; git
history is permanent, so the user chose between commit-all / download-at-package / US-only / LFS.
While testing, CS-MAP's NADCON setup turned out to require the VERTCON catalogue (`Vertcon.gdc` +
three US files, ~3 MB), so those ship too (recorded in the same decision).

## Plan / architectural-boundary check

- **Build** (`third_party/csmap/`, `CMakeLists.txt`): headers, prebuilt Release `/MD` `csmap.lib`
  (upstream `Library.nmk`), `LICENSE`, `VENDORED.md`, compiled dictionaries + US grids.
  `CsMap::csmap` imported lib; the include dir is added to `src/geo/CoordinateSystems.cpp` ONLY
  (ADR-063 (c)), no PCH on it. Dictionaries staged to `build/resources/csmap/` by their own stamp
  (not re-copied on every resource edit); the installer already ships `resources\*`.
- **Geo** (`src/geo/CoordinateSystems.{hpp,cpp}`, new, pure): load/status, categories, systems per
  category, code lookup (description, projection, datum, unit, meters per unit), grid ↔ lat/long,
  datum shift (fatal on missing datum / out-of-grid — no silent fallback). No throws.
- **Commands**: `DrawingSettings::zoneCode` (+ `Geolocated()`), so undo, per-tab isolation and the
  Apply path of REQ-357 carry it unchanged. `DrawingPointToGrid` / `DrawingPointToLatLong`
  (REQ-358 item 4): local + origin in double → meters with the drawing's foot → the zone's unit.
- **IO**: `drawingSettings.zone` in the trailer JSON; unknown codes kept verbatim.
- **UI**: Zone group — Categories (`No Datum, No Projection` + the dictionary's list), Available
  coordinate systems (descriptions, loaded once per category), typeable code (Enter / leaving the
  field; unknown refused with an inline message), read-only Description / Projection / Datum; the
  whole group disabled with the load error when the dictionary is missing.
- **App**: `main.cpp` loads `resources/csmap` at startup; a failure is logged, not fatal.
- **Installer**: CS-MAP BSD notice + NGS public-domain grids on the licence page.

## Tests

- `tests/CoordinateSystemsTests.cpp` (`[req358]`, GoSurveySnapTests, loads the staged dictionary):
  bad folder reported; 246 categories with Lat Longs first; every Texas NAD27/NAD83/HARN/NSRS07/NSRS11
  zone; HARN/TX.TX-C details (LM, HARN/TX); TX83-CF found + its category; NOSUCH refused;
  **NGS PID AG9976** (UT Tower, NAD 83(1993)) grid ↔ lat/long in meters and US feet — round trip
  within 0.00001″ and 0.001 ft; **NAD27 → NAD83** via NADCON vs NGS NCAT within 0.20 m; drawing
  point → grid / lat/long in feet and meter zones, both foot definitions, unknown zone refused;
  zone = one undo step, moves no coordinate, trailer round trip incl. an unknown code; two tabs.
- `tests/LibreDwgCadTests.cpp`: zone survives DWG save → reopen (known and unknown code).
- Developer Shell `req358-zone-group`: the real window — typed codes, refusal, category pick, No
  Datum, Apply, Cancel.

## Completion report

- Build: `./dev/build` clean; `dev/build-devshell.bat` clean.
- Tests: `[req358]` 10 cases / 133 assertions pass; the DWG round trip passes; `ctest` 1959/1966 —
  the 7 failures are the headless ones already failing on `beta` (offset / isolines / surface
  selection / feature-line modify / command-name prompt), unrelated. Dev Shell `req358-zone-group`
  passes; a desktop screenshot confirmed the Zone group renders (USA, Texas → HARN Texas Central).
- Assumptions: a **Unitless** drawing is taken to be in the zone's own unit (no conversion);
  picking a category selects its first system (the staged value, so Cancel still discards it).
- Debt: CS-MAP opens files through narrow ANSI paths (an install folder the code page cannot hold
  fails the load, reported); Debug links the Release lib (as LibreDWG); REQ-360's transformation is
  not yet applied in `DrawingPointToGrid` (its increment adds it).
- Docs: `third_party/csmap/VENDORED.md`, `third_party/README.md`, installer licence, spec
  (REQ-358 item 1, ADR-063 (b), D-2026-09-29-d).
