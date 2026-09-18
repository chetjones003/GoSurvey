# TASK-268 — issue #520: a sphere sections as a circle, a torus as a ring, and a section may be several outlines

- Type:    feat
- Status:  review
- Opened:  2026-09-18
- Owner:   Workshop
- GitHub:  #520

## Requirement authority

REQ-314 (`Slice`) and REQ-335 (`SECTION`, increment 2), both revised on this date, under
D-2026-09-18-a. REQ-101 (±0.002 ft) and REQ-201 (a refusal names its reason).

## Why

Found by the 2026-09-16 section-coverage probe. `SECTION` and `SLICE` refused every cut of a sphere or
a torus ("flat faces only"), though a sphere's cut is a circle and a torus cut square to its axis is
two concentric circles. The second could not be drawn at all: a section was one closed outline.

## Decision (D-2026-09-18-a)

Put to the user, both decided as recommended:
1. A ring-shaped section draws **one closed polyline per outline**, in one undo step, rather than one
   polyline bridging the two.
2. **Scope**: sphere, ring outlines, torus square to its axis, and named refusals. A torus cut through
   its axis (two separate circles) is refused by name and left to a follow-up.

## What changed

- `src/util/brep.cpp`:
  - `SliceSpherePrimitive`: a sphere at any plane strictly inside it gives two caps. Each is built in
    a frame whose +Z is that cap's own side, so one builder serves both: a `Sphere` face over
    `vCut..pi/2` in two halves, two meridian seams, and the flat disc of the cut.
  - `SliceTorusSquareToAxis`: a torus cut square to its axis, with the plane through the tube, gives
    two pieces. Each carries `Torus` faces over `vCut..pi - vCut` and a flat **ring** face — an outer
    loop plus a hole wound the other way. Any other angle, or a tube as wide as its ring, is
    `SliceCutTorusCurve`.
  - `RoundRecipeFitsSolid`: a `Sphere` or `Torus` recipe is used only when the primitive it names has the solid's measured shape (the #526 rule, extended to the round primitives). A tube as wide as its ring is refused by name first, having no measured shape to vet.
  - `SectionOutlines` (new, public): every closed outline of the cut, outer wound CCW about the
    section normal and holes CW. `SectionLoop` is now a wrapper on it for the single-outline case,
    naming `SliceCutSeveralOutlines` (separate faces on the plane) or `SectionHasHole`. The loop →
    `Path` conversion is shared by both.
- `src/util/brep.hpp`: `Problem::SliceCutTorusCurve` with its own text, and the `SectionOutlines`
  contract.
- `src/commands/CadCommands.cpp`: `CadSectionSolidsByPlane` sections through `SectionOutlines` and
  appends one closed polyline per outline, under the single undo snapshot it already took. The
  message counts outlines, so a ring reports "2 section outlines created".

## Tests

- `BrepTests [issue520]`:
  - **Sphere:** through the centre (circle of R, two hemispheres by volume and area, mesh volume);
    off centre (radius √(R² − d²), the cap formula both sides); a tilted plane; a plane that touches
    or misses; survey magnitude on a tilted frame, with the circle's centre on the foot of the
    perpendicular within 0.002 ft.
  - **Torus:** through the centre (outlines of R + r and R − r, outer CCW and hole CW, halves by
    volume, mesh volume); off centre (R ± √(r² − d²), the pieces summing to the whole); three
    unsupported angles, all `SliceCutTorusCurve` through both entry points; planes clear of the tube;
    survey magnitude.
  - **API:** `SectionOutlines` agrees with `SectionLoop` where there is one outline, and both report
    the same refusal.
  - Updated: the sphere cases that asserted the old refusal — the curved-slice test, `[issue516]`'s
    curved-face case (now a drilled box), and the "inherits Slice's accepted set" case (now a tilted
    torus).
- `headless.issue520-sphere-and-ring-sections`: the issue's repro (sphere → one outline), `SLICE` into
  two caps with analytic `SOLIDPROPS`, the torus ring (**two** polylines, removed by **one** `UNDO`
  and restored by `REDO`), and the tilted-torus refusal.
- `headless.req335-section`: its refusal case was a sphere, which now sections; it is a tilted torus.
- **Proven to bite:** with the two recognisers unhooked, 10 assertions in `[issue520]` fail and so does
  the transcript.
- Full suite: 1623/1630; the 7 failures are `beta`'s own.

## Not in scope

- A torus cut through its axis (two separate circles) — increment 3's second half, refused by name.
- A drilled box's ring section still refuses: its hole wall is a curved face the cut crosses, which is
  #518's deferred case, not this one. `SectionOutlines` is ready for it.
- Every other torus cut is a quartic curve a `Path` cannot hold.
