# TASK-134 — Add and delete TIN definition points

- Type:    feature
- Status:  done
- Opened:  2026-08-28
- Owner:   Workshop

## 1. Authority
- Goal:         GitHub #119 editing ACs (D-2026-08-28-a)
- Requirements: REQ-144 (accepted); REQ-201 (named refusals)
- Constraints:  CON-07; immutable `shared_ptr<const CadTin>` (rebuild + pointer swap, same as REQ-139)
- Acceptance:
  - adding a typed XYZ point to an empty TIN and rebuilding produces a surface that includes that vertex;
  - deleting the nearest definition point and rebuilding removes it;
  - a miss on a non-TIN kind, a missing name, or an empty surface with nothing to delete is a named refusal and does not mutate the TIN;
  - added and deleted picks persist in `.gs` and reapply after `SURFACEREBUILD`.
- Owning subsystem: Domain (`CadSurface` definition), Commands, IO, UI (ribbon/Toolspace)

## 2. Scope
- In scope:        `SURFACEADDPOINT` / `SURFACEDELPOINT`, definition lists, rebuild, ribbon Edit Surface, Toolspace Edits, Surface Properties operation list, `.gs`
- Out of scope:    mutating live TIN vertices without recording an edit; grid/corridor/volume kinds; Civil 3D point objects
- Smallest change: two definition vectors + same pick/command shape as `SURFSWAPEDGE`

## 3. Architectural boundary check
- [x] No — proceed (optional `.gs` keys like `swappedEdgePicks`; no new triangulation algorithm).

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Pick Z for Add Point | 2026-08-28 | Work-plane elevation (`CadCommitElevation`); typed command requires Z |

## 5. Assumptions
```
ASSUMPTION-1: Delete removes the nearest assembled input point (groups + files + added), then rebuilds.
- Because:       Live CadTin is immutable; Civil 3D edits the definition.
- Risk if wrong: a far pick deletes an unexpected vertex
- Validate by:   typed XY in the transcript matches a known corner
```

## 6. Plan
- Approach: store local XYZ adds and local XY delete picks; `ResolveSurfaceInputs` applies them before triangulation.
- Files: CadEntities.hpp, CadCommands.hpp/.cpp, GsIo.cpp, CadUi.cpp, CadUi_Toolspace.cpp, CadUi_SurfaceProperties.cpp, ViewportPickPolicy.hpp, headless transcript, spec
- Test: happy = four adds then one delete; failure = grid kind + missing name
- Steps:
  - [x] Spec REQ-144
  - [x] Commands + resolve + persist + UI
  - [x] Tests + build

## 7. Workflow-specific notes
- Feature: `req144-surface-add-del-point` (add 4, delete 1, save/reopen, empty delete, grid refuse).

## 8. Implementation log
- 2026-08-28 plan written; implemented; transcript PASS 27 steps.

## 9. Self-verification
- [x] build-project        — PASS `cmd /c build.bat`
- [x] architecture-review  — PASS (definition lists, no live TIN mutation)
- [x] code-review          — PASS
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a
- [x] testing              — PASS `gosurvey_headless` req144; ViewportPickPolicyTests still green

## 10. Verification result
- Submitted:  2026-08-28
- Verdict: PASS

```
COMPLETION REPORT — TASK-134 — 2026-08-28
- Requirements satisfied: REQ-144 (Acceptance met: yes)
- Summary: TIN add/delete as definition edits (SURFACEADDPOINT / SURFACEDELPOINT), persist in .gs, ribbon and Toolspace.
- Tests: req144-surface-add-del-point (happy + failure-mode, run green)
- Verification verdict: PASS (findings resolved: none)
- Assumptions: ASSUMPTION-1 documented
- Architectural decisions: none made by Workshop (escalated: none)
- Dependencies: none
- Technical debt noted: none
- Build: reproducible, clean on target platform
- Docs updated: spec/requirements.md, spec/project.md (D-2026-08-28-e)
```
