# TASK-toolspace-tabs-1040 — Qt Toolspace: Project, Prospector and Settings tabs

- Type:    feature
- Status:  submitted
- Opened:  2026-10-28
- Owner:   chetjones003 (decision), Claude (implementation)
- GitHub:  #1040 (labels: enhancement, QT-migration; `blocked` removed after the SPEC amendment)
- Branch:  feat/issue-1040-toolspace-tabs (from origin/beta)

## 1. Authority
- Goal:         GOAL-NN from spec/project.md (Qt parity, Toolspace)
- Requirements: REQ-142 (amended by D-2026-10-28-a), REQ-379 clauses 3 and 6 (wording now "Project tab")
- Constraints:  CON for Qt UI and no new dependencies (none added)
- Acceptance (from REQ-142 as amended, and issue #1040):
  - Toolspace presents Project, Prospector and Settings as separate selectable tabs.
  - Each tab shows only its own data: projects/tracked items/open drawings (Project); the active drawing's objects (Prospector); drawing-level style tables (Settings).
  - Project tab uses REQ-379 behaviour: Refresh (explicit, asks before tracking) and Project Health, both run the File-menu actions.
  - Prospector keeps the existing object tree and context menus for surfaces and styles (moved to the Settings tree).
  - Settings lists only drawing-level tables (Text Styles, Layers, Dimension Style, Surface Styles); no button or entry opens app-wide Settings.
  - Switching tabs changes no project or drawing data.
  - Empty states: "No project is open"; "No drawing is open".
- Owning subsystem: UI (src/qt). Command layer (ToolspaceCatalog, TOOLSPACE verb) already had the Project tab value and was not changed.

## 2. Scope
- In scope:
  - Three QTreeWidgets (one per tab) and a Project page with Refresh and Project Health buttons (src/qt/EditorWindow.cpp, .hpp).
  - Tab selection kept in sync with `AppCommandState::toolspaceTab` so `TOOLSPACE PROJECT|PROSPECTOR|SETTINGS` selects the tab in Qt.
  - Context menu takes the clicked tree (src/qt/EditorSurfaces.cpp).
  - Ribbon tooltips and in-app wiki line that name the tabs.
  - Test update for the moved Surface Styles tree (tests/QtEditorTests.cpp).
- Out of scope:
  - Win32 Toolspace (src/ui/CadUi_Toolspace.cpp) — not changed.
  - Prospector root node named after the drawing and Point Groups / Feature Lines in the Qt tree (REQ-142 lists them; the Qt tree does not show them today). Pre-existing gap, recorded in D-2026-10-28-a.
  - `TOOLSPACE LIST` on the Project tab printed Prospector lines; fixed in review commit 999385c9 (the Project list is now in ToolspaceCatalog).
  - Editors for text styles, layers and dimension style (display only).

## 3. Architectural boundary check
- [x] No — proceed. No new abstraction, dependency, global state, or data-format change. Helper functions are file-local in EditorWindow.cpp.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| 1 | Project tab as a separate tab, or a section in Prospector? | 2026-10-28 | Option A: separate tab (owner) |

## 5. Plan
1. Amend REQ-142 and REQ-379 wording; record D-2026-10-28-a. (done)
2. Split the Qt dock into three trees; move project list and open drawings to Project; keep objects in Prospector; move style tables to Settings.
3. Buttons: Refresh → `REFRESH`, Project Health → `PROJECTHEALTH` (same verbs the File menu uses); enabled only when the active drawing belongs to a project (Refresh also not read-only).
4. Tests: update the Toolspace style test; build; run Qt editor tests; capture screenshots of Project, Prospector and Settings tabs (normal and empty states) for review.

## 6. Verification status
- Build: see completion report.
- Tests: see completion report.
- Screenshots: pending owner approval (CLAUDE.md §3 step 6).

## 7. Assumptions and technical debt
- Decided by the owner (2026-10-28, after the independent review): the Project tab lists only drawings that belong to a project. Point Groups were added to the Prospector tree.
- Assumption: the dock is hidden when no drawing is open (existing behaviour), so the "No drawing is open" placeholder is only visible if the dock is shown by other means.
- Resolved: `TOOLSPACE LIST` after `TOOLSPACE PROJECT` printed Prospector lines; now prints the Project list (src/commands/CadCommands.cpp dumpTree, src/commands/ToolspaceCatalog.hpp).
- Debt: Qt Prospector omits the root drawing node and Feature Lines required by REQ-142. Point Groups are listed but have no right-click menu yet (REQ-142 asks for Properties on a named group).

## 8. Completion report
- Status: submitted (PASS against the amended REQ-142 acceptance list, subject to the PR review).
- Build: `.\build.bat` (Release) succeeded with no errors or warnings.
- Tests: full suite `ctest --preset ninja-release` passed 191 of 191 before the final tree-expansion fix; after that fix the six `qt.editor` shards were rerun and all pass. Two existing tests were updated for the new layout (named trees, drawing opened for the Surface Styles test) and one capture test was added (`issue1040ToolspaceTabsCaptureForReview`).
- Screenshots: `build/artifacts/qt-editor/manual/shard-3/artifacts/issue1040-toolspace-{project,prospector,settings}.png`; review page `build/artifacts/issue1040-toolspace-review.html`. Approved by the owner on 2026-10-28.
- Pre-PR documentation review (REQ-453):
  - `README.md`: no Toolspace text; no update needed.
  - `PRIVACY.md`: no Toolspace text; no update needed.
  - `resources/wiki/User-Interface.md`: updated (tab list).
  - GitHub wiki (`GoSurvey.wiki`): cloned and searched; no Toolspace or Prospector text; no update needed.
- Assumptions and debt: see section 7.
- PR: to be recorded when opened.
