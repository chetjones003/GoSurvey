# TASK-280 — an ellipse is selected where it is DRAWN, in its own plane and over its own span

- Type:    fix
- Status:  review
- Opened:  2026-09-24
- Owner:   Workshop
- GitHub:  follow-up to #531 and the #520 elliptical-arc increment

## Requirement authority

REQ-058 (ray picking and the selection funnels), REQ-312 (a curve carries its own plane), REQ-335.
No requirement changed: this restores behaviour the SPEC already states, which selection alone had
stopped delivering once an ellipse gained a plane and a span.

## Why

User report, 2026-09-24, after testing the part-oval section in the app:

> "the only problem seemed to be that i could not accurately select the section"

An ellipse gained two fields that selection never learned to read:

- `nx/ny/nz`, the plane it stands in (#531)
- `startRad/sweepRad`, the part of the turn it actually draws (the #520 elliptical-arc increment)

Render, snap, `.gs` and DXF all honour both. Both selection funnels — ADR-034 names them — sampled the
ellipse flat in world XY at `el.z`, over a full turn.

`EllipseWorldPointAt` (`src/commands/CadEntities.hpp`) already documents itself as *"the one place an
ellipse is turned into points, so render, pick and snap cannot disagree about where it runs."* Neither
funnel called it. `SampleEllipseWorld` still asserted the premise that made it unnecessary — *"An
ellipse is always parallel to XY"* — three weeks after that stopped being true.

### The size of the miss, on the drawing the app itself wrote

The part-oval section of a 30-radius pipe cut at 45 degrees stores centre on the axis,
`majV (0, -42.426)`, `ratio 0.7071`, `n (0, -0.7071, 0.7071)`, `sweep 219 degrees`. In its own plane
that curve is `x = 30 sin t, y = -30 cos t, z = -30 cos t` — in PLAN, a circle of radius 30. Selection
hunted for it at 42.4. **12.4 units**, which no pick aperture closes. And the 141 degrees the cut
removed still selected the section from empty space.

## What changed

- `src/commands/CadCommands.cpp`
  - `SampleEllipseWorld` — every point now comes from `EllipseWorldPointAt` over
    `EllipseSpanAngleAt`, so the drag funnel agrees with the renderer by construction rather than by
    two copies of the arithmetic staying in step. An arc is **not** closed: its ends are not joined,
    and a fence that closed it would test a chord nobody drew. Stale doc comment corrected.
  - Its caller in `ComputeSelectionFromRect` takes its segment count from the drawn span, as the arc
    branch beside it already does, so a short arc is not sampled more coarsely than a whole oval.
  - `PickClosestCadEntity`'s ellipse branch — same substitution, mirroring the tilted-arc branch
    directly above it (which received this fix for arcs and was not carried across to ellipses).
  - That branch now measures against the **chords between** samples rather than the samples
    themselves. Point sampling reads a click squarely on the curve but between two samples as up to
    half a step away — 0.8 units on the section oval — a miss on the very curve being pointed at. It
    is the metric the polyline branch already uses, and it makes the answer independent of the
    sample count.
  - `TryBeginEntityGripAtLocal` — no ellipse grips while the ellipse is tilted, matching
    `src/ui/CadUi.cpp`, which already declines to DRAW them (tilted grips are a later slice).
    Offering an invisible handle that sits where a flat ellipse's would be, and deforms the section
    when grabbed, is worse than offering none.

## Tests

- `GoSurveySnapTests [ellipsesel]` — four cases on the numbers the app actually saved: the tilted arc
  picks on its own plane and not on its flattened shadow; the undrawn part of the span does not select
  it while both ends of the drawn span do; a flat whole ellipse picks exactly as it always did; a
  tilted ellipse offers no grip, while a flat one still offers all three.
- `headless.ellipse-selection-plane-and-span` — the drag funnel end to end, through a real section:
  a box over the drawn curve takes it, a box over the flat phantom takes nothing, and a plain flat
  ellipse drags as before.
- Both were run against the **unfixed** build first and fail there, with exactly the reported
  symptom: the click on the curve misses, and the click 42.4 out selects.
- Full suite: 1784/1792 — `beta`'s 7 headless failures plus `beta`'s em-dash `PIPERUN` test.

## Caught by running it, not by reading it

The first transcript used `PICK` for click-to-select and could not fail: in a transcript `PICK` feeds
command input, and click-to-select lives in `src/ui/CadUi.cpp`. A `PICK` directly on the chord line
selected nothing either, which is what exposed it. The drag funnel is driven with `BOX`, and the click
funnel is asserted in Catch2 against `PickClosestCadEntity` directly.

## Not in scope

- **Grips for a tilted ellipse** — still none drawn and now none grabbable. Its own slice.
- **Extents** (`ComputeWorldExtents`) builds a flat XY box at `el.z` for an ellipse, with no Z spread,
  so `ZOOM E` can under-frame a tall tilted oval. Read, not measured; noted for its own task.
- The layout-viewport draw walks a full turn for an ellipse, so a part-oval shows closed inside a
  paper-space viewport. Paper space is deferred by the user until Phase 6 and 7 are done.
