# TASK-177 — REQ-316 Increment 1: per-vertex bulge storage, POLYLINE arc mode, arc render, DXF/.gs round-trip

- Type:    feature
- Status:  implement
- Opened:  2026-09-02
- Owner:   chetjones003

## 1. Authority
- Requirements: REQ-316 (accepted)
- ADR:          ADR-047 (accepted); architecture §11.8 (amended for stride-4 polyline verts)
- Decision:     D-2026-09-02-b
- Acceptance (this increment — verbatim from REQ-316):
  - Starting POLYLINE, typing `A`, picking a point, typing `L`, picking a point, and finishing
    yields exactly one polyline entity with one arc segment followed by one line segment.
  - The arc segment is tangent to the preceding segment at their shared vertex — angle between the
    incoming segment direction and the arc's tangent there is 0 within 1e-4 rad.
  - `Undo` during the command removes the most recent segment and the command continues drawing
    from the previous vertex in the previous mode.
  - A polyline containing an arc segment renders as a curve (tessellation within a chord-height
    tolerance of the true arc).
  - Its reported length equals the sum of straight-segment lengths and arc-segment arc lengths
    within REQ-101; hand example (3-4-5 leg + quarter circle R10) matches.
  - Saving to DXF and reloading reproduces every vertex within REQ-101 and every bulge within
    1e-6, with no increase in vertex count; a straight polyline is byte-stable.
  - Saving to `.gs` and reloading in a fresh process reproduces the polyline exactly; a pre-ADR-047
    `.gs` fixture loads with the polyline straight and no error.
- Owning subsystem: Domain / Commands / Renderer / IO (per ADR-047).

## 2. Scope
- In scope: `util/geom2d::BulgeArc` (done); parallel `userPolylineVertsBulge` array across all
  three store copies + the ~6 vertex-mutation mirror sites; renderer arc tessellation; POLYLINE
  `Arc`/`Line` sub-modes + `CEnter`/`Radius`/`Angle`; `docinvariants` bulge-count + finite-bulge;
  DXF group 42 export + import (delete tessellation fallback) + arc extents sweep; `.gs` additive
  bulge array (no version bump); polyline length/area over arc segments.
- Out of scope: snap/pick/Properties/grips on arc segments (Inc 2); JOIN arcs (Inc 3);
  TRIM/OFFSET/FILLET/CHAMFER (Inc 4); 3DPOLY arc mode; polyline width/taper; splines.
- Smallest change: bulge defaults 0 everywhere; only render/length/IO branch on `bulge != 0`.

## 3. Architectural boundary check
- [x] No NEW abstraction/layer/dependency/global. Data-format change is pre-authorised by ADR-047
  (f) + D-2026-09-02-b (`.gs` additive, no version bump). Proceed.

## 6. Plan
- Approach: as Step-2 verification plan in the session / ADR-047 consequences.
- Files/functions to touch: `src/util/geom2d.*`, `src/commands/CadCommands.*` (AppCommandState,
  polyline draft, length), `src/commands/CadCoordinateFrame.*`, `src/io/DxfIo.cpp`, `src/io/GsIo.cpp`,
  `src/renderer/*` (polyline batch), `src/util/docinvariants.cpp`, `tests/*`, `tests/headless/*`.
- Test approach: happy = arc+line polyline round-trips DXF/.gs and renders; failure = degenerate
  bulge, pre-ADR-047 .gs fixture, straight-polyline byte stability.
- Steps:
  - [x] BulgeArc helper + unit tests (tests/BulgeArcTests.cpp green)
  - [ ] parallel userPolylineVertsBulge array (3 copies) + mirror sites + docinvariants
  - [ ] renderer arc tessellation
  - [ ] POLYLINE Arc/Line sub-modes + options + Undo
  - [ ] polyline length over arcs
  - [ ] DxfIo export/import + extents
  - [ ] GsIo additive bulge array
  - [ ] tests green; self-verify skills

## 8. Implementation log
- 2026-09-02 open → implement. Branch feat/polyline-arc-mode (worktree).
- 2026-09-02 BulgeArc + BulgeSegmentLength in util/geom2d; geom2d.cpp added to GoSurveyTests;
  tests/BulgeArcTests.cpp (6 cases / 17 assertions) green.
- 2026-09-02 Storage approach corrected stride-widen → parallel `userPolylineVertsBulge` array
  (ADR-047 (a) correction; D-2026-09-02-b updated). Rename attempt reverted.
