# TASK-282 — ZOOM EXTENTS frames an orbited view by the model's 3D silhouette

- Type:    feat
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  #564 section 1 (second of the issue's eight increments; §3 was PR #566)

## Requirement authority

REQ-122 as amended 2026-09-28 under D-2026-09-28-d. REQ-058 (camera), REQ-123 (floating viewport,
unchanged), REQ-201 (refusals stated).

## Why

`ZOOM EXTENTS` swept a 2D rectangle and framed it with pan/zoom only; azimuth, elevation and roll never
entered. Orbited, the frame was too tight / loose / off-centre, and Z was ignored — a tall model ran
off screen. A drawing of pipe runs alone reported "nothing to frame".

## Plan / architectural-boundary check

Commands layer only: a new 3D sweep beside the 2D one, and a pure framing function in
`ZoomFraming.hpp` beside `FrameWorldRect` (which it calls). No renderer / UI change; the camera is
read through `CadViewCamera` + `Camera::ViewRotation`, a downward dependency already used here.

## What changed

- `src/commands/ZoomFraming.hpp` — `FrameBoxInView`: target = box centre; ortho zoom from
  `FrameWorldRect` on the eight corners' screen half-extents; perspective half-height by the closed form
  `max(|u|/((1−m)a), |v|/(1−m)) + w·tan(fov/2)`; refuses non-finite input, writing nothing.
- `src/commands/CadCommands.cpp`
  - `EntityBox` gains `mnZ/mxZ`, filled for every type `CollectEntityBoxes` covers (tilted
    circles / arcs / ellipses by their radius; TIN Z by a vertex sweep). The plan path never reads them.
  - `CollectEntityBoxes3dExtras`: filled regions, block references, pipe runs.
  - `ComputeWorldExtents3d`: both collections, the same outlier rule as the robust 2D sweep.
  - `ProcessPendingViewportZoom`: orbited (or rolled) → 3D sweep + `FrameBoxInView`, sets pan X/Y/Z and
    zoom; plan → unchanged, with a fallback to the 3D footprint only when the 2D sweep is empty.
- `src/commands/CadCommands.hpp` — `ComputeWorldExtents3d` declared.

## Tests (`tests/ZoomExtents3dTests.cpp`, GoSurveySnapTests)

Each checks the camera's own `WorldToScreen` projection of the box corners, not the formula:
- 16 orbited orientations: inside the margin, centred to a pixel, tight on the binding axis, from an
  arbitrary starting zoom/pan.
- A 4×4×300 mast frames by its height.
- Five repeats leave pan X/Y/Z and zoom bit-identical.
- Perspective: every corner inside, and one reaches the margin.
- Pipe runs alone frame in plan and orbited; an empty orbited drawing still reports nothing to frame.
- Plan view equals `FrameWorldRect(ComputeRobustWorldExtents)` exactly and leaves pan Z alone.
- `FrameBoxInView` refuses a NaN box and writes nothing.
- Full suite: 1825/1833 — the 8 failures are `beta`'s pre-existing ones.

## Not in scope

§2 depth-ordered picking and the rest of #564. Framing into a floating paper viewport (REQ-123) still
uses its plan rectangle; per-viewport orbited cameras (REQ-061) are a separate requirement.

## Technical debt / assumptions

- Extents are axis-aligned boxes per entity, so a tilted circle's Z range is conservative (± its
  radius) and an orbited frame can be slightly loose for one; it is never tight enough to clip.
