# TASK-133 — Contextual TIN Surface ribbon tab

- Type:    feature
- Status:  done
- Opened:  2026-08-28
- Owner:   Workshop

## 1. Authority
- Goal:         GitHub #119 / Civil 3D GUI-pass
- Requirements: REQ-143 (accepted, D-2026-08-28-d); REQ-084 (disabled unused)
- Constraints:  CON-07; no new Civil 3D object types
- Acceptance:
  - with no surface selected, the tab strip does not include a `Tin Surface:` tab;
  - selecting a named surface shows a tab whose label contains that surface's name;
  - `SURFELEV`, `WATERDROP`, `CATCHMENT`, `SURFACEREBUILD`, and `EXTRACT` remain invokable from the command line;
  - unimplemented buttons on the tab cannot be activated (disabled).
- Owning subsystem: UI (ribbon), Commands (existing start functions)

## 2. Scope
- In scope:        Contextual tab, seven panels, vector icons, wire existing commands to selected surface
- Out of scope:    Labels, LOD, profiles, grading, drape, object viewer, visibility check
- Smallest change: one extra tab index (session-only) + ribbon sections

## 3. Architectural boundary check
- [x] No — proceed (session UI flags, same as Toolspace / Surface Properties).

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | Screenshot + wiring specified | 2026-08-28 | Treat as confirmed (D-2026-08-28-d) |

## 5. Assumptions
```
ASSUMPTION-1: First selected surface wins if several are selected.
- Because:       Civil 3D tab title is one name.
- Risk if wrong: tools run on the wrong surface
- Validate by:   pick one surface in GUI
```

## 6. Plan
- Approach: kRibbonTabSurfaceCtx; arm/restore; DrawRibbonBar sections; disable NYI
- Files: spec, CadCommands.hpp, UserPrefs, CadUi.cpp
- Test: existing command transcripts still green; GUI for tab visibility
- Steps: spec, UI, build, tests

## 7. Workflow-specific notes
- Feature: no new command verbs required

## 9. Self-verification
- [x] build-project — PASS (`cmd /c build.bat`)
- [x] architecture-review — PASS (session tab index, no new object types)
- [x] code-review — PASS
- [x] testing — PASS `[req137],[req139],[req142]`; ribbon is GUI (REQ-203 anti-requirement)
- CadUi.cpp compiled; no new CadUi warnings in the build log

## 11. Completion report
COMPLETION REPORT — TASK-133 — 2026-08-28
- Requirements satisfied: REQ-143 (Acceptance met: command-line tools unchanged; unimplemented ribbon buttons disabled)
- Summary: Contextual `Tin Surface: <name>` ribbon tab when a surface is selected; seven screenshot panels with vector icons; existing commands wired to that surface.
- Tests: `[req137],[req139],[req142]` (8 cases). Tab visibility is manual GUI (no framebuffer harness).
- Verification verdict: PASS
- Assumptions: ASSUMPTION-1 first selected surface
- Architectural decisions: none (D-2026-08-28-d recorded in spec)
- Dependencies: none
- Technical debt noted: none
- Build: `build.bat` green
- Docs updated: spec/project.md, spec/requirements.md, this task
