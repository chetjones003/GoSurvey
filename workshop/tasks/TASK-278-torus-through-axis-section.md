# TASK-278 — a torus cut through its axis sections as two circles

- Type:    feat
- Status:  review
- Opened:  2026-09-23
- Owner:   Workshop
- GitHub:  #520 (increment 3, second half)

## Requirement authority

REQ-314 and REQ-335, revised this date. REQ-101, REQ-201. No decision entry: increment 3 already
named this cut and the shape it makes (D-2026-09-18-a scoped the first pass to the square cut and
left this one refused by name); this delivers it.

## Why

`SECTION` on a doughnut refused every plane but the square one. A plane **through the axis** meets the
ring on both sides, and its section is two plain circles — a shape the outline has always been able to
hold, now that a section may be more than one outline (#520).

## What changed

- `src/util/brep.cpp` — `SliceTorusThroughAxis`:
  - takes a plane parallel to the axis and passing through it (within `1e-7 × (R + r)`);
  - each piece is half the doughnut: the tube over half a turn (two `Torus` patches, as `MakeTorus`
    winds them) closed by a flat disc at each end, both facing away from the material — which is what
    makes a half-doughnut rather than a wedge;
  - the two halves are one construction in two frames, the second turned half a turn about the axis,
    which a torus is symmetric under;
  - a tube as wide as its ring, a recipe that does not describe its solid, and a plane **beside** the
    axis are each refused exactly as before.
  - `Slice` asks this recogniser **before** `SliceTorusSquareToAxis`, which answers for every other
    torus plane — including with the quartic refusal — and would otherwise swallow this cut.

## Tests

- `BrepTests [torusaxis]`:
  - the section is two outlines, each a full circle in two half-turn arcs, of diameter 2r, with their
    centres 2R apart and both on the ring;
  - `SLICE` gives two pieces of half the volume, each with the analytic surface area (half the tube
    plus two discs), the tessellated volume agreeing, and the centroids on the sides the normal names;
  - `SectionLoop` — the single-outline entry point — reports `SliceCutSeveralOutlines`;
  - beside the axis is still `SliceCutTorusCurve`, through both entry points;
  - survey magnitude on a tilted frame.
- Updated: `[issue520]`'s "every other torus cut" case no longer uses the through-axis plane as an
  example of a refusal, and gains a beside-the-axis one in its place.
- `headless.section-coverage-3d-primitives`: the torus/vertical cell is now two outlines.
- `headless.req342-sphere-torus-sections`: the torus's vertical outline is two circles, with a
  beside-the-axis block added to keep the refusal covered.
- Full suite: 1777/1785 — `beta`'s 7, plus `beta`'s em-dash `PIPERUN` test, which fails through ctest
  only (it passes when the binary is run directly).

## Not in scope

Every other torus cut is a quartic curve a `Path` cannot hold, and keeps `SliceCutTorusCurve`.
