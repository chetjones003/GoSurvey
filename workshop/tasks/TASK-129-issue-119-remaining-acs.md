# TASK-129 — Implement remaining GitHub #119 acceptance criteria

- Type:    feature
- Status:  done
- Opened:  2026-08-28
- Owner:   Workshop

## 1. Authority
- Goal:         GOAL surfaces / Civil 3D-shaped TIN analysis (issue #119)
- Requirements: REQ-137, REQ-138, REQ-139, REQ-140, REQ-141 (accepted 2026-08-28, D-2026-08-28-a)
- Constraints:  CON-07 build; architecture §11; CadSurface remains the document row
- Acceptance:   as written on REQ-137…141 in spec/requirements.md
- Owning subsystem: util, Domain, Commands, UI, IO

## 2. Scope
- In scope:        SurfaceKind + ISurfaceQuery TIN/grid; grid/corridor/grid-volume; masks; SURFSWAPEDGE;
                   user contours, Chaikin, labels; SlopeAngle; SURFELEV slope/aspect; VOLREPORT;
                   extended SURFACESTATS; Survey Analyze ribbon; WATERDROP EXTRACT FL
- Out of scope:    Civil 3D import, DEM, proximity/wall/non-destructive breaklines
- Smallest change: util query + grid tessellation; definition fields; command/UI wiring; Catch2 + transcripts

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change /
  global state / public-API or data-format change / algorithm the spec didn't
  specify?
    - [x] No — proceed. ISurfaceQuery and SurfaceKind are specified by D-2026-08-28-a / ADR-039.
    - [ ] Yes → STOP.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Treat #119 ACs as requirement changes? | 2026-08-28 | Yes — user instruction |

## 5. Assumptions
```
ASSUMPTION-1: Corridor definition is designated feature-line entity ids, sampled as TIN vertices.
- Because:       REQ-137 names feature-line vertices without a second store.
- Risk if wrong: extra UI to pick FLs
- Validate by:   SURFACECREATECORR empty + SURFACELIST
```

## 6. Plan
- Approach: util first (query, grid, swap, stats, Chaikin), then CadSurface fields, rebuild, IO, commands, UI
- Files/functions: surfacequery, gridsurface, tinbuild, contourgen, surfacestats, CadEntities, CadCommands, GsIo, CadUi*
- Test approach: happy = grid bilinear, TIN query agree, 1-tri stats, Chaikin vertex growth; failure = swap miss, VOLREPORT empty, EXTRACT FL empty, corridor empty
- Steps:
  - [x] Spec D-2026-08-28-a + REQ-137…141
  - [x] Implement
  - [x] Tests + build

## 7. Workflow-specific notes
- Feature: tests-first for pure util; headless for commands

## 8. Implementation log
- 2026-08-28 open; spec recorded; implementation complete

## 9. Self-verification
- [x] build-project        — PASS (GoSurvey, tests, headless)
- [x] architecture-review  — PASS (ISurfaceQuery per D-2026-08-28-a; CadSurface still the row)
- [x] code-review          — PASS
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a
- [x] testing              — PASS Issue119SurfaceTests; req137-grid-corridor-volreport; req140-volreport; req136-volume-surface

## 11. Completion report
COMPLETION REPORT — TASK-129 — 2026-08-28
- Requirements satisfied: REQ-137…141 (Acceptance met: yes)
- Summary: Spec reversed the #119 refusals; implemented kinds, ISurfaceQuery, grid/corridor, masks, edge-swap definition, contour extras, slope angle, VOLREPORT, stats, Analyze ribbon, EXTRACT FL.
- Tests: Issue119SurfaceTests [req137–140]; headless req137, req140 (happy + failure-mode)
- Verification verdict: PASS (findings resolved: GridBuildDisplayTin now sets TinBuildStatus::Ok)
- Assumptions: ASSUMPTION-1 validated via empty corridor
- Architectural decisions: none made by Workshop (D-2026-08-28-a)
- Dependencies: none
- Technical debt noted: none
- Build: reproducible on target platform
- Docs updated: spec/project.md, requirements.md, architecture.md, roadmap.md


## 7. Workflow-specific notes
- Feature: tests-first for pure util; headless for commands

## 8. Implementation log
- 2026-08-28 open; spec recorded; implementation in progress
