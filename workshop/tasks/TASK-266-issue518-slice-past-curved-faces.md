# TASK-266 — issue #518: SECTION and SLICE cut a filleted solid wherever the plane misses the fillet

- Type:    feat
- Status:  review
- Opened:  2026-09-17
- Owner:   Workshop
- GitHub:  #518

## Requirement authority

REQ-314 (`Slice`) and REQ-335 (`SECTION`), both revised on this date, under D-2026-09-17-c. REQ-101
(±0.002 ft) and REQ-201 (a refusal names its reason, and nothing is written unless every piece
validates).

## Why

Found by the 2026-09-16 section-coverage probe. Round one edge of a box with `FILLET` (REQ-323), and
`SECTION` and `SLICE` refused every cut of it, even far from the rounded edge. `Slice` sent any solid
with a curved face to the primitive recognisers. Those need a `Cylinder` / `Cone` recipe, so a
filleted box fell through to "flat faces only".

## Decision (D-2026-09-17-c)

Put to the user, and each decided as recommended:
1. A plane that crosses a curved face is **refused by name** (`SliceCutCrossesCurvedFace`), not
   supported.
2. The rule covers **every** curved solid the recognisers do not take, not only filleted ones. A
   sphere's refusal changes to the new name.
3. Built on #530.

## What changed

- `src/util/brep.cpp`:
  - New `EdgeDistRange`: the signed-distance range an edge covers.
    - Exact for a line, and for an arc or ellipse: the endpoints plus the stationary angles of
      `k + A cos + B sin` that lie in the sweep.
    - An intersection curve is marched and widened by `1e-4 × model scale`.
  - New `SliceCarryingCurvedFaces`:
    - **Classifying faces.** Each face gets a range from its boundary edges. A sphere, torus or NURBS
      face also takes its tessellation, widened by the chord tolerance.
    - **Faces on one side.** A face wholly on one side, touching allowed, is copied whole: surface,
      parameter range and all loops.
    - **Crossed flat faces.** A crossed planar face is split as the planar cutter splits it, keeping
      whole curved edges and hole loops on their own side.
    - **Refusals.** A crossed curved face, a crossed curved edge, or a curved edge lying on or
      chording the plane gives `SliceCutCrossesCurvedFace`.
    - **Cap and weld.** The cap is chained as before, with the caller's plane as its surface. Each
      piece is welded with straight edges keyed by their ends and curved edges keyed by their source
      edge.
    - **Writing.** Both pieces are validated before either is written.
  - `Slice` calls it in place of the `SliceCurvedFace` fall-through, and for a solid whose faces are
    all planar but has a curved edge. All-planar solids keep the existing cutter, untouched.
- `src/util/brep.hpp`: `Problem::SliceCutCrossesCurvedFace` with its own text, and the `Slice` doc
  comment.
- No command changed.

## Tests

- `BrepTests [issue518]`, on the issue's 100 × 70 × 50 box with a radius-10 fillet on one top edge:
  - Horizontal below the fillet:
    - corners exact;
    - area 7000;
    - pieces of 175000 and 175000 − fillet, the fillet face in the upper piece;
    - the tessellated volume agrees.
  - Vertical beside the fillet: area 5000, with the piece volumes.
  - 45°, meeting the fillet only along its tangent line: area 5000√2, with the piece volumes.
  - A plane through the fillet: `SliceCutCrossesCurvedFace`, and the output is untouched.
  - Two filleted edges: area 7000, and the upper piece carries both fillet faces.
  - A three-edge corner with a sphere patch: a corner cut clear of all fillets gives a 4500
    tetrahedron, and the rest carries the sphere and three cylinders.
  - A box with a drilled hole: cut beside the hole, refused through it.
  - Survey magnitude (E 2,196,000, tilted frame): corners within 0.002 ft, and the volumes, analytic
    and tessellated.
- Updated for the decided wording:
  - the sphere cases in the curved-slice test and in `[issue516]`;
  - the stepped shaft in `[issue515]`;
  - the curved polysolid, which now slices on its straight run (volumes checked);
  - the every-reason-has-its-own-text list.
- `headless.issue518-section-past-fillet`, at E 2,196,000:
  - `FILLET` via `SUBOBJECT`;
  - `SECTION` horizontal (corners by `EXPECT POLYVERT`), vertical and 45°;
  - one `UNDO`;
  - the refusal through the fillet;
  - `SLICE` with analytic `SOLIDPROPS` (volume, area, topology) for both pieces, as one undo step;
  - a `.gs` round trip.
- `headless.issue515-conical-recipe` and `headless.req335-section`: expect the new refusal text.

## Found, not in scope

`SECTION` with a first plane point typed at survey magnitude triggers the drawing-origin shift, and
then refuses with "the selected solids changed before the plane was picked". This reproduces on a
plain box, with no fillet. The transcript uses UCS planes instead.

## Not in scope

Cutting through a curved face (a fillet, a hole wall, a sphere) is #520's ground.
