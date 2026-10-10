# TASK-139 — Contextual SURVEY Point(s) ribbon tab

- Type:    feature
- Status:  done
- Opened:  2026-08-28
- Owner:   Workshop

## 1. Authority
- Goal:         Civil 3D GUI-pass (COGO Point / COGO Points contextual tab)
- Requirements: REQ-153 (accepted, D-2026-08-28-l); REQ-084 (disabled unused)
- Constraints:  CON-07; no new Civil 3D object types; Object Viewer omitted (D-2026-08-28-f)
- Acceptance:
  - with no survey point selected, the tab strip does not include a `SURVEY Point` tab;
  - one selected point: `SURVEY Point: <id>`;
  - more than one: `SURVEY Points`;
  - Object Viewer is not present;
  - unimplemented buttons cannot be activated;
  - CREATEPOINTS / VIEWPOINTS / IMPORTPOINTS / EXPORTPOINTS / ID remain on the command line.
- Owning subsystem: UI (ribbon), Commands (existing start functions)

## 2. Scope
- In scope:        Contextual tab, screenshot panels, COGO→SURVEY naming, wire existing commands
- Out of scope:    Point tables, label text editor, renumber, datum, lock, geodetic, transfer
- Smallest change: one extra session-only tab index + ribbon sections

## 3. Architectural boundary check
- [x] No — proceed (same session-tab pattern as REQ-143).

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | Screenshots + SURVEY naming + omit Object Viewer | 2026-08-28 | Confirmed (D-2026-08-28-l) |

## 5. Assumptions
```
ASSUMPTION-1: First valid selected survey point supplies the singular tab id.
- Because:       Civil 3D shows one point number in the singular title.
- Risk if wrong: title shows a different id than the Properties panel
- Validate by:   pick one point in GUI

ASSUMPTION-2: Do not steal the TIN Surface contextual tab when points become selected.
- Because:       user instruction / REQ-153
- Risk if wrong: surface tools disappear while a surface is still selected
- Validate by:   select surface then add a point to selection
```

## 6. Plan
- Approach: kRibbonTabSurveyPointCtx; arm/restore; DrawRibbonBar sections; disable NYI
- Files: spec, CadCommands.hpp, UserPrefs, CadUi.cpp
- Test: existing command transcripts still green; GUI for tab visibility
- Steps: spec, UI, build

## 7. Workflow-specific notes
- Feature: no new command verbs

## 9. Self-verification
- [x] build-project — PASS (`cmd /c build.bat`)
- [x] architecture-review — PASS (session tab index, same as REQ-143)
- [x] code-review — PASS (Object Viewer omitted; NYI disabled; lambdas recompute widths)
- [x] testing — ribbon is GUI (REQ-203 anti-requirement); command-line verbs unchanged

## 11. Completion report
COMPLETION REPORT — TASK-139 — 2026-08-28
- Requirements satisfied: REQ-153 (Acceptance met: yes — titles, omit Object Viewer, NYI disabled, existing commands remain)
- Summary: Contextual `SURVEY Point: <id>` / `SURVEY Points` ribbon tab when survey points are selected; Civil 3D COGO Point panels with SURVEY naming; Object Viewer omitted.
- Tests: GUI tab visibility (no framebuffer harness). Build green.
- Verification verdict: PASS
- Assumptions: ASSUMPTION-1 first valid point id; ASSUMPTION-2 do not steal TIN Surface tab
- Architectural decisions: none (D-2026-08-28-l recorded in spec)
- Dependencies: none
- Technical debt noted: none
- Build: `build.bat` green
- Docs updated: spec/project.md, spec/requirements.md, this task
