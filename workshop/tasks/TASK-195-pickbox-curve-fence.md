# TASK-195 — a selection fence tests the CURVE, not a box drawn round it

## Bug report

Reported by the user with six screenshots, 2026-09-03.

| Field | Value |
|---|---|
| **Title** | Crossing box selects a circle it never touches when the view is orbited |
| **Observed** | Off plan view, a small crossing box sitting in empty space clear of a circle selects it; clicks and boxes near the circle feel arbitrary |
| **Expected** | A crossing box selects a circle only when it touches the drawn circle; a window box only when the whole circle is inside |
| **Repro** | Orbit off plan (ViewCube showing TOP/FRONT/RIGHT), draw a circle, drag a right-to-left box in the empty space diagonally off its rim |

Two of the six screenshots turned out to be the program behaving correctly, and are recorded here so
they are not "fixed" later by someone reading the report alone:

- the blue box partly covering the circle is a **window** box (dragged left→right), and a window box
  is defined to select only what it fully encloses — AutoCAD does the same;
- the click that missed was ~20 px clear of the curve, outside the pick aperture.

What made those two read as random was the false hit in the first pair.

## Requirement authority

- **REQ-058** (orbitable 3D camera with ray picking) — the accepted requirement that governs what
  happens to picking once the camera leaves plan view. Its statement is that picking *becomes a
  ray/projection test*, not that it becomes an approximate one, and its acceptance is written around
  **every entity type**, not lines alone.
- **REQ-039** acceptance (1) states the rule this restores in so many words: *"a window box (L→R)
  selects paper objects fully inside it and a crossing box (R→L) selects any it touches, for every
  paper object type"* — written for paper space explicitly as parity with model space.
- **REQ-316 / ADR-047** already applied exactly this rule to the curved part of a polyline: a
  crossing box over a bulge, clear of every chord, selects the polyline. The fix makes circles, arcs
  and ellipses obey the rule the project had already adopted for the same shape.

Not a SPEC GAP: the correct behaviour is stated, and the plan-view circle path already implemented
it (`CircleIntersectsAABB` — *"use the circle curve, not the filled disk"*).

**One scope note against REQ-312.** REQ-312's status paragraph scopes out *"a tilted circle's
box-selection and DXF-header bounds staying the conservative `cx ± r` square"*. That scope-out is
untouched: it is about a **tilted** circle's bounds in the **plan-view / extents** path, which this
task does not modify. The plan-view fence keeps its exact analytic circle tests.

## Root cause

`ComputeSelectionFromRect` (`src/commands/CadCommands.cpp`) tested three entity types against an
**axis-aligned box** instead of against their geometry:

| entity | box it was tested against | where |
|---|---|---|
| Circle | projected bound of the enclosing world square `[cx±r, cy±r]` | orbited view only |
| Arc | `ArcRoughBounds` AABB | **every** view |
| Ellipse | `EllipseRoughBounds` AABB | **every** view |

A box is wrong in two directions at once, and both were user-visible:

1. **It reaches past the curve.** A circle's enclosing square has corners at `√2·r`, so about 41% of
   the box lies outside the circle. That is the reported false hit exactly: the box in the
   screenshot sits in the corner region, outside the rim, inside the square.
2. **It is solid where the curve is hollow.** Crossing hits on any overlap, so a fence floating in
   the empty middle of a circle, arc or ellipse selected it too.

Orthographic projection is affine, so a point outside the circle projects outside the projected
ellipse — the correct answer does not depend on the view angle, which is what makes the assertions
below identical in plan and orbited views.

## Evidence

Before the fix, driven through product code by `gosurvey_headless`:

```
# circle r=100 at origin, VIEWANGLES -45 35, crossing box at (75,75)-(85,85)
# |(75,75)| = 106 > 100, so the box lies entirely outside the circle
FAIL [expect] step 29 (line 67): SELECTED: expected 0, got 1

# arc: semicircle r=100, PLAN VIEW, crossing box at (-90,5)-(-80,15)
# inside the bounding box, ~30 units clear of the arc
FAIL [expect] step 8 (line 9): SELECTED: expected 0, got 1

# ellipse: semi-major 100 / semi-minor 30, PLAN VIEW, crossing box at (85,20)-(95,28)
# inside the bounding box; the curve has come down to y = 13.1 by x = 90
FAIL [expect] step 7 (line 8): SELECTED: expected 0, got 1
```

## The fix

One shared test, `CurveHitsRect`, inside `ComputeSelectionFromRect`: walk the curve's own
tessellation, project each vertex through the same `SP` mapping every other entity uses, then ask the
same two questions the line loop asks — window wants every vertex inside the rect, crossing wants any
**segment** touching it.

- **Circle** — orbited only. Sampled in the circle's own plane (`CurvePlane` / `SampleCurveWorld`,
  REQ-312), so a tilted circle is tested where it actually is. Plan view keeps
  `CircleIntersectsAABB` / `CircleFullyInsideRect`: those are exact closed forms, and a tessellation
  would be a step backwards from them.
- **Arc** — every view. It had no analytic fence test to lose.
- **Ellipse** — every view. Its box was the worst of the three: an ellipse fills π/4 of its bounding
  box and none of its middle.

`ArcRoughBounds` and `EllipseRoughBounds` existed only to feed this box test and are **deleted**;
`SampleEllipseWorld` replaces the second, as the counterpart of the existing `SampleCurveWorld` for
the one authored curve that is not circular.

Tessellation is π/24 per step (48 for a full circle), the same choice `ChainHitsRect` already makes
for a polyline bulge, so the two curve fences cannot disagree. Chord departure is `r·(1−cos 3.75°)`
= `0.0021·r`, under a pixel for any circle that fits on screen.

### A second defect on the same symptom

`src/ui/CadUi.cpp` — the model-space **hover** pick called `PickClosestCadEntity` without the cursor
ray while the **click** two hundred lines below passed it, under a comment claiming *"Same ray the
hover used, so what highlights is what selects (REQ-058)"*. Off plan view those two metrics disagree:
the plan-view XY distance over-measures along the foreshortened screen direction, so geometry the
click would happily take highlighted on one side of the cursor and not the other. One argument; the
comment is now true.

## Files

| file | change |
|---|---|
| `src/commands/CadCommands.cpp` | `CurveHitsRect` + `CurveSegmentCount`; circle (orbited), arc and ellipse branches rewritten; `ArcRoughBounds`/`EllipseRoughBounds` deleted, `SampleEllipseWorld` added; the stale "conservative box" note narrowed to the box-shaped entity types it still describes |
| `src/ui/CadUi.cpp` | hover pick takes `cursorRayPtr`, like the click |
| `tests/headless/transcripts/pickbox-curve-fence-is-the-curve.txt` | new — the regression test |
| `tests/headless/transcripts/stretch-circle-ellipse-arc.txt` | T3's box widened; T3/T4 prose corrected (see below) |

## Test approach

`tests/headless/transcripts/pickbox-curve-fence-is-the-curve.txt`, 176 steps. Every MISS is paired
with a HIT at the same view angle — a fence that selected nothing would pass every miss on its own —
and every case takes a fresh `NEW` drawing, because a box selection accumulates and a miss run after
a hit cannot be told from a hit. Covered: circle in plan view (the control), circle at two orbit
angles, window mode both ways, arc and ellipse in plan **and** orbited, and a line asserted unchanged
so a change to the shared fence cannot trade one entity type's correctness for another's.

Assertions negative-tested against the code that makes them pass: the three misses each failed on
`beta` (output above), and each HIT box was re-run nudged clear of its curve and off the rim from the
inside, confirming the new test is not simply permissive.

## One existing transcript changed, deliberately

`headless.stretch-circle-ellipse-arc` failed after the fix. Its T3 comment read:

> *"Ellipse candidacy is a plain bounding-box overlap test (looser than Circle's curve test), so a
> box entirely inside its rough bounds still selects it."*

That is the defect, written down as the expected behaviour. T3's box `(695,-5)-(705,5)` lies entirely
**inside** the ellipse and selected it only because of the bug. The box is now `(695,-15)-(705,15)` —
still capturing the centre, which is what T3 is about, and now genuinely straddling the curve, the
same shape T1 already used for the circle. T4's arc box already touched the curve at `(910,0)` and is
unchanged; only its stale prose was corrected. No assertion was weakened and nothing was skipped.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean.
- **testing** — PASS. `ctest`: **1064/1064**. New transcript fails on `beta`, passes after.
- **architecture-review** — PASS. No layer moved. The fence stays one function in the Commands
  layer; curve sampling continues to go through `CurvePlane`/`CurvePointAt` (REQ-312's single-frame
  rule) rather than a second parametrisation. Net −40 lines.
- **code-review** — PASS. Two dead functions removed rather than left behind; one test helper added
  with one caller (a moved function, not a new abstraction).
- **performance-review** — PASS by inspection. `ComputeSelectionFromRect` runs once per box
  selection, not per frame; ≤96 projected points per curve. The deleted `ArcRoughBounds` walked the
  arc at comparable cost to build its box, so this is close to cost-neutral and outside REQ-100.

## Not covered by test, stated plainly

The `CadUi.cpp` hover change is UI-layer state (`viewportHoverEntityValid`) that no transcript can
reach — the driver's `HOVER` verb resolves a pick but does not run the viewport's hover block. GUI
verification only, same standing as TASK-193's viewport/plot paths.

## Technical debt / follow-ups (not fixed here)

### Paper space has the same defect, in every type — audited, confirmed, deferred

Asked for and carried out after this fix was pushed. `SelectPaperEntitiesInBox`
(`src/commands/PaperSpace.hpp:373`) tests **every** paper type by bounding box. Seven probes run
against it, all seven failed:

| type | what it tests | probe (crossing box) | got |
|---|---|---|---|
| Line | bbox of the endpoints | box `[8,1]-[9,2]`, ~5 in clear of the line `(0,0)-(10,10)` | selects |
| Circle | enclosing square `cx ± r` | box `[7.5,7.5]-[8.5,8.5]`, 10.6 from an r=10 circle | selects |
| Circle | same, and solid | box `[-1,-1]-[1,1]` in the hollow middle | selects |
| Arc | square of the **whole circle**, sweep ignored | 90° arc in the upper-right quadrant, box in the lower-**left** | selects |
| Ellipse | square of side 2·major, **`ratio` ignored** | semi-minor 1, box at `y ∈ [5,6]` | selects |
| Polyline | bbox of all vertices | L-shape `(0,0)→(10,0)→(10,10)`, box in the empty upper-left | selects |
| Block | the insertion **point** only | box over the block's geometry, not its insertion point | **misses** |

Worse than what this task fixed, in two ways. **Lines and polylines are affected**, which they never
were in model space (`SegIntersectsAABB` and `ChainHitsRect` were always exact there). And **arc and
ellipse ignore their own shape outright** — the arc uses the full circle's square whatever its sweep,
the ellipse ignores `ratio` entirely. The block is the mirror-image defect: too small, not too big.

The click path (`PickPaperEntityAt`) is already curve-correct for circles, and viewports are already
handled correctly inside `closePaperSelBox` (the hollow-rectangle rule, issue #4). The box path is
the outlier.

**Deferred to its own PR after this one merges**, on the user's call (2026-09-03): different file,
different requirement path (REQ-039 acceptance (1)), and it changes an existing unit test —
`tests/PaperSpaceTests.cpp:315` asserts the hollow-circle behaviour as correct (box `[9,9]-[11,11]`
lies entirely inside a circle at `(10,10)` r=2 and is required to select it), the same
defect-written-down-as-intent as this task's STRETCH transcript. Blocks are to be fixed too, against
their real geometry via the world AABB model space already uses (`CadBlockWorldAabb`) — confirmed
with the user rather than assumed, since it changes selection in the opposite direction.

### Model-space box-shaped entities

**Tables, annotations, block references and PDF underlays** are still tested by their bounding box.
For those the box IS the footprint, so this is correct today — but a block whose content is a small
circle in a large empty extent has the same shape of problem, and no accepted requirement currently
says which answer is wanted.
