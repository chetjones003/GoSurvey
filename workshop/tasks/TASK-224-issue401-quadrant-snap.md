# TASK-224 — REQ-330: Quadrant object snap for circles and arcs

- Type:    feature (new accepted requirement, single task)
- Status:  done — PR #428
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #401

## 1. Authority

- REQ-330 (accepted 2026-09-08, D-2026-09-08-d) — new Quadrant object snap.
- Related: REQ-062 (snap-set precedent), REQ-312 (`CurvePlane` — the curve's own plane the
  Center snap already resolves against for a tilted curve), issue #372 / PR #376 (orbited CENTRE
  uses the shape plane), issue #103 (Shift+right-click "snap once" override).

## 2. Problem

`CadSnap::Kind` has no `Quadrant`. The only circle/arc model-space snaps are Center /
GeometricCenter / SurveyCenter. Paper space computes quadrant points inline
(`src/commands/PaperSpace.hpp`) but model space has no equivalent.

## 3. Scope

IN:
- `CadSnap::Kind::Quadrant`; `Priority` = 2 (same tier as `Center`, per REQ-330).
- `AppCommandState::objectSnapQuadrant` (default `false` — AutoCAD OSMODE parity; no existing
  snap result changes unless the toggle is turned on); `.gs` (`GsIo.cpp`) + user-prefs
  (`UserPrefs.cpp`) round-trip.
- `CadSnap.cpp` `FindBest`: quadrant candidates for every standalone circle
  (`userCirclesCxCyZR` + `userCircleNormals`) and every standalone arc (`userArcs`).
  - Directions: project active-UCS (`cmd.activeUcs`) X and Y axes onto the curve's `CurvePlane`,
    normalise, step one radius along `±X`, `±Y`.
  - Degenerate (curve plane ⟂ UCS plane → projected axes near-zero or near-parallel): fall back
    to the curve plane's own local X/Y axes (`plane.xAxis` / `plane.yAxis`).
  - Arc: keep only quadrant angles inside `[startRad, startRad+sweepRad]` (wrap / negative-sweep
    aware).
  - Ordinary `Consider` acceptance (distance to the point), not the `Center` disk heuristic — a
    quadrant point is a specific point on the rim. This gives the orbited pick-ray path for free.
- `CadSnap::GatherAllSnapsOfKind` — a `Kind::Quadrant` case enumerating every circle/arc quadrant
  (for the snap-picker "cycle all of this kind").
- `ViewportRenderer.cpp` `BuildSnapOverlayLines` — `Kind::Quadrant` → green diamond
  (`AppendSnapDiamondOutline`), `solid=false` so it is 2D-green not 3D-purple.
- UI: `SnapKindLabelForUi` ("Quadrant"); Shift+right-click override menu entry; the OSNAP
  settings checkbox (`CadUiSettings.cpp`) and the quick OSNAP popup (`CadUi.cpp`).

OUT (documented follow-ons in REQ-330): ellipses, curved polyline segments, a NEAREST snap.

## 4. Files

- `src/viewport/CadSnap.hpp` — enum value + `Priority`.
- `src/viewport/CadSnap.cpp` — helpers (`QuadrantDirections`, `AngleWithinSweep`), circle + arc
  candidate generation in `FindBest`, `GatherAllSnapsOfKind` case.
- `src/commands/CadCommands.hpp` — `objectSnapQuadrant` field.
- `src/io/GsIo.cpp`, `src/io/UserPrefs.cpp` — persistence.
- `src/ui/CadUi.cpp`, `src/ui/CadUiSettings.cpp` — label, override menu, checkboxes.
- `src/render/ViewportRenderer.cpp` — glyph.
- `tests/.../CadSnapTests.cpp` — `[CadSnap][issue401]` cases.

## 5. Test approach

`CadSnapTests` driven through `FindBest` with a pick ray (the REQ-312 pattern):
- TOP + world UCS: N/E/S/W within REQ-101 of hand values.
- Plan view, UCS rotated 30° about Z: the four points rotate with the axes.
- Orbited camera, circle tilted (45° about X): all four reported points within REQ-101 of the
  radius from centre, and on the circle's plane.
- Circle plane ⟂ UCS plane: four distinct points, 90° apart in the circle frame (fallback).
- Arc 0°–90° sweep: only the 0° and 90° quadrants offered, not 180° / 270°.
- F3 master gate off → nothing; per-type toggle off but `onlyKind = Quadrant` → offered.

## 6. Architectural-boundary check

Pure viewport/snap + UI + IO wiring. No new entity, no geometry kernel change, no new dependency.
Reuses `CurvePlane` / `ucs::` exactly as the Center snap and REQ-312 curve walks do. Follows the
REQ-062 pattern for adding a snap kind.

## 7. Verification

- `dev/build` clean.
- `dev/test` — 1326/1327 green. The one failure, `a missing or corrupt store loads as an empty
  list` (test #887, crash exit `0xc0000409`), is pre-existing on `beta` and unrelated to snapping
  (a store-corruption serialization test). All six new `[CadSnap][issue401]` cases green; the six
  circle-rim CENTRE/Midpoint/Endpoint ranking tests that briefly failed while Quadrant defaulted
  ON pass again with the default OFF.
- Manual GUI pass handed to the user (glyph colour/shape, menu entry, checkbox).

## 8. Result

**PASS.** REQ-330 delivered. Quadrant defaults OFF (AutoCAD OSMODE parity); no existing snap
behaviour changes unless the toggle is enabled.
