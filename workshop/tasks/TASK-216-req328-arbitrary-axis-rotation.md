# TASK-216 — REQ-328: general arbitrary-axis rotation primitive + Polar ARRAY consumer

- Type:    feature
- Status:  done — PR pending
- Opened:  2026-09-07
- Owner:   Workshop
- GitHub:  #400 (scope addition)

## 1. Authority

- REQ-328 (proposed 2026-09-07, D-2026-09-07-d).
- REQ-305 acceptance 11 (amended — tilted-UCS refusal superseded).

## 2. Scope

IN:
- `ray3d::RotateVectorAboutAxis` / `ray3d::RotatePointAboutAxis` in `src/util/ray3d.hpp`.
- `RotateSelectionAboutAxis(st, axisPoint, axisUnit, angleRad, log)` in `CadCommands.cpp`: Line,
  Polyline, FeatureLine, FilledRegion (bare points — full 3D, no gap); Circle/Arc/Ellipse (centre +
  plane normal rotate, Arc re-anchors start point); Annotation/Table/BlockRef refused under a
  non-Z-parallel axis, with a named log line.
- Rebuild `CommitArrayPolar` on this primitive; lift the tilted-UCS refusal in `HandleArrayText`'s
  'p'/'polar' branch (still refuse Annotation/Table/BlockRef specifically when tilted).

OUT (explicit follow-on, not this task):
- Solid rotation (`brep::Solid` about an arbitrary axis).
- 3D ROTATE / 3D SCALE typed commands.
- MOVE gizmo rotate/scale handles.

## 3. Files

- `src/util/ray3d.hpp`: two new pure functions.
- `src/commands/CadCommands.cpp`: `RotateSelectionAboutAxis` (new), `CommitArrayPolar` (rebuilt),
  `HandleArrayText`'s Polar refusal (loosened to Z-parallel check only for the entity-type subset
  that still needs it).

## 4. Regression guard

`RotateVectorAboutAxis`/`RotatePointAboutAxis` with axis (0,0,1) through the world origin must
reproduce `RotateAroundBase`'s output bit-for-bit — a unit test, not just a transcript, since this
is the primitive every other guarantee rests on.

## 5. Tests

- New Catch2 test file (or addition to an existing ray3d test file) for the two primitives:
  Z-axis-through-origin regression, a known tilted-axis rotation checked by hand, vector vs point
  distinction (a vector rotation ignores `axisPoint`).
- New headless transcript: Polar ARRAY under a UCS tilted 90 degrees about X (previously refused)
  now arrays a Line/Circle/Arc/Polyline/Ellipse selection correctly; the same selection with an
  Annotation added still refuses the annotation by name while arraying the rest; Polar ARRAY under
  a Z-parallel UCS (translated/rotated in-plane) is unchanged (existing issue400-array-ucs-plane.txt
  transcript must still pass byte-for-byte).

## 6. Verification

- `build-project`, `testing`.
