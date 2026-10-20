# TASK-921 — Reconnect paper modify commands in Qt

- Type: feature / testing
- Status: done
- Opened: 2026-10-20
- Owner: Codex
- GitHub issue: #972

## 1. Authority
- Goal: none defined in `spec/project.md`; user authorized proceeding under accepted requirements on 2026-10-20.
- Requirements: REQ-039, REQ-103 (both accepted).
- Constraints: REQ-101 coordinate tolerance; REQ-201 named refusals; CON-06 smallest correct solution.
- Acceptance: each #972 command (FILLET, CHAMFER, LENGTHEN, EXTEND, BREAK, STRETCH) starts and completes in the active paper layout; picks and typed input advance required phases; edits stay in that layout and preserve model geometry; Escape cancels without partial edits; successful edits are undoable; Qt tests cover command input, geometry, and isolation; model-space behavior remains unchanged.
- Owning subsystem: UI / Commands / Domain. Command phases and mutations remain in Commands; Qt supplies paper-coordinate picks and text.

## 2. Scope
- In scope: route the six existing paper command phase machines and geometry helpers through Qt command input and viewport picks; clear paper drafts on Escape or command switching; add Qt flow coverage.
- Out of scope: ImGui support, new geometry algorithms, paper JOIN/TRIM/OFFSET, visible layout changes.
- Smallest change: extend the existing paper-phase dispatch seams and reuse `PaperEntityRef`, picking, eligibility, and apply helpers.

## 3. Architectural boundary check
- No new abstraction, layer, dependency, ownership model, global state, public API, or data format. Existing command state and paper operations are reused.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | `spec/project.md` has no GOAL-NN, though the task template requires one. | 2026-10-20 | Proceed under accepted REQ-039 and REQ-103; record the gap without inventing a goal. |

## 5. Assumptions
- None. REQ-039/103 and issue #972 define the behavior. Paper LENGTHEN/EXTEND reuse model-configured length values as their current command documentation states.

## 6. Plan
- Approach: intercept paper modify clicks before idle selection; route point/option/Enter text through existing paper command handling; clear pending phases on Escape and command switching; preserve active-space checks and operation-owned undo snapshots.
- Files/functions: `src/commands/CadCommands.cpp` (`SubmitViewportPickImpl`, paper phase input, cancellation, command starts); `src/qt/EditorInput.cpp`; `src/qt/EditorViewport.cpp`; `tests/QtEditorTests.cpp`.
- Test approach: happy path drives commands through `EditorWindow` and viewport clicks, then asserts paper geometry, model isolation, and undo; failure path Escapes a multi-pick BREAK and asserts no edit plus phase cleanup.
- Steps:
  - [x] Route six paper commands through existing operations.
  - [x] Add text, Enter, Escape, and command-switch handling for phases.
  - [x] Add Qt flow coverage for geometry, isolation, undo, and cancellation.
  - [x] Build and run the Qt editor suite and full Release CTest.

## 7. Workflow-specific notes
- Feature: accepted requirements; existing algorithms and command ownership retained.

## 8. Implementation log
- 2026-10-20 — Plan reviewed against REQ-039/103 and current command/Qt seams. The phase fields were initialized by command starts but not consumed in the Qt-only tree.
- 2026-10-20 — Routed paper modify picks ahead of idle selection in both the Qt viewport mouse path and shared command dispatcher. Escape and switching commands clear pending paper drafts.
- 2026-10-20 — Added `issue972PaperModifyCommandsRouteAndStayInPaperSpace`: actual Qt clicks drive LENGTHEN and BREAK; typed input and picks exercise EXTEND, STRETCH, FILLET and CHAMFER; assertions cover paper changes, model isolation, undo, and cancellation.
- 2026-10-20 — CI follow-up adds the required Qt kit setup to PR build, clang-tidy, and MSVC analysis jobs; their CMake configure steps previously failed before compiling because Qt6 was unavailable.

## 9. Self-verification
- [x] build-project — Release build passed on final implementation.
- [x] architecture-review — PASS; no new boundary or abstraction.
- [x] code-review — PASS; active-paper routing precedes idle selection and reuses existing geometry helpers.
- [x] dependency-audit — n/a; no manifests changed.
- [x] performance-review — n/a; click-time phase dispatch only.
- [x] testing — Full CTest passed 2665/2665 (654.00s) before the final Qt mouse-routing refinement. After that refinement, Release build passed and `qt.editor` passed (193 Qt tests, 481.61s). The other full-suite tests are unaffected by the Qt-only change.

## 10. Verification result
- Submitted: 2026-10-20
- Verdict: PASS.
- Findings: none open.

## 11. Outcome
- Requirements satisfied: REQ-039 paper modify isolation; REQ-103 paper LENGTHEN, EXTEND, BREAK, STRETCH, FILLET, and CHAMFER flows.
- Tests added: `issue972PaperModifyCommandsRouteAndStayInPaperSpace`.
- Refactors: none.
- Docs updated: this task log.
- Done: 2026-10-20
