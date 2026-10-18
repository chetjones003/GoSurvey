# TASK-895 — Port Qt block-editor workflows (GitHub issue #895)

- Type: feature
- Status: in progress
- Opened: 2026-10-18
- Owner: Codex

## 1. Authority

- GitHub issue #895 (parent tracker #878).
- Accepted **REQ-107** defines the BEDIT session and dirty-close Save / Don't Save / Cancel behavior.
- Accepted **REQ-369** defines GoSurvey-authored dynamic-block parameters, actions, visibility, and grips. The existing ImGui UI and shared BPARAM / BACTION / BVISIBILITY command routes are the 0.7.4 behavior reference.
- Existing ImGui close prompt, block-authoring palette, and Connection Modes window are the parity references for the added Qt workflows.

## 2. Scope

- Port the dirty BEDIT close prompt, including Save, Don't Save, and Cancel through BCLOSE.
- Port the block-authoring palette tabs (Parameters, Actions, Parameter Sets, Constraints), dispatching the same shared commands for entries enabled in 0.7.4. Include Block Properties Table and geometric/dimensional constraint entries in their existing disabled/unavailable state; do not invent behavior for tools that were stubs in 0.7.4.
- Port Connection Modes editing using the existing shared connection-mode data and undo behavior.
- Keep Block Properties Table, geometric constraints, and dimensional constraints visibly unavailable, matching their disabled/not-implemented 0.7.4 behavior; do not invent functionality for those stubs.
- Add focused Qt workflow coverage and update `docs/qt-074-parity-ledger.md`.
- Present 2–3 integrated visual options for each major affected surface and apply the user's selected direction before implementation is considered complete.

## 3. Architectural boundary check

- Reuse the shared BEDIT / BCLOSE / BPARAM / BACTION / BVISIBILITY commands and existing block-definition state. Connection Modes edits the existing `CadBlockDefinition` data with one undo snapshot per edit.
- No new command semantics, block data model, dependency, ownership model, or persistence format.
- Unsupported authoring stubs remain disabled, as they are in the 0.7.4 UI; enabling them would require a separate accepted requirement.

## 4. Plan

- [x] Inspect accepted REQ-107 and REQ-369, Qt ribbon routes, and the 0.7.4 ImGui workflows.
- [x] Verify plan against requirements: preserve the existing edit-session gate and shared dynamic-block commands; disabled authoring stubs remain disabled because there is no accepted behavior to implement for them.
- [x] Present integrated visual options for the close prompt, authoring palette, and Connection Modes; apply the user's selected direction (B — Guided workspace, 2026-10-18).
- [x] Implement native Qt surfaces using the existing shared block-edit commands and data.
- [x] Cover Save / Don't Save / Cancel, active palette commands and disabled entries, and Connection Modes edit/add/remove/default/undo behavior.
- [x] Update the parity ledger with routes and focused evidence.
- [ ] Complete the documented full Qt editor verification; capture all affected surfaces and meaningful states under `build/artifacts/` and show final screenshots inline for approval.

## 5. Verification

### Build and focused workflow tests

- `.\build.bat qt` — passed (MSVC 19.51, Qt 6.8.3).
- Hidden Qt workflow tests (`blockAuthoringPaletteUsesSharedCommandsAndDisabledToolsStayUnavailable`, `connectionModesDialogEditsUndoAndDirtyCloseChoicesWork`, `qt895BlockEditorWorkflowScreenshots`) — passed: 5 passed, 0 failed (includes init/cleanup).
- `git diff --check` — passed.
- Full `ctest --preset ninja-release -R "^qt[.]editor$" --output-on-failure` — 90 passed, 1 failed. Existing unrelated failure: `tabsAppearInitiallyAndTypingRoutesFromEditorChrome()` expected the tree widget to have focus at `tests/QtEditorTests.cpp:1648`. The three issue 895 tests passed in this run.

### Visual review

Screenshots for Parameters, Actions, Parameter Sets, Constraints, Connection Modes (full and table detail), invalid/empty states, and the dirty-close prompt are in `build/artifacts/Release/qt-895-*.png`. Awaiting user approval of the implemented Option B surfaces before closing this task.
