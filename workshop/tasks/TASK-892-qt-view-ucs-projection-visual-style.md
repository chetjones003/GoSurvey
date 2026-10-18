# TASK-892 — Qt view, UCS, projection, and visual-style workflows

## 1. Authority

- GitHub issue [#892](https://github.com/chetjones003/GoSurvey/issues/892), under parent tracker #878.
- Preserve beta 0.7.4 behavior, including cancel, validation, failure, undo, and read-only semantics.
- A shared route or visible ribbon button is not a complete workflow. Update `docs/qt-074-parity-ledger.md` and add focused Qt workflow coverage.
- Before completion, show 2–3 integrated screenshot options for each major affected surface, apply the user's selected direction, then show final affected states for approval.
- Behavior authority: accepted REQ-064 (visual styles), REQ-106 (view management), REQ-154 (UCS/PLAN), REQ-309 (projection/FOV), and applicable persistence/read-only/undo requirements referenced by those workflows. No SPEC change is planned.
- Decision: the user selected **C — Workspace cards** for the View Manager (2026-10-18). A named view carries projection and FOV per REQ-309; visual style stays a live viewport selector under REQ-064 and is not added to saved-view data.

## 2. Scope

- Inventory every ImGui-only or incomplete Qt workflow in the issue's view, saved-view, UCS, projection, and visual-style areas; use the 0.7.4 ImGui behavior as the parity baseline.
- Implement native Qt workflows for the in-scope dialogs and controls, preserving existing shared command/domain behavior and all cancel, validation, failure, undo, and read-only semantics.
- Add focused Qt workflow tests and update the 0.7.4 parity ledger with routes, coverage, and remaining gaps.
- Do not add new domain behavior or alter accepted requirements.

## 3. Files and boundaries

- Expected: `src/qt/` UI and routing, `tests/QtEditorTests.cpp` and/or existing focused test targets, `docs/qt-074-parity-ledger.md`, and this task record.
- Reuse existing command/domain services. No new dependency, persistence format, or architectural layer is planned.
- Before implementation, verify each affected workflow maps to accepted requirements and that its behavior is clear from the SPEC and 0.7.4 implementation. Escalate any gap instead of guessing.
- Plan check: REQ-106 explicitly calls for the View Manager and says it also lists saved UCS frames; REQ-154 assigns named UCS restore/delete there and reserves WCS. REQ-064 and REQ-309 define the live visual-style/projection/FOV controls already present in Qt's View ribbon. The current Qt routing still marks `@viewManager` as pending and sets `showViewManagerWindow`, but `EditorDialogs.cpp` does not service that window. The UI change can stay in the existing Qt dialog/ribbon layers while all state changes continue through the existing view/UCS/command services. The parity ledger currently lists native selectors but warns that view dialogs lack native adapters. No spec gap found at planning time.

## 4. Plan

- [x] Read governance, issue #892, relevant requirements, current parity ledger, and adjacent Qt workflow patterns.
- [x] Inventory the affected ImGui dialogs and current Qt routes; map each behavior to an accepted requirement and define focused coverage.
- [x] Verify this plan against the accepted requirements, architectural boundaries, issue scope, and migration ledger.
- [x] Present 2–3 integrated visual options for the View Manager and record the user's selection: C — Workspace cards (2026-10-18).
- [x] Implement the smallest complete native Qt workflow and focused coverage.
- [x] Update the parity ledger and record verification results and any remaining gaps.
- [x] Show final Qt screenshots for the View Manager's views, coordinate systems, empty state, and the View ribbon groups inline; user approved the screenshots (2026-10-18).

## 5. Verification

- `.\build.bat qt` — PASS; GoSurveyQt and GoSurveyQtTests build. The compiler emitted existing warnings in RW5/StarNet, LibreDWG headers, PDF and renderer files.
- Focused hidden-desktop Qt tests — PASS (5 passed, including two new View Manager workflows and the existing ribbon selector workflow).
- Full `ctest --preset ninja-release -R "^qt[.]editor$" --output-on-failure` — 89 passed, 1 failed. The failure is the existing `tabsAppearInitiallyAndTypingRoutesFromEditorChrome` focus assertion at `QtEditorTests.cpp:1649`; TASK-891 records the same beta failure. Full log: `build/artifacts/Release/qt-tests-17156.log`.
- Screenshots are under `build/artifacts/Release/qt-892-*.png` and are not committed.

## 6. Completion report

Implementation, verification, and visual review are complete. The user approved the final screenshots on 2026-10-18. The only full-suite failure is the already-known Qt editor focus assertion also recorded in TASK-891; the new workflow tests and ribbon selector test pass. No additional technical debt or SPEC changes were introduced.
