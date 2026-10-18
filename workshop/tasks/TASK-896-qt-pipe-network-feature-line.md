# TASK-896 — Qt Pipe Network and Feature Line ribbon workflows

## 1. Authority

- GitHub issue [#896](https://github.com/chetjones003/GoSurvey/issues/896), under parent tracker #878.
- Accepted behavior authority: REQ-426 (Qt routes and parity workflow), REQ-087 (feature-line behavior), REQ-201 (validation/failure behavior), REQ-302 (ribbon), and REQ-345 (pipe-run behavior).
- Decision: D-2026-10-18-c (2026-10-18). User selected option 1: accept a Qt port requirement for both workflows and reverse the prior Pipe Network ribbon deferral. Existing 0.7.4 command behavior is the baseline. The user selected visual option A (2026-10-18): preserve the current ribbon button style and placement.
- Issue acceptance requires preserving cancel, validation, failure, undo and read-only semantics; complete Qt workflows; focused coverage and parity ledger updates; and 2–3 integrated screenshot options per major affected surface, with the user's selected direction applied before completion.

## 2. Scope

- Implement Qt Home → Create Design → Pipe Network as a complete `PIPERUN` workflow.
- Implement both Qt Feature Line ribbon actions as complete `FEATURELINE` workflows.
- Keep unsupported placeholders disabled and preserve shared command/drawing behavior.
- Add focused Qt workflow coverage and update `docs/qt-074-parity-ledger.md`.
- Present integrated visual options for affected surfaces, apply the user's choice, then capture final states for approval.

## 3. Files and boundaries

- Expected: `src/qt/` ribbon catalog, command routing and workflow dialogs/adapters as needed; `tests/QtEditorTests.cpp`; `docs/qt-074-parity-ledger.md`; this task record.
- Reuse the existing shared command services; do not duplicate command state machines or introduce a new architectural layer.
- Plan check: REQ-426 names each action and delegates command behavior to existing REQ-087/345 services. Validation and failure semantics come from REQ-201. Work stays in Qt UI/test/documentation layers and does not alter domain, persistence, or geometry rules. No specification gap remains after D-2026-10-18-c.

## 4. Plan

- [x] Read repository governance, issue #896, relevant accepted requirements, current Qt routes, and parity ledger.
- [x] Record D-2026-10-18-c, accept REQ-426, and reverse the Qt Pipe Network deferral in REQ-345.
- [x] Verify this plan against accepted requirements, architecture boundaries, issue scope, and migration ledger.
- [x] Present 2–3 integrated visual options for the affected Home ribbon surface and record the user's selection: A — match existing ribbon buttons.
- [x] Implement complete native Qt workflows with existing command services.
- [x] Add focused tests for action routing, validation, cancel, failure preservation, completion, undo, and read-only behavior.
- [x] Update the parity ledger with routes, coverage, and known limitations.
- [x] Build the affected Qt target and capture final screenshots.
- [x] Obtain user approval of the final screenshots before marking this Qt UI task complete.

## 5. Verification

- `.\build.bat qt` — PASS. Existing compiler warnings appeared in RW5/StarNet, LibreDWG, PDF, and renderer code.
- Hidden-desktop focused workflows — PASS (3 issue-specific test functions; 5 including QtTest init/cleanup). Log: `build/artifacts/Release/qt-896-focused.log`.
- `ctest --preset ninja-release -R "^qt[.]editor$" --output-on-failure` — 92 passed, 1 failed. The failure is `tabsAppearInitiallyAndTypingRoutesFromEditorChrome()` at `QtEditorTests.cpp:1649` (`tree->hasFocus()`); it is the existing hidden-desktop focus assertion also recorded in TASK-892 and TASK-884. Log: `build/artifacts/Release/qt-tests-16024.log`.
- Screenshots: `build/artifacts/Release/qt-896-create-ground-data.png`, `qt-896-create-design.png`, `qt-896-piperun-prompt.png`, `qt-896-piperun-validation.png`, and `qt-896-featureline-elevation-error.png`.
- `git diff --check` — PASS.

## 6. Completion

- Result: PASS for the issue-specific workflows and acceptance criteria. The full Qt suite has one known, unrelated hidden-desktop focus failure documented above; it does not involve these routes.
- User approved the final ribbon/workflow screenshots on 2026-10-18.
- No changes to domain command behavior, persistence, or geometry; no new architectural layer or dependency.
- Build artifacts and screenshots remain under `build/artifacts/` and are not part of the change.
