# TASK-131 — REQ-141 Analyze ribbon, Create Surface kinds, swap coords, restore demo

- Type:    feature
- Status:  done
- Opened:  2026-08-28
- Owner:   Workshop

## 1. Authority
- Goal:         GitHub #119 / D-2026-08-28-a
- Requirements: REQ-141 (accepted), REQ-137 (accepted), REQ-139 (accepted)
- Constraints:  CON-07; architecture §11; no new layers
- Acceptance:
  - REQ-141: Survey ribbon exposes Surfaces, volume-surface create, Add breakline/contour/boundary,
    Elevations, Slopes / Directions / Arrows, Watershed, Water Drop, Catchment, Volume Dashboard,
    Bounded volume, VOLREPORT, Properties, Statistics, Rebuild; commands stay on the command line;
    EXTRACT FL path/no-path as already tested.
  - REQ-137: Create Surface can name TIN / grid / corridor / volume kinds (commands already exist).
  - REQ-139: SURFSWAPEDGE records a pick in the same local frame as the TIN.
- Owning subsystem: UI, Commands

## 2. Scope
- In scope:        Restore `samples/surface-demo.gs` to a no-surface fixture; Analyze ribbon section;
                   Create Surface type + kind-specific fields; SURFSWAPEDGE World→local.
- Out of scope:    DEM import; per-vertex TIN delete; ISurfaceQuery spatial index rewrite.
- Smallest change: wire existing commands; do not add new surface kinds.

## 3. Architectural boundary check
- [x] No — proceed.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | none | | |

## 5. Assumptions
```
ASSUMPTION-1: Ribbon Analyze tools that need a surface act on the first surface in the drawing.
- Because:       Ribbon has no surface picker; Toolspace already names a specific surface.
- Risk if wrong: user wanted the selected surface
- Validate by:   help text names “first surface”
```

## 6. Plan
- Approach: restore fixture; extend Create Surface; add Analyze ribbon section; LocalFromWorld on swap.
- Files: CadUi.cpp, CadUi_Toolspace.cpp, CadCommands.cpp, samples/surface-demo.gs, req137 transcript, this task
- Test: req069/073/136 green again; req137 swap hit; Catch2 [req139] still green
- Steps:
  - [x] plan
  - [x] restore demo
  - [x] swap coords
  - [x] create kinds
  - [x] ribbon
  - [x] verify

## 7. Workflow-specific notes
- Feature: existing commands; UI wiring
- Bug: demo `.gs` contained a saved surface; swap used World XY on local verts

## 8. Implementation log
- 2026-08-28 opened from #119 review (HIGH demo fixture, MEDIUM ribbon/types/swap)
- 2026-08-28 restored `samples/surface-demo.gs` from 9d97139; SURFSWAPEDGE LocalFromWorld; Create Surface kinds; Survey Analyze ribbon

## 9. Self-verification
- [x] build-project        — PASS
- [x] architecture-review  — PASS
- [x] code-review          — PASS
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a
- [x] testing              — PASS headless req069/073/136/137/140/142; Catch2 [req137][req139][req142]

## 11. Completion report
COMPLETION REPORT — TASK-131 — 2026-08-28
- Requirements satisfied: REQ-141 ribbon tools; REQ-137 type picker; REQ-139 local swap pick
- Summary: Restored the empty-surface demo fixture, World→local edge swaps, Create Surface kinds, Analyze ribbon.
- Tests: req069, req073, req136, req137 (swap hit+miss), req140, req142
- Verification verdict: PASS
- Assumptions: ASSUMPTION-1 (first surface) still open for ribbon targeting
- Architectural decisions: none
- Dependencies: none
- Technical debt noted: ribbon Analyze uses first surface; labels Breakln/Boundry truncated for column width
- Build: clean
- Docs updated: this task

