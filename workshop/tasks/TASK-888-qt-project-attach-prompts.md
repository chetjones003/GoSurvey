# TASK-888 — Port project drawing copy and link prompts to Qt

## Authority

- GitHub issue #888 (parent tracker #878); accepted **REQ-379** clauses 1 (attach: copy or link, size prompt) and 5 (Add PDF to project).
- The 0.7.4 `DrawProjectAttachModal` is the parity reference: Copy / Link / Cancel, 100 MB size warning, "identical copy will be used" note, failure and read-only behaviour.
- Issue-level acceptance adds a Qt-native usable workflow, focused workflow coverage and a parity-ledger update.

## Scope

- Qt Copy / Link / Cancel question for `projectAttachPrompt` (point cloud, PDF placement, Add PDF to project).
- File → Add PDF to project… (`ADDPDFTOPROJECT`) so the Add-PDF kind is reachable in Qt.
- Not in scope: Qt PDF placement dialog (#909), point-cloud ribbon Attach (#916), Refresh "New files found" question (REQ-379 clause 6; no Qt Project panel — #889).

## Files / subsystem

- `src/commands/ProjectFiles.*`: `CompleteProjectAttach` — resolves the answer and goes on with the attach that asked; now used by both frontends (ImGui `DrawProjectAttachModal` calls it instead of its own copy of that logic).
- `src/qt/EditorProjectAttach.cpp` (new), `EditorDialogs.cpp` (ServiceUiRequests), `EditorWindow.*` (menu + verb).
- Tests: `tests/QtEditorTests.cpp`, `tests/ProjectFilesFlowTests.cpp`. Evidence: `docs/qt-074-parity-ledger.md`.
- No spec, schema, dependency or architecture change.

## Plan review

- Authority: REQ-379 already specifies the behaviour; the issue asks only for a Qt port.
- Boundary: file rules stay in `io/ProjectFiles`; the glue stays in `commands/ProjectFiles`; Qt only presents the question.
- Risk: the dialog opens synchronously from `Submit` (via ServiceUiRequests), so tests must install their answer before issuing the command.

## Verification record

- Selected design B (two choice cards) from three screenshot options.
- Focused Qt workflows (3) and the shared-logic Catch2 case pass.
