# TASK-275 — issue #522: a section outline keeps only the corners the shape has

- Type:    fix
- Status:  review
- Opened:  2026-09-23
- Owner:   Workshop
- GitHub:  #522

## Requirement authority

REQ-335, revised this date. REQ-101 (±0.002 ft). No decision entry — see "The choice" below.

## Why

Found by the 2026-09-16 section-coverage probe. `BOX 0,0,-25 100 70 50` ∪ `BOX 50,40,-25 50 50 50`
cut at z = 0 gave a correct area (9000) with **12** vertices for an outline that has **8** corners.
`UNION` leaves each operand's faces split into fragments along the other's planes and does not merge
coplanar fragments back, so `Slice` crosses each internal boundary and keeps a vertex there.

## The choice

The issue names two fixes: merge coplanar faces after a Boolean, or drop the collinear vertices from
the outline. **The outline.** Merging faces would change the vertex, edge and face counts of every
Boolean result — a much larger change, to geometry this issue does not claim is wrong, and one that
would move `SOLIDPROPS` expectations across the suite. Tidying the outline is the smallest change
that meets every acceptance line. The face merge stays available as a follow-up, and would make this
drop a no-op rather than contradict it.

## What changed

- `src/util/brep.cpp` — `DropCollinearPathVertices`, run on every closed section outline as it is
  built (so `SectionLoop` and `SectionOutlines` both get it, once, in one place):
  - a vertex goes only when **both** its segments are straight and it lies within 1e-9 of the
    outline's own size of the line between its neighbours;
  - it must lie **between** them, so a spike doubling back along the same line is kept as the corner
    it is;
  - a run of several collinear vertices collapses in one pass (each looks past the ones already
    dropped);
  - an arc's endpoint is never dropped — it carries the sweep;
  - fewer than three kept vertices means nothing is dropped, so an outline cannot be dissolved.

## Tests

- `BrepTests [issue522]`, on the issue's own union:
  - eight vertices at z = 0, the area still 9000, and the eight corners matched to the issue's list;
  - no three consecutive corners in a line, at z = 0 and on a 45° cut;
  - a single box still sections to four.
- And the outlines that must not lose anything:
  - a 6-sided pyramid keeps six corners;
  - a cylinder's circle keeps both half-turn arcs;
  - a wedge's triangle keeps three;
  - a torus ring keeps both outlines, two arcs each.
- Full suite: 1677/1684; the 7 failures are `beta`'s own.

## Note on the local build

`beta`'s vendored `E57Format.lib` is built with a newer MSVC than this machine's toolset (14.44.35207)
and does not link here, so the suite was run with a temporary local shim for the one missing STL
symbol. The shim is **not** part of this change. Worth raising separately: anyone on 14.44 cannot
build `beta` at all.
