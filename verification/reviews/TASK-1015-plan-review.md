# Verification review: TASK-1015 layout duplication plan

Date: 2026-11-03

## Scope and architecture

REQ-489 owns the observable behavior. `PaperLayout` is the single owner of its sheet setup, viewports, and paper objects. Copying its value and appending it to the existing layout collection preserves architecture and uses the existing persistence model. No new file field or dependency is proposed.

**Architecture: PASS.**

## Risks and required evidence

- Active layout indices can shift when inserting the copy before the active tab; tests must assert the active layout identity, not only the index.
- Collision validation must occur before `PushUndoSnapshot`; otherwise a failed copy would alter undo history despite leaving visible content unchanged.
- The UI and command routes must share one helper to prevent differences in copy contents and naming.
- DWG and DXF tests must include representative viewport and paper-space state, because a name-only copy test would not demonstrate full value copying.
- A tab-context-menu action is a visible Qt UI change. Capture the affected menu/task state; the paired Civil 3D panel is required before final completion. Civil 3D is unavailable in this environment, so that comparison may remain inconclusive.

## Tests

Approve coverage for generated unique names, explicit collision no-op including unchanged undo history, source/sibling equality, full state copy, current-layout preservation across index shifts, undo/redo, typed command, UI entry point, and DWG/DXF save/reopen.

## Verdict

**APPROVE.** The plan fits REQ-489 and existing subsystem ownership. No code may be written until this review is recorded; it is now recorded before Workshop implementation.
