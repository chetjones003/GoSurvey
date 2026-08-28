# TASK-125 — Issue #119 Phase 1 TIN product gaps

- Type:    feature
- Status:  implement
- Opened:  2026-08-27
- Owner:   workshop

## 1. Authority
- Goal:         M-Surfaces-119 (GitHub issue #119)
- Requirements: REQ-124, REQ-125, REQ-126, REQ-127, REQ-128, REQ-129, REQ-130, REQ-135 (accepted D-2026-08-27-a)
- Constraints:  ADR-028, ADR-036, ADR-039; CON-06 smallest change; no surface interface
- Owning subsystem: util (tinbuild, surfaceanalysis, surfacestats) + Commands + IO + UI + Viewport

## 2. Scope
- In scope: Phase 1 REQs listed above.
- Out of scope: REQ-131…134 (Phases 2–3); grid/corridor/volume-surface entity; TIN swap; contour labels.

## 3. Architectural boundary check
- [x] No — ADR-039 already recorded the cache placement, Clip kind, Direction mode, and refusals.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | D1–D10 | 2026-08-27 | User: start Phase 0 then Phase 1 (accepts recommended options) |

## 5. Assumptions
```
ASSUMPTION-1: Surface OSNAP uses covering-triangle interpolation at the cursor XY (no search for
nearest triangle outside the aperture).
- Because: REQ-127 says "at the cursor's plan position"
- Risk if wrong: snap feels sticky off the surface
- Validate by: GUI pass
```

## 6. Plan
- Approach: spec already landed. Implement util modules and wire commands/IO/UI. Indexed query cache
  on AppCommandState (mutable, ADR-039 (c)).
- Test: Catch2 for stats/aspect/clip/index agreement; headless transcript for empty SURFACECREATE.

## 8. Implementation log
- 2026-08-27 Phase 0 spec accepted and written (REQ-124…135, ADR-039, D-2026-08-27-a).
- 2026-08-27 Phase 1: empty surface, stats, query cache, clip, contours, aspect, OSNAP, paper/PDF
  strokes, Catch2 + req124 transcript.
