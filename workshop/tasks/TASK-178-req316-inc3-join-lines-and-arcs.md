# TASK-178 — REQ-316 Increment 3: JOIN welds lines and arcs into one polyline

- Type:    feature
- Status:  implement
- Opened:  2026-09-02
- Owner:   chetjones003

## 1. Authority
- Requirements: REQ-316 (accepted) — acceptance items 6, 7, and the JOIN acceptance lines
- ADR:          ADR-047 (increment 3: "JOIN of lines + arcs, and arc-segment grips")
- Decision:     D-2026-09-02-b
- Acceptance (verbatim from REQ-316):
  - JOIN of two lines meeting at a point produces one 2-segment straight polyline; originals removed.
  - JOIN of a line and a tangent arc meeting at a point produces one polyline whose arc segment has a
    non-zero bulge matching the arc's included angle within 1e-6.
  - JOIN of objects whose nearest endpoints are farther apart than the JOIN tolerance leaves the
    drawing unchanged and logs which objects were not joined.
  - Undo after JOIN restores the exact pre-command geometry in one step.
- Owning subsystem: Commands (`ExecuteJoinSelection`).

## 2. Scope
- In scope: `ExecuteJoinSelection` accepts `ARC` entities as edges carrying a bulge
  (tan(sweep/4)); polyline edges carry their per-segment bulge; the Eulerian walk tracks edge
  traversal direction so a reversed edge negates its bulge; the emitted polyline writes
  `userPolylineVertsBulge`; consumed arcs are erased. Tilted arcs (REQ-312) are refused by name
  (a 2D polyline cannot carry the tilt), like feature lines already are.
- Out of scope: arc-segment grips (separate follow-up); JOIN in paper space; CIRCLE (no endpoints —
  already refused).
- Smallest change: extend the existing `Edge` struct + walk; no new command, no new store.

## 3. Architectural boundary check
- [x] No NEW abstraction / layer / dependency / global / API / data-format change. Extends one
  command's internal algorithm. Proceed.

## 6. Plan
- `Edge` gains `float bulge` and `int arcIx`.
- Line edges: bulge 0. Polyline edges: bulge from `userPolylineVertsBulge`. Arc edges: endpoints
  from (cx,cy,r,startRad,sweepRad), bulge = tan(sweepRad/4); flat arcs only.
- Hierholzer records the edge used between consecutive path vertices; direction decided by matching
  the edge's stored x0/x1 cluster against the path order; reversed → negate bulge.
- Build `pvBulge` in lockstep with `pv`; on the closed-loop vertex trim, trim `pvBulge` too.
- Commit: `SyncPolylineBulge` then overwrite the new polyline's bulge tail.
- Erase consumed arcs (descending index, mirror attrs) like the line-delete block.
- Tests: headless transcript (line+arc join, tangency bulge, non-contiguous refusal, undo) +
  extend req316 transcript.

## 8. Implementation log
- 2026-09-02 open. Branch feat/polyline-arc-mode (worktree). Inc 1 (TASK-177) shipped.
