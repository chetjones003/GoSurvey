# TASK-265 — issue #517: SECTION and SLICE take a vertical cut through a cylinder or cone

- Type:    feat
- Status:  review
- Opened:  2026-09-17
- Owner:   Workshop
- GitHub:  #517

## Requirement authority

REQ-314 (`Slice`) and REQ-335 (`SECTION`), both revised on this date, under D-2026-09-17-b. REQ-101
(±0.002 ft) and REQ-201 (a refusal names its reason).

## Why

Found by the 2026-09-16 section-coverage probe. `SECTION` and `SLICE` took a cylinder or cone only
when the cut went straight across it (a circle). The section a surveyor draws most, a vertical cut
through a pipe, culvert or manhole, was refused.

## Decision (D-2026-09-17-b)

The issue left the tilted cut's representation to a spec decision. Three questions were put to the
user, each with a recommendation, and each was decided as recommended:

1. **Tilted cut: keep the named refusal** (`SectionEllipse`). Neither a polyline nor the flat
   `ELLIPSE` entity (ADR-025) can hold a tilted ellipse, and teaching `ELLIPSE` a plane of its own is
   a separate job.
2. **Cone cut parallel to its axis but off it: refuse it as a hyperbola** (`SliceCutConeOffAxis`),
   not approximate it. The issue's "needs no new curve type" holds only for a cone cut through its
   axis.
3. **Built on #529** (#516), which rewrote the same fall-through.

## What changed

- `src/util/brep.cpp`:
  - New `SliceConicalAlongAxis`, the fifth curved recogniser. `Slice` tries it after the four
    existing ones.
  - It takes a plane whose normal is within 1e-6 of perpendicular to a `Cylinder` / `Cone` recipe's
    axis. The oblique recognisers decline the same planes.
  - Each piece is built from one arc of the rim:
    - a planar cap at each end (the arc plus its chord);
    - a side face spanning that arc (`uStart..uEnd`);
    - two straight seams;
    - the planar cut face, which takes the caller's plane point and normal exactly.
  - A pointed cone has no top cap, and its seams meet at the apex.
  - Both pieces are proven to build and validate before either output is written (REQ-201).
  - Cylinder: any offset strictly inside the radius.
  - Cone: through the axis only.
    - Off the axis inside the base radius gives `SliceCutConeOffAxis`.
    - At or beyond the radius gives `SlicePlaneMissesSolid`.
- `src/util/brep.hpp`: `SliceCutAlongCurvedAxis` (from #516) is replaced by `SliceCutConeOffAxis`,
  because after this change no refused cut fits the old name. `Slice`'s doc comment is updated to
  match.
- No command changed. `SectionLoop` already turns a planar cut face of lines into a closed `Path`,
  and `SECTION` / `SLICE` already store and undo it.

## Tests

- `BrepTests [issue517]`:
  - Cylinder through its axis:
    - the section corners match the 60 x 50 rectangle within 0.002 ft;
    - the path winds CCW;
    - each half's volume is half, and its centroid is 4r/3π on its own side;
    - the tessellated volume agrees.
  - Cylinder off its axis: the rectangle's width is 2√(r²−d²), and the pieces are the segment volume
    and the rest. Keeping one side only follows the normal's sign.
  - Cone: a trapezoid through the axis, and a triangle for a pointed cone. The pieces are halves, and
    the tessellated volume agrees.
  - Cone off its axis: refused with the hyperbola text by both `SectionLoop` and `Slice`.
  - A plane touching or beside the cylinder: `SlicePlaneMissesSolid`.
  - Survey magnitude (E 2,196,000, a tilted frame):
    - the rectangle and trapezoid corners lie within 0.002 ft;
    - the piece volume, analytic and tessellated, is right;
    - the source solid's volume and recipe are unchanged.
- `BrepTests [issue516]`: the axis-parallel refusal case now covers only the off-axis cone.
- `headless.issue517-axis-parallel-sections`, at E 2,196,000:
  - `SECTION` draws the rectangle, trapezoid and triangle, and `EXPECT POLYVERT` checks each corner.
  - The solid is unchanged, and one `UNDO` removes the outline.
  - `SLICE` makes two half-cylinders with the expected `SOLIDPROPS`. It is one undo step, and the
    pieces survive a `.gs` save and reopen.
  - The off-axis cone refusal.
- `headless.issue516-section-refusal-reasons`: the axis-parallel rows become the off-axis cone row.
- **Proven to bite:** with the new recogniser's result ignored, both kernel cases and the transcript
  fail.

## Not in scope

- A tilted section drawn as an exact ellipse needs a tilted `ELLIPSE` entity. That is a follow-up
  issue under D-2026-09-17-b.
- Sphere, torus and hole sections are #520.
