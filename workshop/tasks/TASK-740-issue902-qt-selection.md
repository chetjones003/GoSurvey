# TASK-740 — Port selection gestures and grip editing to Qt

- Type: feature
- Status: complete
- Opened: 2026-10-18
- Owner: Codex

## 1. Authority
- Goal: Qt migration parity tracker #878 (preserve beta 0.7.4 behavior; additive Qt executable)
- Requirements: proposed REQ-425 (Qt selection cycling and model-space grips); REQ-039 (paper-space selection/grips), REQ-047 (grip ORTHO and typed distance), REQ-054 (selection context actions), REQ-060 (3D manipulation), REQ-121 (selection-step input), REQ-318 (3D sub-object selection), REQ-370 (selection box preview/appearance)
- Constraints: CON-06 (smallest sufficient change; no speculative abstractions); no production-default switch or ImGui removal under #878
- Acceptance: preserve applicable cancel, validation, failure, undo, and read-only behavior; fully usable Qt workflows; update the 0.7.4 parity record and add focused workflow coverage. Provide 2–3 integrated screenshot options for each major affected UI surface and apply the user's chosen direction before completion.
- Owning subsystem: Qt presentation/input (`src/qt/EditorViewport.*`, `src/qt/EditorWindow.*`) using existing Commands, selection, grip, and render APIs.

## 2. Scope
- In scope: overlapping-pick disambiguation and selection cycling; click, additive/toggle, window/crossing drag, and cancellation gestures; existing 2D entity grip display/drag; Ctrl+click 3D sub-object selection and the supported advanced selection paths; focused Qt workflow tests; update `docs/qt-ui-migration.md`'s input parity record.
- Out of scope: production executable/default changes; removing ImGui; replacing shared selection or geometry algorithms; 3D gizmo and section-plane overlays assigned to #900; unsupported new geometry/edit operations.
- Smallest change: route Qt viewport events through existing selection/pick/grip functions and expose the already-defined state transitions in native Qt UI. Keep shared domain behavior unchanged unless a demonstrated Qt integration defect requires a small fix.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / global state / public API / data format / algorithm required: **No, based on current inspection.** Existing `AppCommandState`, `PickClosestCadEntity`, overlap candidate state, `SubmitSubObjectPick`, and entity-grip functions provide the behavior. Qt owns event capture and presentation.
- Boundary: do not duplicate domain picking or grip math in Qt; call the existing APIs. Keep gizmo and section-plane overlay rendering in #900.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | The accepted REQs do not define the released overlapping-pick selector or model-space entity-grip workflow. Should these be recorded as a new accepted REQ before porting, or should issue #902 be narrowed to behavior already specified? | 2026-10-18 | User chose to draft and review a new requirement; accepted REQ-425 on 2026-10-18. |

## 5. Assumptions
ASSUMPTION-1: The existing beta UI behavior is the expected 0.7.4 interaction reference for selection cycling and gestures not fully restated in an individual REQ.
- Because: #878 explicitly requires preserving existing 0.7.4 behavior, and `src/ui/CadUi.cpp` implements the released interactions.
- Risk if wrong: the port could preserve a legacy detail that should have changed.
- Validate by: compare event outcomes and cancellation against the current legacy implementation and release notes; ask the user if a conflict with an accepted REQ appears.

## 6. Plan
- Approach: implement Qt event routing and native selection UI over existing selection and grip APIs; preserve legacy modifier semantics and transaction boundaries; avoid new domain state.
- Files/functions to touch: `src/qt/EditorViewport.hpp/.cpp`, `src/qt/EditorWindow.hpp/.cpp`, `tests/QtEditorTests.cpp`, `docs/qt-ui-migration.md`.
- Test approach: happy path = click/shift selection, window and crossing drag, overlap candidate choice, grip edit commit, and 3D sub-object selection; failure/cancel = empty pick, Escape/right-click during a drag, refused grip or sub-object pick, read-only project, and undo restoring the original geometry/selection behavior.
- Steps:
  - [x] Complete verification of the plan against the Qt and legacy event paths, including #900 overlay boundaries.
  - [x] Implement gesture, overlap selection, grip, and advanced 3D selection routing in the Qt viewport.
  - [x] Add focused Qt integration coverage and update the migration parity record.
  - [x] Produce 2–3 integrated screenshot options for each major affected UI surface; apply the user's selected visual direction (wide layer-aware overlap chooser, blue square grips/translucent fence, modeless Selection… list).
  - [ ] Self-verify and record results.

## 7. Workflow-specific notes
- Feature: user confirmed scope and accepted REQ-425; implementation and focused Qt verification are complete.

## 8. Implementation log
- 2026-10-18: Inspected issue #902, parent tracker #878, accepted selection requirements, Qt viewport event routing, and legacy selection/grip behavior. No implementation code changed.
- 2026-10-18: Verification found a SPEC GAP: no accepted REQ defines overlapping-pick selection cycling or model-space entity-grip behavior. Stopped before implementation pending a recorded decision.
- 2026-10-18: User accepted REQ-425; recorded D-2026-10-18-b in `spec/project.md` and updated the traceability matrix. Proceeding with implementation.
- 2026-10-18: Qt viewport now routes visible clicks/hover through the shared resolver; adds the overlap menu, click-drag selection box, Ctrl+click sub-object selection/pre-highlight, model-space grips, status toggle, and modeless Selection… snapshot dialog. Added focused Qt coverage for overlap choice, crossing drag/cancel, grip drag/undo/Escape, and selection-list/toggle behavior. Updated the parity ledger.
- 2026-10-18: Qt 6.8.3 configuration and Debug/Release `GoSurveyQtTests` builds succeeded. The Windows platform plugin is required for the Qt GUI suite in this environment; the Release focused run passed selection-list/toggle, grip drag/undo/Escape, overlap chooser, crossing drag/Escape restoration, and Ctrl+click sub-object/plain whole-solid selection: 7 passed, 0 failed. The box Escape test exposed selection loss on cancel; selection snapshots now restore it. The overlap chooser now uses a non-blocking Qt popup so selection remains usable and testable.

## 9. Self-verification
- [x] build-project — Qt `GoSurveyQtTests` Debug target builds successfully
- [x] architecture-review
- [x] code-review — inspected Qt event and selection changes against scope and shared API boundary
- [x] dependency-audit — no dependency or manifest changes
- [x] performance-review — reuses the shared visibility-aware picker and existing hover path; no new dependency or geometry algorithm
- [x] testing — five focused Qt selection workflows pass (7 QtTest cases including setup/teardown)

## 10. Verification result
- Submitted: —
- Verdict: APPROVE
- Findings: REQ-425 resolves the authority gap. The scoped architecture uses existing shared picking/grip services and keeps issue #900 overlays separate. The plan is approved for implementation under the user's confirmation of the feature scope.

## 11. Outcome
- Requirements satisfied: REQ-425 selection-cycle, gesture, grip, selection-list, and advanced 3D workflows implemented with cancel/undo/read-only behavior preserved
- Tests added: overlap choice, crossing drag/cancel, grip commit/undo/Escape, selection list and toggle, Ctrl+click sub-object and whole-solid selection; all focused cases passed
- Refactors: none planned
- Docs updated: `docs/qt-ui-migration.md`, accepted REQ-425 and decision ledger
- Done: 2026-10-18
