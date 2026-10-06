# TASK-276 — issue #531: an ellipse carries its own plane, and a tilted cut is drawn as one

- Type:    feat
- Status:  review
- Opened:  2026-09-23
- Owner:   Workshop
- GitHub:  #531

## Requirement authority

REQ-312 (revised this date, under D-2026-09-23-a) and REQ-335. REQ-101 (±0.002 ft), REQ-201.

## Why

REQ-312 gave arcs and circles a plane and left the ellipse flat. Three costs, all live:

- `SECTION` refused every tilted cut of a cylinder or cone — the cut a surveyor makes through a pipe
  on a slant;
- `ELLIPSE` on a tilted UCS landed flat;
- a tilted `ELLIPSE` in a DXF arrived flat and in the wrong place, silently: group 210 was never read.

## Decision (D-2026-09-23-a)

Three questions, each decided as recommended:
1. **A section-only result** (`brep::SectionEllipse`) rather than an ellipse segment in `Path` — every
   feature that consumes a `Path` would otherwise have to learn one only to refuse it.
2. **Full ellipses only.** A cut that also crosses an end cap is an elliptical arc plus a chord and
   keeps its refusal.
3. **OFFSET** treats a tilted ellipse exactly as a flat one, in its own plane — no tool taken away.

## What changed

- `src/commands/CadEntities.hpp` — `CadEllipse` gains `nx/ny/nz` (default +Z), with `CurvePlane`,
  `EllipseWorldPointAt` and `EllipseIsFlat` beside the arc's. The major-axis vector is read in the
  ellipse's own plane, which for a flat one IS world XY — so nothing flat moves.
- `src/render/ViewportRenderer.cpp` — a tilted ellipse tessellates through its own plane with a Z per
  vertex, the same branch a tilted arc already takes.
- `src/util/brep.{hpp,cpp}` — `SectionEllipse` and `SectionEllipseOutline`: the cut face's loop, when
  it is two half-sweeps of one ellipse, read back as centre, normal, major direction and semi-axes. A
  sweep that does not add to a full turn is the cap-crossing cut, refused as before.
- `src/commands/CadCommands.cpp` — `SECTION` asks for that by name when the outline refusal is
  `SectionEllipse`, and creates an `ELLIPSE` standing in the cut plane.
- `src/io/GsIo.cpp` — the normal persists, omitted when +Z (additive, no format bump).
- `src/io/DxfIo.cpp` — export writes the real 11/21/31 (world major axis) and 210/220/230; import
  reads both and turns the axis back into the ellipse's own plane.
- `src/viewport/CadSnap.cpp`, `src/ui/CadUi.cpp` — the plan-space snap, pick and grip paths **skip** a
  tilted ellipse, as they already skip a tilted arc. See "Not in scope".

## Tests

- `BrepTests [issue531]`:
  - a 45° cut of a cylinder: semi-minor r, semi-major r / cos 45, centred on the axis, with the
    normal and major direction both in the caller's plane;
  - 15°, 30° and 60°: the minor axis never changes and the major follows 1 / cos;
  - off the axis, the same shape centred where the plane crosses;
  - a cut crossing an end cap refused as `SliceCutCrossesCurvedEnd`;
  - survey magnitude on a tilted frame, to 0.002 ft;
  - a cone's tilted cut, and a cut steeper than its side refused.
- `headless.issue531-tilted-ellipse-section`: the cut that was refused now draws one ellipse and no
  polyline, in one undo step; it survives `.gs`; it survives a DXF round trip with its plane (group
  210 checked in the file, twice — after export and after re-export); the cap-crossing cut is still
  refused by name; and a flat ellipse round-trips unchanged.
- `headless.issue516-section-refusal-reasons`: its tilted-cut row now draws the ellipse instead of
  naming the refusal.
- Full suite: 1680/1687; the 7 failures are `beta`'s own.

## Not in scope

- **Plan-space snap, pick and grips skip a tilted ellipse** — stated in the REQ revision. They compute
  in plan and would otherwise act on a flattened projection (REQ-201). The follow-up is the same
  work REQ-312 item 3 describes for arcs.
- An elliptical **arc** (the cap-crossing cut) — the ellipse would need a start and end parameter.
- DWG (`LibreDwgCad`) export of a tilted ellipse is untouched.

## Note on the local build

`beta`'s vendored `E57Format.lib` needs a newer MSVC than this machine's (14.44.35207), so the suite
was run with a temporary local shim for the one missing STL symbol. The shim is **not** part of this
change.
