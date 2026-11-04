# TASK-1005 — ARRAY and OVERKILL in paper space (issue #1005)

- Type: feature
- Status: ready for issue closure with combined PR
- Opened: 2026-11-03
- GitHub issue: #1005

## 1. Authority

- Requirement: REQ-483, plus REQ-039 and REQ-305.
- Acceptance: REQ-483 Acceptance 1–5.
- Owning subsystem: Commands / Domain.

## 2. Scope

- In scope: rectangular and polar ARRAY over selected paper-space objects; active-layout-only OVERKILL using its existing cleanup rules; single undo for each completed ARRAY; isolation from model geometry and other layouts.
- Out of scope: changing model-space command behavior, adding associative arrays, adding cleanup rules, or cleaning viewports as OVERKILL entities.
- User decision: support both ARRAY and OVERKILL in paper space (2026-11-03).

## 3. Architectural boundary check

- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public API / data format?
  - [ ] Yes
  - [x] No. The active paper layout owns its entities; Commands already owns selection and transformation state. Extend those paths without adding a subsystem or persistence representation.

## 4. Plan

- Approach: add a paper ARRAY phase using existing rectangular/polar parameters and the selected paper-entity indices; transform copies within the active layout and commit the batch with one undo snapshot. Route OVERKILL's cleanup to the active layout's native paper stores, preserving the current model path and cleanup rules.
- Likely files: `src/commands/CadCommands.hpp/.cpp`, paper-space transform helpers, focused `tests/PaperSpaceTests.cpp`, `tests/QtEditorTests.cpp`, and headless transcript coverage if the existing paper-selection harness can drive the full command.
- Risks: using model selection indices or stores by accident; incomplete duplication/rotation of one paper entity type; cleanup tolerance accidentally derived from model extents; per-instance undo; stale selection after array commit.
- Tests: rectangular and polar arrays on selected paper geometry; one-step undo; source/model/sibling-layout invariance; active-layout cleanup and no cross-layout effects; unchanged model-space array and cleanup behavior.
- Steps:
  - [x] Record D-2026-11-03-d and accepted REQ-483.
  - [x] Complete architecture/risk/test review before implementation.
  - [x] Implement both paper-space commands.
  - [x] Add focused command, undo, and isolation coverage.
  - [x] Build and run focused tests.

## 5. Verification review

- Verdict: APPROVE.
- Architecture: responsibility remains in Commands/Domain; no dependency, layer, file-format, or public API change is needed.
- Risks: coordinate-space mix-ups; mutation of non-active layouts; array copies not matching paper COPY transforms; OVERKILL cleanup tolerance leaking model extents into sheet units.
- Required tests: both array modes, batch undo, exact source/model/sibling invariance, OVERKILL active-layout cleanup, and model-space regression coverage.

## 6. Implementation log

- 2026-11-03: User chose to support both commands. Added D-2026-11-03-d and REQ-483 before Workshop.
- 2026-11-03: Implemented paper ARRAY selection, rectangular and polar copy placement, and a single undo snapshot per completed array. Paper copies use the existing paper translate/rotate primitives; polar rotation preserves each copy's correct center placement.
- 2026-11-03: Routed paper OVERKILL through the existing cleanup rules using only the active layout's line, circle, arc, and polyline stores, with one undo snapshot on the drawing. Model geometry and sibling layouts are not passed to the cleanup operation.
- 2026-11-03: Updated ARRAY and OVERKILL in-app help and the paper-space guide.
- 2026-11-04: Updated `regression-issue02-editing-respects-space.txt` to match the approved active-paper-layout OVERKILL behavior. The earlier transcript expected a paper-space refusal and was stale; it now verifies duplicate cleanup on paper and that model geometry remains unchanged. Removed an obsolete JOIN refusal assertion because paper JOIN is covered by issue #973.
- 2026-11-03: Release build passed. Combined `[req483],[req305]` run passed (41 assertions in 4 cases).

## 7. Outcome

- Requirements satisfied: REQ-483 acceptance criteria 1–5 have focused coverage and pass.
- Tests: Release build passed; combined `[req483],[req305]` run passed (41 assertions in 4 cases). The full combined suite remains part of final PR validation.
- Documentation: updated `resources/wiki/Commands/array.md`, `resources/wiki/Commands/overkill.md`, and `resources/wiki/Paper-Space-and-Layouts.md`. Combined PR documentation review is recorded in `TASK-paper-space-issue-batch-pr.md`.
- Verification: focused plan accepted before implementation; no new dependency, data format, public API, or layer was added.
- Done: ready for closure with the combined PR.
