# TASK-281 — an ellipse is drawn, selected and gripped in the plane it stands in

- Type:    fix
- Status:  review
- Opened:  2026-09-24
- Owner:   Workshop
- GitHub:  #531 (completes its acceptance), user report 2026-09-24

## Requirement authority

REQ-312 (a curve carries its own plane), REQ-058 (ray picking, the work plane, the two selection
funnels), REQ-201/REQ-204. No requirement changed — this delivers what #531's acceptance already
states.

## Why

#531 was left open when #515–#522 were closed, because its second acceptance line was not met:

> `ELLIPSE` on a UCS turned 90° about X makes an ellipse standing in that plane. Its **snaps, grips
> and pick** land on the drawn curve.

Snaps landed with PR #556. Audited on `beta` @ `72bb3108`, the other three did not, and the user had
already hit the middle one in the app:

> "the only problem seemed to be that i could not accurately select the section"

## What was wrong

**The command never set a plane.** `CommitEllipseDraft` set only `z = CadCommitElevation(st)` and
never touched `nx/ny/nz`, so `ELLIPSE` on any turned UCS still made a flat ellipse at an elevation.
`CommitCircle` had done this correctly for circles since REQ-312.

**Both selection funnels read an ellipse flat and whole.** `PickClosestCadEntity` and
`SampleEllipseWorld` → `ComputeSelectionFromRect` ignored the plane normal (#531) and the drawn span
(the #520 elliptical-arc increment). Measured on the drawing the app itself wrote — a 30-radius pipe
cut at 45° — the arc is a circle of radius 30 in plan while selection hunted at **42.4**: a
**12.4-unit** miss no pick aperture closes, and the 141° the cut removed still selected it from empty
space.

**The highlight contradicted the pick.** `appendEllipsePolylineStrip` drew flat and whole, so a
correctly-picked arc lit up as a complete oval on the datum.

**Grips disagreed with themselves.** `CadUi.cpp` declined to draw grips for a tilted ellipse while
the grip hit test still offered them — an invisible handle, sitting where a flat ellipse's would be,
deforming the section when grabbed.

## What changed

- `src/commands/CadEntities.hpp` — `EllipseHasGrips` / `EllipseGripPoints`: one definition of which
  grips an ellipse has and where they are. Four callers (the grip hit test in `CadCommands.cpp`, the
  hover hit test and both grip draws in `CadUi.cpp`), which is why it is a helper and not inlined
  four times — a handle drawn anywhere but where it is grabbed is worse than no handle.
- `src/commands/CadCommands.cpp`
  - `CommitEllipseDraft` commits into the active work plane, measuring the major axis **in** that
    plane, with `CommitCircle`'s own finite/degenerate-normal guard. Resolved before the undo
    snapshot, so a refusal leaves no empty undo entry — the shape the function's other refusals have.
  - `ellCz` / `ellMajEz`: each pick keeps the elevation it was made at (as `arcAz`/`arcBz` do), which
    is what makes a major axis on a vertical plane expressible at all.
  - `PickClosestCadEntity` and `SampleEllipseWorld` walk the drawn span through the ellipse's own
    plane, via `EllipseWorldPointAt`. The pick also measures against the **chords between** samples
    rather than the samples themselves — point sampling read a click squarely on the curve as up to
    half a step away, 0.8 units on the section oval.
  - `ApplyEntityGripPoint` takes the drag point's elevation and reshapes a tilted ellipse **in its
    own plane**; the flat path is untouched and byte-identical.
- `src/viewport/TransformPreview.cpp` — the selection highlight traces the curve that is drawn.
- `src/ui/CadUi.cpp` — the three grip sites go through the shared helper.

## Tests

- `GoSurveySnapTests [ellipsesel]` — 7 cases: the tilted arc picks on its own plane and not on its
  flattened shadow; the undrawn part of the span does not select while both ends of the drawn span
  do; the **orbited** (ray) metric, which is what the app uses once the view is turned and the only
  one that can see a fault in Z at all; a tilted whole ellipse, #531's own case; a flat whole ellipse
  exactly as before; no grip to grab on a part-drawn ellipse; and the highlight's geometry.
- `GoSurveySnapTests [ellgrip]` — 4 cases: `ELLIPSE` on the UCS the acceptance names stands in that
  plane; a flat `ELLIPSE` commits exactly as before; a tilted ellipse's three grips lie on the curve
  and are offered by the hit test at those positions and not at the flat ones; and each grip drag
  reshapes in-plane.
- `headless.ellipse-selection-plane-and-span` — the drag funnel end to end through a real section.
- Every one was run against the unfixed build first and fails there, with the reported symptom.
- Full suite: 1811/1819 — `beta`'s 7 headless failures plus `beta`'s own cone tessellation failure
  (`Tessellation agrees with the analytic figures and winds outward`, 256 of 768 cracked edges),
  which was confirmed pre-existing by building clean `beta` in a separate worktree.

## Caught by running it, not by reading it

The first selection transcript used `PICK` and could not fail: in a transcript `PICK` feeds command
input, and click-to-select lives in `CadUi.cpp`. A `PICK` directly on the chord line selected nothing
either, which is what exposed it. The drag funnel is driven with `BOX`; the click funnel is asserted
against `PickClosestCadEntity` directly.

## Not in scope

- **Endpoint grips for an elliptical ARC.** A part-drawn ellipse offers no grips: an axis endpoint
  need not lie on the drawn span, so a grip there is a handle in empty space — the exact fault this
  pass removed. Endpoint/centre semantics for an arc are their own slice.
- **A tilted CIRCLE's radius grip** drags by world-XY distance, so it reads the projected radius
  rather than the true one — the same shape of gap this fixes for ellipses, pre-existing since
  REQ-312 and left alone here.
- **Extents**: `ComputeWorldExtents` builds a flat XY box for an ellipse with no Z spread, so `ZOOM E`
  can under-frame a tall tilted oval.
- The layout-viewport draw walks a full turn, so a part-oval shows closed inside a paper-space
  viewport. Paper space is parked by the user until Phases 6 and 7 are done.
