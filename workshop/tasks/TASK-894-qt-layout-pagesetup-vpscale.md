# TASK-894 — Qt layout, page setup, and viewport scale workflows

## 1. Authority

- GitHub issue [#897](https://github.com/chetjones003/GoSurvey/issues/897), under parent tracker #878.
- Behavior baseline: beta 0.7.4 Qt migration parity, with cancel, validation, failure, undo, and read-only semantics where applicable.
- Applicable accepted requirements: REQ-025 (layout creation/rename/delete and space selection), REQ-026 (paper size/orientation), REQ-027 (viewport scale), REQ-031 (persist layout/viewports), and REQ-033/035 where the usable viewport workflow invokes creation or selection.
- The command handlers are currently documented against REQ-401 in `CadCommands.hpp/.cpp`, but REQ-401 is the unrelated Traverse Editor scale-factor requirement. Issue #897 plus REQ-025..027 are the authority for this Qt migration; do not alter the SPEC to preserve the stale code comment.
- Acceptance from issue #897: preserve beta 0.7.4 behavior, including cancel, validation, failure, undo, and read-only semantics where applicable; the Qt workflow is fully usable (a shared command route or visible ribbon button alone is not parity); update the 0.7.4 parity ledger and add focused workflow coverage.
- Visual gate: present 2–3 integrated screenshot options for each major affected surface, apply the user's selected direction, then present final screenshots and wait for approval before marking complete. The user selected Option C — Manager-first (2026-10-18).

## 2. Scope

- Inventory LAYOUT, PAGESETUP, and VPSCALE on the 0.7.4 ImGui side and current Qt side, including their dialogs, state changes, and route behavior.
- Implement complete Qt workflows within the existing UI/command/domain ownership boundaries.
- Add focused Qt workflow coverage and update `docs/qt-074-parity-ledger.md`.
- No persistence-format or domain-model changes are planned.

## 3. Boundary check and plan

- Existing paper-layout and viewport state is owned by the drawing/domain; UI dispatch and dialogs belong in `src/qt/`; command behavior stays in the existing command service. Reuse these boundaries and APIs.
- No new dependency, layer, ownership change, or abstraction is planned.
- Plan check: REQ-025/026/027 provide accepted behavior for layout operations, sheet setup, and viewport scale; REQ-031 governs persistence. Issue #897 supplies the migration-specific parity, coverage, and visual-option gates. Before implementation, audit 0.7.4 behavior and current Qt dispatch to identify exact cancel/failure/read-only cases. If a behavior needed for the complete workflow lacks a clear rule, stop and ask rather than infer.

## 4. Plan

- [x] Read repository governance, issue #897, relevant REQs, parity ledger, and adjacent Qt task records.
- [x] Inventory the 0.7.4 ImGui page setup manager, page setup editor, layout context menu and VPSCALE command; Qt had no space strip or workflow dialogs, and the Page Setup ribbon route only set an unserviced flag. Accepted REQ-025/026/027/031 govern the underlying layout, sheet, scale and persisted state.
- [x] Verify the plan against the SPEC, architecture and issue scope: reuse the existing domain commands and PageSetup data, keep Qt UI in the editor layer, and preserve command validation/cancel behavior.
- [x] Present 2–3 integrated visual options for each major surface and record the user's choice: Option C — Manager-first (2026-10-18).
- [x] Implement the Option C space strip, layout manager, Page Setup Manager/editor, Viewports manager and typed VPSCALE dialog; preserve cancel, validation, failure, undo and read-only behavior as applicable.
- [x] Update the parity ledger and record verification results and the existing full-suite focus failure.
- [x] Capture final affected Qt screenshots inline and receive user visual approval (2026-10-18).

## 5. Verification

- `.\build.bat qt` — PASS (Release Qt app and tests built; existing compiler and third-party warnings remain).
- Focused hidden-desktop workflows — PASS (4 focused workflow/ribbon tests passed; QtTest also counts init/cleanup).
- Full `ctest --preset ninja-release -R "^qt[.]editor$" --output-on-failure` — 102 passed, 2 failed. The existing editor focus assertion failed at `QtEditorTests.cpp:1792`; the ribbon inventory count was stale after the four new controls, updated from 280 to 284, and passed in the focused rerun. Full log: `build/artifacts/Release/qt-tests-26932.log`; focused rerun: `build/artifacts/Release/qt-897-final-focused.log`.
- `git diff --check` — PASS.
- Final screenshots were approved by the user on 2026-10-18.

## 6. Completion report

- Implementation, verification and visual approval are recorded above. The user authorized admin merge after CI passes and issue #897 closure.

- 2026-10-18 — implementation started: add the Option C space strip, layout manager, Page Setup Manager/editor and typed VPSCALE dialog; preserve existing command handlers and page setup data model.
