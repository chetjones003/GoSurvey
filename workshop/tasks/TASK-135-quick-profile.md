# TASK-135 — Quick Profile from a TIN (no alignments)

- Type:    feature
- Status:  done
- Opened:  2026-08-28
- Owner:   Workshop

## 1. Authority
- Goal:         GitHub #119 / user 2026-08-28: Quick Profile first; alignments are a future issue
- Requirements: REQ-145 (accepted, D-2026-08-28-g); REQ-201; REQ-084 (Create Profile stays disabled)
- Constraints:  CON-07; no new document entity; no `.gs` fields
- Acceptance:   restated from REQ-145
- Owning subsystem: util (sample), Commands (QUICKPROFILE), UI (session graph)

## 2. Scope
- In scope:        two-point sample of a named surface; log + ImGui graph; ribbon Quick Profile
- Out of scope:    alignments, profile views, paper-space profile objects, Create Profile, grading
- Smallest change: `ISurfaceQuery::elevationAt` along a segment + session panel (Volume Dashboard shape)

## 3. Architectural boundary check
- [x] No — proceed (free function on existing `ISurfaceQuery`; session state like Volume Dashboard).

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Quick Profile vs Civil 3D alignment/profile/profile view | 2026-08-28 | Quick Profile first; alignments are a future issue |

## 5. Assumptions
```
ASSUMPTION-1: Sample step is 1 ft, capped at 4096 samples, always including both endpoints.
- Because:       Civil 3D has a sample increment; we need a fixed bound (REQ-100).
- Risk if wrong: coarse graph on a long line
- Validate by:   5 ft grid row produces 6 samples at 1 ft
```

## 6. Plan
- Approach: `SampleSurfaceProfileLine`; `QUICKPROFILE`; session `QuickProfileState`; ribbon; Catch2 + transcript
- Files: surfacequery.hpp/.cpp, CadCommands, CadUi ribbon, CadUi_QuickProfile.cpp, main.cpp, CMakeLists, tests
- Test: plane Z=X midpoint; miss; zero-length refusal
- Steps:
  - [x] Spec
  - [x] util + commands + UI
  - [x] tests + build

## 7. Workflow-specific notes
- Feature: `SurfaceProfileTests [req145]`; `req145-quick-profile`.

## 8. Implementation log
- 2026-08-28 user chose Quick Profile; D-2026-08-28-g; implemented and tests green.

## 9. Self-verification
- [x] build-project — PASS
- [x] architecture-review — PASS (session UI, no Alignment entity)
- [x] code-review — PASS
- [x] dependency-audit — n-a
- [x] performance-review — n-a (sample cap 4096)
- [x] testing — PASS Catch2 + transcript

```
COMPLETION REPORT — TASK-135 — 2026-08-28
- Requirements satisfied: REQ-145 (Acceptance met: yes)
- Summary: QUICKPROFILE samples a named surface along two plan points and shows a session graph.
- Tests: SurfaceProfileTests [req145]; req145-quick-profile
- Verification verdict: PASS
- Assumptions: ASSUMPTION-1
- Architectural decisions: none by Workshop (D-2026-08-28-g)
- Dependencies: none
- Technical debt noted: none
- Build: clean
- Docs updated: spec/project.md, spec/requirements.md
```
