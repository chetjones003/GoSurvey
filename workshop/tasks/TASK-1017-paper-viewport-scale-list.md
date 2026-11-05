# TASK-1017 — Paper viewport scale list and match-to-scale (issue #1017)

- Type: feature
- Status: ready for issue closure with combined PR
- Opened: 2026-11-03
- GitHub issue: #1017

## 1. Authority

- Requirement: REQ-484 (accepted), plus REQ-027/031/033/201.
- Acceptance: REQ-484 Acceptance 1–4.
- Owning subsystems: Commands/Domain and Qt paper viewport properties.

## 2. Scope

- In scope: named-scale dropdown for selected paper viewport(s); a command to apply a chosen named scale to all selected paper viewports; preserving model centers and paper frame geometry; single undo/redo; model editor picker unchanged.
- Out of scope: changing scale-list management, viewport creation defaults, file formats, or the existing model editor scale picker.
- Smallest change: route the existing drawing-owned named scale list through the paper viewport properties control and add a selection-aware command helper that updates only `scaleModelPerPaperIn` within the existing undo mechanism.

## 3. Architectural boundary check

- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
  - [ ] Yes
  - [x] No. Named scales, paper viewport values, selected viewport indices, and undo history already exist. No persistence schema change is needed.

## 4. Plan

- Approach: inspect the Qt property panel's current annotative-scale selector and free-text actual viewport scale editor; add a clearly labeled named drawing scale dropdown that modifies actual `scaleModelPerPaperIn`. Add a command with explicit named-scale input that validates the scale and selection before taking one undo snapshot, applies the converted ratio to all valid selected viewports in the active layout, and leaves center/frame values untouched. Keep `VPSCALE`'s floating viewport behavior intact; use a distinct command name unless existing command naming conventions show an established match-to-scale name.
- Files/functions to inspect and likely touch:
  - `src/qt/EditorWindow.cpp` and related Qt property helpers for selected paper viewport scale controls.
  - `src/commands/CadCommands.cpp` / `.hpp` for command parsing, scale conversion, selected viewport validation, and undo.
  - `src/commands/PaperSpace.hpp` only if existing value operations require a minimal helper.
  - Existing command/property tests in `tests/PaperSpaceTests.cpp` and `tests/QtEditorTests.cpp`; headless transcript if the command routing can be covered there.
  - In-app command help and affected paper-space user documentation after implementation.
- Test approach:
  - Verify named dropdown options reflect the drawing's named scales and selecting one changes the selected viewport ratio.
  - Apply a named scale to multiple selected viewports with different center/frame values; assert only scale changes.
  - Verify unknown scale and empty/invalid selection leave drawing and undo state unchanged.
  - Verify one undo restores all prior scales and redo reapplies them.
  - Verify model editor scale picker behavior remains unchanged.
- Architectural boundary check: Qt owns the control, Commands owns validation and mutation, and the existing drawing model owns scale definitions and viewport state. No upward dependency or new abstraction.

## 5. Decisions

| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | What does match-to-scale do? | 2026-11-03 | Apply a selected drawing scale to selected paper viewport(s), preserving centers and frames. |
| Q2 | Where is scale selection exposed? | 2026-11-03 | Properties dropdown and command. |
| Q3 | Is the feature request's scope complete? | 2026-11-03 | Yes; user confirmed the requirements and acceptance criteria. |

## 6. Verification record

- 2026-11-03 — Plan review: **APPROVE**. See `verification/reviews/TASK-1017-plan-review.md`. The accepted REQ gives measurable behavior; implementation stays within existing command, Qt, drawing-scale, and undo ownership. No architecture or format change is needed.

## 7. Implementation log

- Added `VPMATCHSCALE <name>` for selected paper viewports, validating the drawing-owned named scale and the complete active-layout selection before a single undo snapshot. It refuses unknown scales, missing/invalid selections, and locked targets without mutation. Existing `VPSCALE` is unchanged.
- Added a separate **Viewport drawing scale** dropdown to the Viewports manager. The manager supports selecting multiple viewport rows; applying a named scale changes only their model-to-paper ratios. The existing annotative scale selector remains separate.
- Added regression coverage for multi-viewport scale application, unchanged centers/frames, undo/redo, unknown-name no-op, Qt dropdown behavior, and the command route. Updated the paper-space guide and ribbon help.
- Release `build.bat` passed. `[req484]` passed (22 assertions / 1 case). Hidden Qt test `viewportScaleWorkflowValidatesAndSupportsUndo` passed (3 QtTest totals, including setup/cleanup); `ctest --preset ninja-release -R "^qt[.]editor[.]shard-5$" --output-on-failure` passed (1/1 shard). `git diff --check` passed.
- Screenshot: `build/artifacts/Release/qt-1017-viewport-scale-dropdown.png`. Civil 3D is unavailable in this environment; the required paired visual comparison is **inconclusive**.

## 8. Outcome

- Requirements satisfied: REQ-484 behavior and regression coverage are implemented. No new dependency or data format was added; the model editor scale picker and `VPSCALE` behavior remain unchanged.
- Verification: plan approved before implementation; focused Release and Qt checks passed. The Civil 3D visual comparison remains inconclusive because the application is unavailable. Combined PR product-documentation preflight remains pending.
- Ready for closure with the combined paper-space PR; do not close the GitHub issue before that PR is ready.
