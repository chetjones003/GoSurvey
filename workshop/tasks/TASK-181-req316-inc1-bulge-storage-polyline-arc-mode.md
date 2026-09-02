# TASK-181 — REQ-316 Increment 1: per-vertex bulge storage, POLYLINE arc mode, arc render, DXF/.gs round-trip

- Type:    feature
- Status:  self-verify
- Opened:  2026-09-02
- Owner:   chetjones003

## 1. Authority
- Requirements: REQ-316 (accepted)
- ADR:          ADR-047 (accepted); architecture §11.8 (amended for stride-4 polyline verts)
- Decision:     D-2026-09-02-e
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
  (f) + D-2026-09-02-e (`.gs` additive, no version bump). Proceed.

## 6. Plan
- Approach: as Step-2 verification plan in the session / ADR-047 consequences.
- Files/functions to touch: `src/util/geom2d.*`, `src/commands/CadCommands.*` (AppCommandState,
  polyline draft, length), `src/commands/CadCoordinateFrame.*`, `src/io/DxfIo.cpp`, `src/io/GsIo.cpp`,
  `src/renderer/*` (polyline batch), `src/util/docinvariants.cpp`, `tests/*`, `tests/headless/*`.
- Test approach: happy = arc+line polyline round-trips DXF/.gs and renders; failure = degenerate
  bulge, pre-ADR-047 .gs fixture, straight-polyline byte stability.
- Steps:
  - [x] BulgeArc helper + unit tests (tests/BulgeArcTests.cpp green)
  - [x] parallel userPolylineVertsBulge array (3 copies) + mirror sites + docinvariants
  - [x] renderer arc tessellation (AppendChainEdgesVc expands non-zero-bulge segments)
  - [x] POLYLINE ARC/LINE sub-modes + RADIUS/CANGLE + UNDO (D-2026-09-02-f)
  - [x] polyline length over arcs (PolylineOpenLengthOf -> BulgeSegmentLength)
  - [x] DxfIo import (store bulge, drop tessellation) + export (group 42)
  - [x] GsIo additive bulge array (model + block content), no version bump
  - [x] tests green: 817 Catch2, 996/996 ctest incl. req316 transcript (58 steps)

## Deferred within Inc 1 (documented, not silent)
- Live rubber-band preview still shows a straight line during an arc pick
  (CadRubberPreview not arc-aware). Acceptance doesn't require preview.
- DXF `$EXTMIN/$EXTMAX` sweep uses vertices only; an arc bowing outside its chord
  is not in the header extents. Straight- and arc-polyline byte-stability both
  pass (transcript), so this is a refinement, not a round-trip break.
- PdfPlot and block-INSERT render draw arc segments as chords.
- LibreDWG (.dwg) import reads bulges as straight (DXF path is complete).
- Paper-space polylines have no bulge store.
- COPY / MIRROR / ROTATE / clipboard-paste of an arc polyline flatten it to
  straight (SyncPolylineBulge pads zeros). Arc-aware modify is Inc 3.
- Snap / pick / Properties on arc segments — Inc 2.

## 8. Implementation log
- 2026-09-02 open → implement. Branch feat/polyline-arc-mode (worktree).
- 2026-09-02 BulgeArc + BulgeSegmentLength in util/geom2d; geom2d.cpp added to GoSurveyTests;
  tests/BulgeArcTests.cpp (6 cases / 17 assertions) green.
- 2026-09-02 Storage approach corrected stride-widen → parallel `userPolylineVertsBulge` array
  (ADR-047 (a) correction; D-2026-09-02-e updated). Rename attempt reverted.
- 2026-09-02 Storage layer + docinvariants + GsIo landed; full suite green (commit f70f3ab).
- 2026-09-02 DxfIo bulge round-trip + renderer arc tessellation + arc length + draft plumbing.
- 2026-09-02 Keyword conflict (`A`/`ANGLE` = bearing lock) raised with user → D-2026-09-02-f:
  full-word `ARC`/`LINE`/`RADIUS`/`CANGLE`/`UNDO`.
- 2026-09-02 Keyword handler + req316 transcript (58 steps) + docinv fixtures. 817 Catch2 /
  996 ctest green.

## 9. Self-verification
- [x] build-project — PASS (release + GoSurveyTests + gosurvey_headless, MSVC)
- [x] architecture-review — PASS. No Workshop architectural decision: the storage change is
  ADR-047 (a) (parallel array, exception noted in §11.8); the keyword choice is D-2026-09-02-f
  (user). No new dependency, layer, global, or public API. `BulgeArc` is a pure value helper with
  3 uses (§11.4). `.gs` additive, no version bump (ADR-030).
- [x] code-review — PASS. Bulge array is empty-tolerant everywhere (empty = all straight), kept
  empty for straight-only drawings so `.gs` stays byte-stable; mutation sites mirror or Sync;
  ErasePolylineByIndex cuts the per-vertex span [a,b). One correction mid-work (stride→sidecar).
- [x] dependency-audit — PASS / n-a (no new dependency)
- [x] performance-review — n-a. Arc tessellation is chord-tolerance, feeds the existing line
  batch (no new GL); straight polylines take the unchanged path. Not a measured REQ-100 profile.
- [x] testing — PASS. Happy: tangent/radius/angle bulge, DXF+`.gs` round trip, arc length.
  Failure: short bulge array, non-finite bulge, degenerate chord, straight-polyline byte stability.

## 10. Verification result
- Verdict: PASS for increment 1 (self-verified). Increments 2–4 are separate tasks.
