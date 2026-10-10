# TASK-223 — REQ-329 increment 7: OFFSET's direction and side pick in the UCS plane

- Type:    feature (increment 7 of an accepted cross-cutting requirement — the last)
- Status:  done — PR pending
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #402

## 1. Authority

- REQ-329 (accepted 2026-09-08, D-2026-09-08-b) — increment 7 of 7 (OFFSET). Increment 6 (ALIGN)
  was closed as no-change (D-2026-09-08-c).

## 2. Problem

OFFSET's side / through pick used world XY (`SignedSideLine` / `SignedSideCircle` on the raw cursor),
and `CommitOffsetLine` offset the line by a world-XY-horizontal perpendicular keeping each endpoint's
own Z. On a tilted work plane the "which side" test is wrong and the offset copy slides along world
XY off the plane instead of staying parallel in it.

## 3. Scope

IN:
- `OffsetPlaneLocal(st, x, y, z)` (new file-local) — `ucs::WorldToPlane(CadActiveUcsStorage(st), …)`.
- `HandleOffsetSidePick` / `HandleOffsetThroughPick`: under a tilted UCS, project the cursor
  (`px, py, CadCommitElevation`) and the Line endpoints / Circle-Arc centre into the work-plane 2D
  frame before the `SignedSideLine` / `SignedSideCircle` test.
- `CommitOffsetLine`: under a tilted UCS, offset both endpoints by `signedD × normalize(UCS-Z × lineDir)`
  — the same left-normal handedness `UnitLeftNormal` gives, reducing to it exactly under the World
  UCS — so the copy stays in the work plane.
- Polyline / Ellipse offset is refused by name under a tilted UCS (`CommitOffsetPolyline`'s miter
  geometry is world-XY 2D; an ellipse carries no plane normal).
- `CadOffsetAppendLivePreview` returns empty under a tilted UCS — the ghost is built with flat `Ofs*`
  math and would sit where the click will not; suppressed rather than misleading.

OUT (documented follow-ons):
- Polyline offset in a tilted work plane (the miter-join solve re-based into the UCS 2D frame).
- Ellipse offset under a tilted UCS.
- A plane-aware OFFSET live preview.
- `CommitOffsetCircle` / `CommitOffsetArc` are unchanged — concentric offset is plane-preserving by
  construction (radius ± d, frame kept).

## 4. Files

- `src/commands/CadCommands.cpp` (`namespace OffsetCmd`): `OffsetPlaneLocal`, `CommitOffsetLine`
  (in-plane perpendicular + 3D endpoint Z), `HandleOffsetSidePick` / `HandleOffsetThroughPick`
  (plane-frame pick, Polyline/Ellipse refusal), `CadOffsetAppendLivePreview` (tilted-UCS suppress).
- `tests/headless/transcripts/issue402-offset-ucs.txt` (new).

No new `AppCommandState` field.

## 5. Regression guard

- `regression-122-offset-line-nan.txt`, `regression-58-offset-entity-id.txt` pass unchanged.
- New transcript's World-UCS and plan-rotated-UCS (Z 30) scenarios match the pre-change numbers
  (world-XY offset, elevation 0).
- The increment 1-5 transcripts and the ARRAY transcripts pass unchanged.
- Full `dev/test` — 1 failure (`a missing or corrupt store loads as an empty list`), pre-existing on
  `beta`, unrelated.

## 6. Tests

`tests/headless/transcripts/issue402-offset-ucs.txt`: World-UCS regression; tilted UCS (X 90) — a
line offsets into the work plane on both sides; a circle offsets concentrically staying on its own
plane; a polyline offset refused by name; plan-rotated UCS (Z 30) regression.

## 7. Verification

- `build-project`: PASS (release).
- `testing`: PASS — see §5.
- `architecture-review`: one new file-local helper + branches in the existing OFFSET commit/pick
  functions. No layer change, no new state. Same shape as REQ-329 increments 1-5.
