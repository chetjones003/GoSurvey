# TASK-132 — Indexed TIN queries and pick-to-swap edges

- Type:    feature
- Status:  done
- Opened:  2026-08-28
- Owner:   Workshop

## 1. Authority
- Goal:         GitHub #119
- Requirements: REQ-137 (efficient query via existing TinSpatialIndex), REQ-139 (SURFSWAPEDGE)
- Constraints:  CON-07; no new query abstraction beyond ISurfaceQuery
- Acceptance:
  - REQ-137: TIN ISurfaceQuery uses the same spatial index as volume sampling; indexed and scan elevations agree.
  - REQ-139: SURFSWAPEDGE <name> waits for a viewport/typed pick; Toolspace Edits → Add starts that command.
- Owning subsystem: util, Commands, UI

## 2. Scope
- In scope:        TinElevationAtIndexed optional triangle ordinal; TinSurfaceQuery index; swap pick mode
- Out of scope:    Per-vertex TIN delete; ribbon first-surface picker
- Smallest change: reuse TinSpatialIndex; clone WaterDrop pick wiring

## 3. Architectural boundary check
- [x] No — proceed (index already specified for TIN lookup).

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | none | | |

## 5. Assumptions
```
ASSUMPTION-1: Viewport picks are local storage XY (same as WATERDROP).
- Because:       SubmitViewportPickImpl feeds WaterDrop without LocalFromWorld.
- Risk if wrong: double-subtract origin
- Validate by:   existing WATERDROP click path
```

## 6. Plan
- Approach: optional out-tri on indexed elevation; query ctor builds index; SURFSWAPEDGE name-only starts pick
- Files: surfacevolume, surfacequery, CadCommands, ViewportPickPolicy, CadUi, CadUi_Toolspace, Issue119 tests
- Test: existing [req137][query]; SURFSWAPEDGE G1 still hits; SURFSWAPEDGE G1 without xy starts (headless types coords)
- Steps: implement, build, transcripts

## 7. Workflow-specific notes
- Feature: no new interface type

## 8. Implementation log
- 2026-08-28 open after TASK-131
- 2026-08-28 TinSurfaceQuery uses TinSpatialIndex; SURFSWAPEDGE <name> starts a pick; Toolspace Edits Add enabled

## 9. Self-verification
- [x] build-project — PASS
- [x] architecture-review — PASS
- [x] code-review — PASS
- [x] testing — PASS [req137][req139]; req137 transcript including unbuilt swap start

## 11. Completion report
COMPLETION REPORT — TASK-132 — 2026-08-28
- Requirements satisfied: REQ-137 indexed TIN query; REQ-139 pick-to-swap
- Summary: ISurfaceQuery TIN path uses TinSpatialIndex; SURFSWAPEDGE without XY waits for a pick; Toolspace Edits → Add starts it.
- Tests: Issue119 [req137][req139]; req137-grid-corridor-volreport
- Verification verdict: PASS
- Assumptions: ASSUMPTION-1 (viewport picks are local)
- Architectural decisions: none
- Dependencies: none
- Technical debt noted: none
- Build: clean
- Docs updated: this task

