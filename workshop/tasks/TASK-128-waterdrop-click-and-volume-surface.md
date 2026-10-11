# TASK-128 — Route WATERDROP/CATCHMENT clicks; add TIN volume surfaces

- Type:    feature | bug
- Status:  implement
- Opened:  2026-08-27
- Owner:   workshop

## 1. Authority
- Goal:         surfaces / issue #119
- Requirements: REQ-133 (accepted), REQ-136 (accepted, D-2026-08-27-b)
- Constraints:  CON-06 smallest change; ADR-039 no ISurface
- Acceptance:   REQ-133 path preview from a viewport pick; REQ-136 volume TIN from two parents
- Owning subsystem: Viewport pick policy; util/tinvolume; Commands; UI; IO

## 2. Scope
- In scope: ViewportClickRouteFor WaterDrop/Catchment; VOLUMESURFACE; util/tinvolume; .gs names
- Out of scope: grid/corridor; volume-of-volume stacking; drawing volume reports
- Smallest change: route clicks; one CadSurface with two parent names

## 3. Architectural boundary check
- [x] No — one CadSurface, util module, additive .gs fields (recorded D-2026-08-27-b)

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Volume surface Z convention | inferred | comparison minus base (Civil 3D) |

## 5. Assumptions
```
ASSUMPTION-1: Difference samples are unique plan vertices of either parent that both TINs cover.
- Because: REQ-136 does not specify a regular grid overlay.
- Risk if wrong: sparse difference TINs on mismatched meshes.
- Validate by: planar 5 ft fixture.
```

## 6. Plan
- Approach: fix Ignore routing; BuildTinVolumeSurface; VOLUMESURFACE + manager popup
- Files: ViewportPickPolicy.hpp, tinvolume.*, CadEntities.hpp, CadCommands.*, GsIo, CadUi_Surfaces
- Tests: ViewportPickPolicyTests; TinVolumeTests; req133-waterdrop-click; req136-volume-surface

## 7. Workflow-specific notes
- Bug root cause: WaterDrop/Catchment omitted from ViewportClickRouteFor → Ignore (TASK-099 class)
- Feature: D-2026-08-27-b recorded before code

## 8. Implementation log
- 2026-08-27 implement

## 9. Self-verification
- [x] build-project        — PASS (ninja-release)
- [x] architecture-review  — PASS (no ISurface; D-2026-08-27-b recorded)
- [x] code-review          — PASS
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a
- [x] testing              — PASS TinVolumeTests, ViewportPickPolicy, req133-waterdrop-click, req136-volume-surface

## 10. Verification result
- Submitted:  2026-08-27
- Verdict: PASS
