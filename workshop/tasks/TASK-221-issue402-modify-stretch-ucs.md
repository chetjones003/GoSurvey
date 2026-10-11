# TASK-221 — REQ-329 increment 4: STRETCH's crossing box and displacement in the UCS plane

- Type:    feature (increment 4 of an accepted cross-cutting requirement)
- Status:  done — PR pending
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #402

## 1. Authority

- REQ-329 (accepted 2026-09-08, D-2026-09-08-b) — increment 4 of 7 (STRETCH).

## 2. Problem

STRETCH captured its crossing box as world-XY min/max of the two drag corners and moved vertices
by a 2D `(dx, dy)`. On a tilted work plane both corners project to nearly the same world Y, so the
box collapses to a sliver and STRETCH is effectively unusable; and the displacement never carried Z.
REQ-103 step 5 flagged STRETCH's box as "load-bearing geometry" — this increment makes it
work-plane-aware.

## 3. Scope

IN:
- Base / second points captured with elevation and read in the active UCS axes — `HandleStretchText`
  now reuses `ResolveTypedModifyPoint` (the MOVE/COPY typed helper); the viewport-pick path captures
  `modifyBaseZ` from `CadCommitElevation`. → a 3D `(dx, dy, dz)` displacement.
- `AppCommandState::stretchRectInUcsPlane` (new bool). When the work plane is tilted, `finishBox`
  stores the crossing box in the plane's local 2D frame — each corner's world Z rides on
  `selBoxAnchorZ` / `uiCursorWorldZ`, so `ucs::WorldToPlane` recovers its `(u, v)`.
- `ApplyStretchToSelection` gains `dz` and `rectInUcsPlane`; its `inBox` helper projects each
  candidate vertex through `ucs::WorldToPlane` before the rect test when `rectInUcsPlane`, and every
  moved position also gets `+= dz` (Line / Circle / Arc / Ellipse / Polyline / Annotation / Table /
  FilledRegion / feature-line vertex / survey-point elevation).
- Headless: the `CLICKUCS` selection-box path sets `selBoxAnchorZ` / `uiCursorWorldZ` from
  `resolvedPointZ` (test infra) so a crossing box drawn on a tilted plane round-trips.

OUT (increment-4 boundaries, recorded in REQ-329):
- A tilted arc degrades to a whole-arc move when its centre is in the box (`StretchOneArc`'s
  `cx + r*cos` endpoint math is planar — its own pre-existing 2D simplification).
- Entity *candidacy* on a **non-orbited** tilted UCS still uses `ComputeSelectionFromRect`'s flat
  world-XY test — a pre-existing limitation of that function (its screen-space path already handles
  the orbited case), not introduced here.
- PDF underlay carries no elevation (positions only) — unchanged.
- Paper-space STRETCH stays 2D (paper is 2D by definition).

## 4. Files

- `src/commands/CadCommands.hpp`: `stretchRectInUcsPlane` field; `ApplyStretchToSelection` decl.
- `src/commands/CadCommands.cpp`: `finishBox` STRETCH branch (world-XY vs UCS-plane box),
  `HandleStretchText` (typed 3D via `ResolveTypedModifyPoint`), the STRETCH viewport-pick
  `NeedBase` / `NeedDestination`, `ApplyStretchToSelection` (`dz` + `rectInUcsPlane` + per-site Z).
- `tests/headless/HeadlessDriver.cpp`: `CLICKUCS` box-corner Z capture.
- `tests/headless/transcripts/issue402-stretch-ucs.txt` (new).

## 5. Regression guard

- `stretch-lines-polylines.txt`, `stretch-circle-ellipse-arc.txt` pass unchanged.
- New transcript's World-UCS and plan-rotated-UCS (Z 30) scenarios match the pre-change numbers
  (`dz` = 0, world-XY box).
- `req087-feature-line-modify`, `regression-119-variant-coverage`, `issue124-blocks`, the ARRAY
  transcripts, and the increment 1-3 transcripts pass unchanged.
- Full headless corpus green; full `dev/test` — see completion report.

## 6. Tests

`tests/headless/transcripts/issue402-stretch-ucs.txt`: World-UCS regression; tilted UCS (X 90) —
a line endpoint in the crossing box moves by the 3D displacement; a vertex outside the box in the
work-plane frame (but inside the flat world-XY candidacy) is left alone; a polyline where only the
in-box vertices move in 3D; plan-rotated UCS (Z 30) regression.

## 7. Verification

- `build-project`: PASS (release).
- `testing`: PASS — see §5.
- `architecture-review`: one new `AppCommandState` bool + a widened Commands-layer function
  signature + a test-infra Z capture. Same shape as REQ-329 increments 1-3.
