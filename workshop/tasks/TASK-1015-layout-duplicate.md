# TASK-1015 — Duplicate a paper layout (issue #1015)

- Type: feature
- Status: ready for issue closure with combined PR
- Opened: 2026-11-03
- GitHub issue: #1015

## 1. Authority

- Requirement: REQ-481 (accepted), plus REQ-025/031/113/201.
- Acceptance: REQ-481 Acceptance 1–5.
- Owning subsystems: Commands/Domain, Qt layout-tab menu, DWG/DXF persistence.

## 2. Scope

- In scope: a `Duplicate` action on a layout tab's right-click menu; typed `LAYOUT COPY`; complete in-drawing layout copy; generated unique names; explicit-name collision refusal; unchanged source and other layouts; unchanged current layout; undo/redo and DWG/DXF round-trip.
- Out of scope: loading layouts from templates or other drawings; import/export of a separate layout file.
- Smallest change: use the existing value-owned `PaperLayout` copy, add a single command helper with validation before undo snapshot, route both entry points to it, and maintain active-space indices when inserting the copy.

## 3. Architectural boundary check

- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
  - [ ] Yes
  - [x] No. `PaperLayout` already owns all sheet state by value, and existing DWG/DXF persistence iterates the layout collection. No new field or file representation is needed.

## 4. Plan

- Approach: add `CopyPaperLayout` beside the existing `MoveOrCopyLayout`; validate source and optional target name before mutation; create a generated `Name (N)` when no explicit name is supplied; append the copy so current layout indices stay stable through undo/redo; call `PushUndoSnapshot` only after validation. Add `COPY` to the one-shot `LAYOUT` command. Add the tab-menu action using the same helper.
- Files/functions to inspect and likely touch:
  - `src/commands/CadCommands.cpp` / `.hpp`: layout copy helper and typed command.
  - `src/qt/EditorWindow.cpp` / `.hpp`: layout-tab context menu action.
  - `tests/PaperSpaceTests.cpp` or existing command tests: helper, source/current invariants, collision, undo/redo.
  - Headless transcript/driver if existing LAYOUT command transcript seam can cover `LAYOUT COPY`.
  - `tests/LibreDwgCadTests.cpp`: DWG and DXF copy-state round-trip coverage using full layout state.
  - `tests/QtEditorTests.cpp`: context-menu Duplicate action.
- Test approach:
  - Verify copy includes sheet size/setup, viewport values/settings and paper geometry across representative stores; source and sibling layouts compare unchanged.
  - Verify active layout identity remains active when inserting before/after it; copy entry points auto-name uniquely; explicit collision leaves arrays and undo stack untouched.
  - Verify one undo removes copy, redo restores it; both DWG and DXF save/reopen retain the duplicated layout's data.
  - Verify the tab-menu action is present, enabled only for writable drawings, and invokes the same operation.
- Steps:
  - [x] Implement the validated layout-copy helper and typed command.
  - [x] Add the tab context-menu action.
  - [x] Add command, UI, undo/redo, and DWG/DXF regression coverage.
  - [x] Run focused tests/build and capture the affected UI surface.

## 5. Questions and decisions

| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Same drawing only, or import from template/another drawing too? | 2026-11-03 | Same drawing only. |
| Q2 | What state should copy include? | 2026-11-03 | Complete layout, including sheet setup, viewports/settings, and paper-space objects. |
| Q3 | Should copy activate? | 2026-11-03 | Preserve the current active layout. |
| Q4 | Are acceptance criteria complete? | 2026-11-03 | Yes: unique name; unchanged source/siblings/current active layout; undo/redo; DWG/DXF save/reopen; collision leaves state unchanged. |

## 6. Implementation log

- 2026-11-03: User confirmed the feature request and asked to proceed through SPEC and Verification.
- 2026-11-03: Full Release build passed. Qt editor shard 5 passed, including `layoutTabDuplicateCreatesCopyAndPreservesCurrentLayout`; focused menu screenshot: `build/artifacts/qt-editor/manual/shard-5/artifacts/qt-1015-layout-duplicate-context-menu.png`.
- 2026-11-03: The initial UI test timed out because its programmatic action trigger left the modal context menu open. The test now closes the menu after triggering Duplicate; rerun passed (1/1 shard).

## 7. Outcome

- Requirements satisfied: REQ-481 implementation and focused command/UI/undo/redo/DWG-DXF coverage complete.
- Verification: plan approved before implementation.
- 2026-11-03: Added UI action, command/collision/undo-redo, and DWG/DXF save/reopen coverage. The viewport-control state is included in the round-trip fixture; named page-setup names are not persisted by the existing DXF codec and are outside this feature’s acceptance requirement for the copy itself to retain representable layout state.
- Civil 3D comparison is inconclusive because Civil 3D is unavailable. Product documentation review remains for the combined PR.
- Done: ready for closure with the combined PR.

