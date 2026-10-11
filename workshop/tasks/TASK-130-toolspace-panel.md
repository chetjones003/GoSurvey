# TASK-130 — Toolspace Prospector and Settings panel

- Type:    feature
- Status:  done
- Opened:  2026-08-28
- Owner:   Workshop

## 1. Authority
- Goal:         Civil 3D-shaped drawing explorer for objects GoSurvey already has
- Requirements: REQ-142 (accepted 2026-08-28, D-2026-08-28-b)
- Constraints:  CON-07; architecture §2 (UI over Commands stores); REQ-301 (no new interface)
- Acceptance:   as written on REQ-142 in spec/requirements.md
- Owning subsystem: UI, Commands

## 2. Scope
- In scope:        Dockable TOOLSPACE; Prospector + Settings tabs; trees of points/groups/surfaces/feature lines
                   and text/layer/dim/surface styles; TOOLSPACE command + LIST
- Out of scope:    Survey/Toolbox tabs; Civil 3D collections not in GoSurvey; persisting the panel in `.gs`
- Smallest change: one ImGui window + catalog helpers over existing vectors; command for REQ-203

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change /
  global state / public-API or data-format change / algorithm the spec didn't
  specify?
    - [x] No — proceed. Session flags on AppCommandState; reads existing stores.
    - [ ] Yes → STOP.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Full Civil 3D tree vs implemented objects only? | 2026-08-28 | Implemented only (user) |
| Q2 | Tabs besides Prospector and Settings? | 2026-08-28 | Not yet (user) |

## 5. Assumptions
```
ASSUMPTION-1: Preview strip stays empty.
- Because:       screenshots show an unused lower pane; no requirement names its content.
- Risk if wrong: later fill-in is a new REQ
- Validate by:   user looking at the panel
```

## 6. Plan
- Approach:     Catalog functions list labels from AppCommandState; ImGui draws Civil-like chrome;
                TOOLSPACE drives show/tab/LIST for headless.
- Files:        spec (done in this task); ToolspaceCatalog.hpp; CadUi_Toolspace.cpp; CadCommands.cpp;
                CadUi.hpp/cpp dock+menu; CMakeLists; tests + transcript
- Test approach: happy = LIST names implemented collections and created objects; failure = unknown verb +
                 forbidden Civil 3D names absent
- Steps:
  - [x] REQ-142 + D-2026-08-28-b
  - [x] catalog + command + UI
  - [x] tests

## 7. Workflow-specific notes
- Feature: verification APPROVE — UI overlay, no new document type, no dependency.

## 8. Implementation log
- 2026-08-28 plan + spec recorded; implemented CadUi_Toolspace + TOOLSPACE command
- 2026-08-28 Catch2 `[req142]` 4/4; headless `req142-toolspace` PASS

## 9. Self-verification
- [x] build-project        — PASS (`build.bat`)
- [x] architecture-review  — PASS (UI overlay, no `.gs` fields)
- [x] code-review          — PASS
- [x] dependency-audit n-a
- [x] performance-review n-a
- [x] testing              — PASS `[req142]` + `req142-toolspace.txt`

## 10. Verification result
- Submitted:  2026-08-28
- Verdict: PASS

COMPLETION REPORT — TASK-130 — 2026-08-28
- Requirements satisfied: REQ-142 (Acceptance met: yes)
- Summary: Dockable TOOLSPACE with Prospector and Settings; trees list only implemented objects
- Tests: `[req142]` (4 cases); `req142-toolspace.txt`
- Verification verdict: PASS
- Assumptions: ASSUMPTION-1 preview strip empty
- Architectural decisions: none (D-2026-08-28-b)
- Dependencies: none
- Technical debt: toolbar uses placeholder glyphs rather than Civil 3D bitmaps
- Build: `build.bat` green
- Docs updated: spec/project.md, requirements.md, roadmap.md
