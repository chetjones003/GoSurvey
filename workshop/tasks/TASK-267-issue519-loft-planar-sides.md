# TASK-267 — issue #519: a loft's flat side strips are stored as flat faces

- Type:    fix
- Status:  review
- Opened:  2026-09-17
- Owner:   Workshop
- GitHub:  #519

## Requirement authority

REQ-315 (`LOFT`), revised on this date, and REQ-101. No decision entry: the issue states the expected
behaviour ("store it as a `Plane` face, the way `EXTRUDE` and `PYRAMID` do"). A flat ruled strip is
still the straight span REQ-315 describes, stored as its exact kind.

## Why

Found by the 2026-09-16 section-coverage probe. A 60 × 60 square lofted to a 30 × 30 square is a
square frustum with four flat sides, but `Loft` stored every straight span as a ruled NURBS patch. So:

- `SECTION` refused every cut ("flat faces only");
- `SECTIONPLANE` refused the sides ("that is a freeform face");
- the same frustum made by `PYRAMID` worked.

## What changed

- `src/util/brep.cpp`:
  - New `RuledStripIsFlat`: a quadrilateral's Newell normal, plus a flatness test with every corner
    within `max(1e-9 × strip size, 1e-9)` of the plane. Distances are measured from the first corner,
    so survey-magnitude coordinates stay out of the arithmetic.
  - The two ring edges must run the same way round: reversed, the strip is a bow-tie whose corners are
    coplanar but whose face would cross itself, so it stays a ruled patch (final review on #533).
  - `Loft` stores a straight span that passes the test as `MakePlaneFace`, with the same loop and the
    outward Newell normal. Arc ribbons and twisted strips keep their NURBS patch.
- No command changed. All-planar lofts now take the planar paths in `Slice`, `SectionLoop` and
  `SECTIONPLANE`.

## Tests

- `BrepTests [issue519]`, on the issue's loft:
  - all six faces are planar;
  - volume 105000 and area as the frustum formula and the equivalent `PYRAMID` (turned 45°);
  - `SECTION` matches the pyramid corner for corner at horizontal, vertical and 45° planes, with the
    45 × 45 square area checked;
  - a rotated top profile keeps four NURBS sides;
  - a top profile turned half a turn makes bow-tie strips (flat, but self-crossing): they keep their
    NURBS patch;
  - a kite-shaped top profile keeps four NURBS sides;
  - at survey magnitude on a tilted frame: planar, the volume, and the section corners within
    0.002 ft.
- `headless.issue519-loft-planar-sides`, the issue's own steps:
  - `LOFT` with analytic `SOLIDPROPS`;
  - `SECTION` at z 0, with every corner by `EXPECT POLYVERT`, and one `UNDO`;
  - `SECTIONPLANE` clicking a side face places the plane on it (`SECTIONCLIPNORMAL`).
- **Proven to bite:** with the flat branch disabled, `[issue519]` and the transcript fail.
- Full suite: only `beta`'s 7 known failures.

## Note

The acceptance says "non-similar or rotated" profiles keep freeform sides. The implemented rule is the
issue's own Expected rule, "exactly planar". So a non-similar profile whose edges are still parallel
to the base's (a rectangle over a square) has flat sides, and they are stored as planes. They really
are flat.
