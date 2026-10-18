# TASK-901 — Complete Qt annotation scale, isolation, and perspective workflows

- Type: feature
- Status: implemented
- Opened: 2026-10-18
- Owner: Codex

## 1. Authority
- Goal: Qt migration parity tracker #878, target beta 0.7.4.
- Requirements: REQ-424 (accepted), REQ-084 (accepted), REQ-309 (accepted), REQ-201 (accepted).
- Constraints: preserve beta 0.7.4 behavior; do not switch the production default or remove ImGui; no new dependency or document format change.
- Acceptance: Qt completes the full annotation-scale, object-isolation, and perspective workflows with the applicable validation, cancel, failure, undo, read-only, persistence, and picking behavior of beta 0.7.4; Qt workflow tests cover these paths; update the parity ledger.
- Owning subsystem: Qt editor UI and interaction; existing command, document, renderer, and IO services own domain behavior.

## 2. Scope
- In scope: finish the three named Qt workflows, add focused Qt workflow coverage, update the 0.7.4 parity ledger, and prepare 2–3 screenshot options per major affected surface for user direction.
- Out of scope: changing legacy command semantics or drawing storage, changing the production executable/default, or migrating unrelated controls.
- Smallest change: use existing shared command/document/render/IO operations and the current Qt editor surfaces; add only the Qt-facing state and interaction needed to make each entire workflow usable.

## 3. Architectural boundary check
- New abstraction, layer, dependency, ownership change, global state, public API, data format, or algorithm: No. Existing behavior is exposed through the existing Qt editor and shared services. Stop if implementation proves otherwise.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | What scope should issue #901 cover? | 2026-10-18 | Full Qt parity for all three named workflows, confirmed by user; recorded as D-2026-10-18-a / REQ-424. |

## 5. Assumptions
- None. The beta 0.7.4 executable is the behavior reference where the existing specification does not spell out an annotation-scale control detail.

## 6. Plan
- Approach: inspect the legacy workflow and current Qt gaps; reuse shared services; complete each interaction in the Qt editor; test Qt behavior and resulting state; prepare integrated screenshots for user direction.
- Files/functions to touch: `src/qt/EditorRibbon.*`, `src/qt/EditorWindow.*`, `src/qt/EditorViewport.*`, relevant Qt dialogs/state, `tests/QtEditorTests.cpp`, `docs/qt-ui-migration.md`, and requirement/decision/traceability docs.
- Test approach: happy path checks Qt scale changes and persistence, isolate/hide/unisolate behavior, and projection/FOV controls and view persistence; failure cases check invalid values, cancellation, read-only rejection, hidden-object picking, and unchanged state on failure; cover undo/redo where supported by beta 0.7.4.
- Steps:
  - [x] Record accepted requirement and task plan.
  - [x] Compare 0.7.4 behavior against Qt implementation and identify gaps.
  - [x] Implement Qt workflow parity and focused coverage.
  - [x] Update parity ledger and prepare screenshot options.
  - [x] Run build, focused/full Qt tests, and self-verification.

## 7. Workflow-specific notes
- Feature: user confirmed scope; existing Qt code is the implementation baseline. Add focused coverage before/with each behavior.

## 8. Implementation log
- 2026-10-18 — Confirmed issue scope with user; added accepted REQ-424 and D-2026-10-18-a.
- 2026-10-18 — Found initial T3 checkout lacked Qt; created this task branch from `origin/test/win32UI`, the migration branch specified by parent tracker #878.
- 2026-10-18 — Completed annotation-scale state/rendering and status-bar selector, selected-object annotative properties, stable-ID isolation rendering, and perspective/FOV controls. Applied the selected UI direction: status bar scale selector, current property section, current isolation ribbon menu.

## 9. Self-verification
- [x] build-project
- [x] architecture-review
- [x] code-review
- [ ] dependency-audit — n-a unless dependencies change
- [x] performance-review — annotation scale filtering reuses one active-scale lookup per render pass; isolation relies on the existing stable-ID picking path
- [x] testing

## 10. Verification result
- Submitted: 2026-10-18
- Verdict: implemented; targeted Qt checks pass
- Findings: `build-qt.bat GoSurveyQtTests` succeeds. Focused #901 QtTest report: 5 passed, 0 failed, 0 skipped. Full QtTest report: 41 passed, 1 failed, 0 skipped; the failure is the unrelated startup focus assertion in `tabsAppearInitiallyAndTypingRoutesFromEditorChrome` (`tests/QtEditorTests.cpp:411`). Final diff review found no additional issue.

## 11. Outcome
- Requirements satisfied: REQ-424
- Tests added: Qt scale/projection, annotation filtering/rendering, property undo, and isolate/hide/unisolate workflows
- Docs updated: requirement, decision log, traceability, parity ledger
- Done: implemented; unrelated full-suite focus failure remains recorded
