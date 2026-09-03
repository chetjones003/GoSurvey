# TASK-190 — POLYSOLID: a wall swept along a path (issue #146, REQ-317 new)

## Requirement authority

- **REQ-317** (new) — POLYSOLID.
- **ADR-050** (new) — offset-and-mitre in the kernel.
- **D-2026-09-03-d** — the recorded decision, including the scope the user chose.
- Uses **`Surface::inward`** (REQ-314 B2a, D-2026-09-02-c) and REQ-314's tessellator paths; adds
  neither.
- REQ-201, REQ-301, REQ-304, REQ-313, REQ-316 (a converted polyline's arc segments), ADR-045.
- GitHub issue #146. The last of three pieces sequenced with the user; follows TASK-171 and TASK-172.

## What was asked for

The third of the screenshot requests: *"this is the first point for polysolid… this is the 2nd point
for the polysolid."* Asked what scope the first version should have, the user chose **all of it,
matching AutoCAD**: straight and curved segments, mitred throughout, plus converting an existing
object.

**One thing said out loud during scoping was wrong and is corrected here**: a curved run was
described as sweeping *torus*-shaped faces. It does not. A polysolid extrudes a flat profile straight
up, and extruding a planar arc perpendicular to its own plane sweeps a **cylinder** — a surface the
kernel already integrates in closed form. That is also why this could ship while REQ-315's general
sweep stays blocked on the freeform-surface question.

## The substance: corners

Offsetting each segment to each side is easy and produces a run of disconnected pieces. Making one
wall out of them means **intersecting adjacent offsets** so the corner is mitred and belongs to the
wall exactly once. Line/line solves two lines, line/arc a quadratic, arc/arc the radical line — three
cases, all closed form, nothing iterative. A **smooth** join is taken directly rather than solved
for: its two offsets are tangent there, so the intersection is a double root, and every arc the
command draws is tangent to the run before it.

The tempting alternative — one box per straight run — is far easier and wrong three ways at once. The
runs **overlap**, so the volume double-counts every bend; the drawing holds N objects where the user
drew one, so a single MOVE or ERASE cannot address the wall; and the overlap is invisible in the
shaded view, which makes it exactly the silent wrong answer REQ-201 exists to prevent.

## What this task DELETED before shipping, and why that is the main lesson

The work was written against a `beta` that had since moved a long way — REQ-314's whole Phase 4
workstream landed while it was in progress. Rebasing onto it removed **two of its three parts**:

- **A `Surface::sense` field**, added for the inner face of a curved wall. REQ-314 B2a had already
  added **`Surface::inward`** for the wall of a Boolean bore — the same situation seen from the other
  side — with the same three application points (the divergence integrand, the mesh normals, the mesh
  winding). The duplicate was deleted and `inward` reused. Two flags meaning the same thing is
  precisely the disagreement ADR-045's original "no reversed flag" rule existed to prevent; keeping
  both would have been that rule's failure mode rather than its absence.
- **A planar triangulator** — ear clipping with hole bridging, written because a wall's cap is
  non-convex the moment its path bends and annular when the path closes on a circle. REQ-314 had
  already taught the plane branch all three cases: convex rings are fanned, non-convex ones
  ear-clipped, two-loop faces stripped by angle about the hole. Deleted entirely. It also un-did a
  side effect that version had: `req313-solid-isolines` had needed its triangle count changed from
  200 to 196, and does not any more.

What survives is a builder and nothing else: no new surface kind, no new curve kind, no change to the
tessellator, the integrals or the validity checks. **The reconciliation made the change smaller, and
that is the right outcome to record rather than to hide.**

A third correction came from the same rebase: ADR-050 (a) had said a GoSurvey polyline is
straight-only. REQ-316 gave the polyline store per-vertex **bulges** while this was being written, so
a converted polyline now brings its arc segments across — `tan(theta/4)` to `PathSeg::sweep` as
`4*atan(bulge)`, the DXF convention both stores already share.

## Test approach

`BrepTests [brep][req317]` for the geometry, where it can be written down exactly.

The load-bearing case is the mitred right angle, and it asserts **two** numbers for a reason. For
centre justification a mitred wall's plan area is exactly `width × centreline length`, whatever
angles it turns through — a mitre gives back on the outside of a bend precisely what it takes on the
inside. That makes the **volume** a lovely invariant and a **useless discriminator**: a run of
overlapping boxes sums to the same number. The **surface area** is what separates them — 212 square
feet for one wall with two end caps, 224 for two boxes with four.

Also: a one-segment wall that is exactly a box; a closed rectangle checked independently as the
difference of two prisms; a full ring checked against `π(11² − 9²)` with its inner faces asserted to
be `inward` and its cap faces asserted to carry two loops; a tangent arc join taken directly;
justification moving the wall without resizing it; survey-magnitude stability; and every refusal by
name. One case asserts the **triangle count** (32) specifically to pin that a swept solid's non-convex
cap reaches REQ-314's ear clipper while its convex quads still go through the centroid fan.

`headless.req317-polysolid` (**147 steps**) for the half the kernel cannot see: a typed L-wall, a
closed rectangle by `C`, `U` replacing one leg, **the preview matching the commit** (`PREVIEWBOUNDS`
then `SOLIDBOUNDS` at the same place), a tangent arc, an arc refused as the first segment, Left
justification, `O` on a Line, a Circle and an **arc-carrying Polyline** (asserted to give the same
figures as the picked-arc wall, which is what pins the bulge conversion), both refusal classes with
the run left standing, Esc, and a `.gs` round trip.

## Verification

- `ctest`: **1037/1037 green.**
- **Negative-tested four ways** (the first two against the pre-rebase version, whose mitre and
  triangulator this shares):
  1. removing the mitre — four `BrepTests` cases;
  2. restoring the centroid fan on the L-shaped cap — `Dot(Normalize(geo), n) > 0.0 with expansion
     -1.0 > 0.0`, the fan emitting triangles that fall outside a non-convex face;
  3. building the preview without the cursor — `PREVIEWBOUNDS: the preview is empty`;
  4. ignoring the arc tangent so a curved segment goes straight — `volume is 144.852814, expected
     154.24778`.
- **One real defect was found by the round trip while it was being written.** The face-orientation
  flag was not written to `.gs`, so the ring wall reopened with its inner faces facing outward,
  failed the geometric closure probe and was refused on load: nine solids saved, eight read back.
  (The fix now rides on REQ-314's existing `inward` key rather than a second one.) Worth noting that
  the probe ADR-045 added is what caught it, and that no topological check could have — a shell with
  one face turned the wrong way is manifold, orientable and ring-closed.

## Assumptions

- **ASSUMPTION-1 (stated):** an `O`bject source must lie in the current work plane, and one that does
  not is refused by name rather than projected onto it. Projecting would silently build a wall along
  a path the user never drew.
- **ASSUMPTION-2 (stated):** height, width and justification persist across invocations and are saved
  with the drawing. Taken from AutoCAD's `PSOLWIDTH` / `PSOLHEIGHT`; a wall is almost always drawn at
  the same size as the last one.

## Composing with REQ-314, measured rather than assumed

The point of putting a polysolid in the same `brep::Solid` as everything else is that the feature
operations accept it. That is now asserted, with the figures that would catch a wrong answer rather
than a returned `true` — a boolean that kept the wrong side still succeeds:

| Operation | Result |
|---|---|
| a doorway box SUBTRACTed from a straight wall | 320 − 42 = **278** (its overlap, not its volume) |
| UNION of the two | 320 + 84 − 42 = **362** |
| SLICE a straight wall in half | **160 / 160** |
| SLICE a BENT wall off-corner | **90 / 30**, the mitred corner intact in the larger piece |
| SUBTRACT a notch from a bent wall | 120 − 8 = **112** |

The wall's own volume is asserted first, so a regression in `MakePolysolid` trips before any of these
and a regression in the operation trips after — the two cannot be confused for each other.

Refused, and asserted **by name** so the boundary is visible when it moves: a **curved** wall gives
`SliceCurvedFace` and `BooleanCurvedFace`. Neither is a polysolid defect nor a gap in this
requirement — they are the limits REQ-314 states for its own increments (SLICE 3a is flat-faced, B1
takes uncurved operands). When those increments land, these assertions fail and say so, rather than a
curved wall quietly staying unusable.

## Why this is NOT built on `Extrude`, measured rather than argued

A wall's plan outline is a closed planar loop of lines and arcs, which is exactly what `Extrude`
takes, so folding `MakePolysolid` into it is the obvious refactor to reach for. It was **two**
reasons, and the investigation removed one of them from the codebase rather than from the argument:

1. `Extrude` refused an arc curving **into** its loop, which the inner rail of every bend is. That
   restriction was ADR-046 (d)'s own "separate feature, now unblocked", and this requirement's test
   is what pointed at it — so it was **lifted** (D-2026-09-03-b), and the test that pinned it is
   gone because the thing it pinned is gone.
2. `Profile` is a **single loop**, and a closed wall's plan is an annulus with two. That one is not
   going away by itself, and it is what the test pins now: extruding a ring wall's outer rail alone
   does not approximate the wall, it fills the courtyard in — a solid cylinder more than three times
   the wall's volume. The gap between those two figures is the whole of the argument.


## Technical debt / stated boundaries

- **DEBT-1 — no self-intersection check on a path containing a curve.** Exact for straight-segment
  paths, where a rail is a polygon. With an arc in the path a rail is not a polygon, and testing its
  chords instead would refuse walls that are perfectly fine — **a false refusal being strictly worse
  than the absence of a check**. The general case is the same Phase 4 test ADR-045 already defers.
- **DEBT-2 — a placed polysolid's path cannot be edited.** The recipe carries it, so the information
  is there; a parametric edit is #120 Phase 5, along with transforming any solid at all.
- **DEBT-3 — the preview is verified as geometry, not pixels.** Same category as REQ-064's styles.
- **Noticed, not fixed, and not ours:** a blank Enter after a LINE or CIRCLE chain re-arms that
  command, so the next verb typed is swallowed as a point. Pre-existing, reproducible with `SPHERE`
  as readily as with `POLYSOLID`, and the transcript uses `Esc` rather than working around it
  silently.

## Status

Complete and verified. Goes to review, not done; the issue is not closed here.
