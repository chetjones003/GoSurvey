# TASK-293 — Drawing Settings Transformation tab (local ↔ grid)

- Type:    feat
- Status:  review
- Opened:  2026-09-29
- Owner:   Workshop
- GitHub:  #582 (increment 4 of 6)

## Requirement authority

REQ-360 (accepted 2026-09-29, D-2026-09-29-b), REQ-358 item 4 (`DrawingPointToGrid` /
`DrawingPointToLatLong`), REQ-359 (markers / Mark Position use the conversion), REQ-357 (window,
Angular units, Imperial to Metric factor), REQ-021 (typed angles are CW from north), REQ-101 /
local-storage invariant (world = local + origin, in double), REQ-084 (*not implemented yet*),
REQ-201, ADR-063 (CS-MAP only behind `src/geo/`), ADR-044 (trailer).

## Plan / architectural-boundary check

- **`src/geo/` (pure)**: new `LocalGridTransform.hpp/.cpp` — `grid = G_ref + k·R(θ)·(L − L_ref)`
  and its exact inverse, `SeaLevelScaleFactor(R, h) = R/(R+h)`, rotation from a point pair
  (coincident refused). No CS-MAP, no drawing state. `CoordinateSystems` gains
  `GridScaleFactor(code, e, n)` (CS-MAP `CS_cssck` at the grid point) and
  `EllipsoidSemiMajorMeters(code)` (the Spheroid radius default).
- **Commands (data)**: `DrawingSettings::Transform` (apply flag, sea level on/elevation/radius,
  computation + user factor, reference point WORLD local + grid + point number, rotation mode
  {Rotation point, To north, Azimuth} with its values). Angles stored in degrees whatever the
  Angular units (the UI converts). Rides the existing undo / per-tab / trailer paths of `DrawingSettings`.
- **Commands (maths)**: `DrawingPointToGrid` / `GridToDrawingPoint` both go through one pair
  `DrawingWorldToGrid` / `GridToDrawingWorld(settings, insUnits, …)`: unit factor, then the
  transform when applied. `GridToDrawingPoint` stops relying on "the conversion is a pure scale".
  `ResolveDrawingTransform` gives the UI its readouts (k_grid, k_sea, k, θ) and the conversion its
  numbers. `ValidateDrawingTransform` refuses bad input at Apply (factor ≤ 0, R ≤ 0, R + h ≤ 0,
  coincident rotation point). Clearing the zone resets the transform (as it resets the marker).
- **Commands (pick)**: `Kind::DrawingSettingsPick` (`DSPICK`, internal, started only by the tab's
  pick buttons): one snapped point (reference / rotation point; a survey point there gives its point
  number) or two points (a direction: To north / Azimuth). Wired into the six places a point command
  appears (click route, dyn-input list, prompt, footer hint, typed point, cancel). The window hides
  while picking and comes back with its staged values; Esc returns with no change.
- **UI**: the Transformation tab (was greyed): zone description, *Zone units are in …*, Apply
  transform settings (disabled with no zone; gates every control below), Sea Level Scale Factor,
  Grid Scale Factor, Reference Point, Rotation (radio Rotation point / Specify grid rotation angle →
  To north / Azimuth), Rotation Point. Fixed window size measured for the taller tab.
- **IO**: trailer JSON `drawingSettings.transform` (additive; absent → defaults).

## Tests

- `tests/TransformationTests.cpp` (`[req360]`, GoSurveySnapTests): transform off = world × unit
  factor; hand case (k = 0.9999, θ = 1°) forward within 0.0001 ft and inverse within 1e-9 relative;
  CS-MAP k at NGS AG9976 vs datasheet 0.99995905 within 1e-8; `k_sea(20,906,000 ft, 1000 ft)`;
  coincident rotation point refused (pure + Apply); To north / Azimuth / Rotation point give the
  same θ; markers (Mark Position) use the transform; clearing the zone resets it; one undo step;
  trailer JSON round trip; DSPICK sets the staged reference point + point number, Esc cancels.
- `tests/LibreDwgCadTests.cpp`: transform survives DWG save → reopen.
- Dev Shell: the tab's controls disabled until Apply transform settings is checked.

## Status log

- 2026-09-29: planned. PR #587 (REQ-359) merged first by the user's choice; branched from beta.
- 2026-09-29: implemented. Self-review fixes: the internal pick no longer becomes the Enter-repeat
  command; conversions skip the CS-MAP factor lookups while the transform is off; the Esc that
  cancels a pick no longer also closes the window it brings back.

## Completion report

- Build: `./dev/build` clean; `dev/build-devshell.bat` clean.
- Tests: `[req360]` 8 cases (7 in `tests/TransformationTests.cpp` + the DWG round trip in
  `tests/LibreDwgCadTests.cpp`) pass; `[req357]`–`[req360]` together 38 cases / 458 assertions pass.
  CS-MAP's scale factor at AG9976 agrees with the NGS datasheet's 0.99995905 within 1e-8 (meter and
  US-foot zones). `ctest` 1977/1984 — the 7 failures are the headless ones already failing on `beta`
  (TASK-291/292), unrelated. Dev Shell `req360-transformation-tab` passes in the real window: every
  control disabled until *Apply transform settings*, Elevation waits for the sea level checkbox, a
  pick hides and restores the window, Esc returns without closing it, Apply stores the transform.
- Acceptance: transform off = world in zone units ✔; hand case (k = 0.9999, θ = 1°) within
  0.0001 ft and back within 1e-9 relative ✔; Reference Point k vs datasheet within 1e-8 ✔;
  k_sea(20,906,000 ft, 1000 ft) = 0.999952169 ✔; coincident rotation point refused (pure, Apply,
  and the pick) ✔; DWG save → reopen keeps every setting ✔; controls disabled until Apply transform
  settings ✔.
- Not verified by me: how the tab LOOKS (column alignment, clipping at other font sizes) — the
  capture tools cannot show a modal reliably; handed to the user.
- Assumptions (none change the SPEC's meaning):
  - θ is counter-clockwise in the formula; *To north* is entered clockwise from local north to grid
    north (REQ-021's entry convention), so θ = To north; *Azimuth* gives θ = local − grid azimuth.
  - *To north*'s pick is two points along grid north as drawn; *Azimuth*'s pick is two points along
    the local line.
  - A picked reference/rotation point's grid pair starts as the point in zone units (no transform);
    the user types the real grid coordinates over it.
  - Spheroid radius 0 is stored as "the zone ellipsoid's" (6,378,137 m for GRS 80 zones).
  - Clearing the zone (No Datum, No Projection, or Remove Location) resets the transform, as it
    resets the geographic marker: it only means something in a zone.
  - A Unitless drawing's elevation is in the zone's unit (the same rule REQ-358 uses for X/Y).
- Debt: no rubber-band preview during the two-point direction pick; the window is one fixed size for
  both tabs (wider than Units and Zone needs).
- Docs: REQ-360 status links this task; this task.
