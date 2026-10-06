# TASK-279 — a tilted cut that runs off the end of a pipe sections as an arc plus a chord

- Type:    feat
- Status:  review
- Opened:  2026-09-23
- Owner:   Workshop
- GitHub:  #520 follow-up (the shape `SliceCutCrossesCurvedEnd` names)

## Requirement authority

REQ-335, revised this date, under D-2026-09-23-b. REQ-101, REQ-201.

## Why

#531 gave the tilted cut BETWEEN a pipe's caps its ellipse. Where the cut leaves through an end —
the common mitre — the outline is an elliptical **arc** plus the **chord** across that cap, two
shapes, and it was still refused.

## Decision (D-2026-09-23-b)

Both put to the user, both decided as recommended:
1. **Section first.** `SLICE` still refuses this cut; the outline is computed from the primitive's own
   geometry. This is the one place sectioning does not inherit `Slice`'s accepted set — stated, not
   hidden — and it reverts once the cutter learns the cut.
2. **Two objects**: an `ELLIPSE` carrying its span plus a `LINE`, rather than one polyline
   approximating the curve.

## What changed

- `src/commands/CadEntities.hpp` — `CadEllipse` gains `startRad` / `sweepRad`, the pair DXF states in
  groups 41 and 42, defaulting to a full turn, plus `EllipseIsFullTurn` and `EllipseSpanAngleAt`.
- `src/util/brep.{hpp,cpp}` — `SectionEllipseArc` and `SectionEllipseArcOutline`:
  - the full ellipse the plane makes on the endless side surface (closed form for a cylinder, the
    quadric solve for a cone);
  - `EllipseParamsAtAxialHeight` solves `zc + A cos t + B sin t = capZ` for the two parameters where
    it meets a cap;
  - exactly one cap crossed is this shape; none means it stays on the side (`SectionEllipse`, #531's
    job) and both means two arcs and two chords (`SliceCutCrossesCurvedEnd`, still refused);
  - of the two spans between those parameters it keeps the one **inside** the solid, and restates the
    span in the caller's own plane frame when that frame opposes the ellipse's.
- `src/commands/CadCommands.cpp` — `SECTION` asks for it when the refusal is
  `SliceCutCrossesCurvedEnd`, and appends the `ELLIPSE` and the `LINE` under the one undo snapshot.
- `src/render/ViewportRenderer.cpp`, `src/viewport/CadSnap.cpp` — an arc is walked over its own span,
  and drawn open rather than closed.
- `src/io/GsIo.cpp` — the span persists, omitted for a full turn.
- `src/io/DxfIo.cpp` — groups 41/42 are written from the entity, and a trimmed ELLIPSE read back is
  **kept as itself** instead of tessellated (TASK-114 DEBT-1, which existed only because there was
  nowhere to store the range).

## Tests

- `BrepTests [sectionarc]`: the arc's ellipse is the 45° one (semi-minor r, semi-major r√2); the span
  is less than a turn; both chord ends lie on the cap and on the wall; the chord's ends are the arc's
  ends; every sampled point of the arc is inside the pipe and on its wall; a cut between the caps
  reports `SectionEllipse`; a cut off **both** ends is still refused; a cone; and survey magnitude on a
  tilted frame, with the chord's ends on the far cap.
- `headless.section-elliptical-arc`: the cut draws one `ELLIPSE` and one `LINE` in **one** undo step;
  both survive `.gs` and a DXF round trip; the DXF carries the real span in groups 41/42; the
  both-ends cut and the between-the-caps cut keep their own answers.
- Full suite: 1779/1787 — `beta`'s 7 plus `beta`'s em-dash `PIPERUN` test.

## Caught by checking the file, not the count

The first version of the DXF export still wrote groups 41/42 as a full turn, so the arc round-tripped
as the whole ellipse it was cut from — while the transcript passed, because it counted entities. The
transcript now asserts the span in the file itself.

## Not in scope

- `SLICE` building the pieces for this cut (the follow-up that removes the asymmetry above).
- A cut that runs off **both** ends: two arcs and two chords.
