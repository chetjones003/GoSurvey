# TASK-914 — Qt traverse adjustment and command-editor workflows

- Type: feature
- Status: implementation approved; PR verification in progress
- Opened: 2026-10-19
- GitHub: #914, child of #878

## 1. Authority

- Goal: Qt migration to beta 0.7.4 parity, authorized by issue #914.
- Behavior requirements: accepted REQ-010, REQ-013–018, REQ-201, REQ-403, REQ-417–423, and REQ-431 as applicable to the existing Traverse Editor and adjustment window.
- Issue acceptance: preserve 0.7.4 cancel, validation, failure, undo, and read-only behavior; deliver complete Qt workflows; add focused workflow coverage; update the 0.7.4 parity ledger; present 2–3 integrated screenshot options per major affected surface and apply the user's selection.
- Cancellation decision: user selected cooperative stop (2026-10-19). A Stop action requests solver cancellation at safe checkpoints; partial results are discarded and the traverse inputs remain available for correction/retry.
- Visual direction: user selected **A — Side-by-side workbench** (2026-10-19): persistent command editor beside result tabs.
- Owning subsystem: Qt presentation over existing traverse commands, parsers, importers, solver, IO, and drawing-commit/undo services.

## 2. Scope

- In scope: Qt open-traverse adjustment using a known ENDPOINT, editing/applying command text, cooperative solve cancellation and recoverable validation/failure states, safe accept/commit behavior, focused Qt coverage, parity ledger and migration inventory updates, and final captures.
- Adjacent scope: #912 owns the command colours, line numbers, inline parse errors, plan preview, Files and Summary panel; this branch adds fuzzy typo suggestions to the existing inline diagnostics at the user's request. Merge import and JobXML changes remain with #913; settings/report plotting remain with #915. Changes to command syntax, input formats, `.gstrav` persistence, and non-Qt workflows remain out of scope.

## 3. Architectural boundary check

- Reuse existing `TraverseCommandLang`, traverse importers, `TraverseIo`, least-squares solver, report builder, and `AcceptTraverseAdjustment` / `CommitTraverseToDrawing` paths. Qt should own interaction and presentation only.
- A cancellable solve uses the cooperative `std::atomic<bool>` cancellation rule in architecture §8. The solver checks safe checkpoints, returns no usable partial result when stopped, and the UI keeps the current inputs available for retry.
- No new dependency, data format, or one-use abstraction is planned.

## 4. Plan

- [x] Read repository governance, issue #914, related traverse requirements, Qt adjustment window, and parity ledger.
- [x] Inventory the shipped ImGui workflow against the current Qt window, including cancellation, validation, failure recovery, read-only, accept, commit, and undo states.
- [x] Verify the plan against accepted requirements, Qt architecture boundaries, and issue scope; user resolved solve cancellation as cooperative stop with partial-result discard.
- [x] Present three integrated visual options for the adjustment workbench; user selected A, side-by-side editor and result tabs.
- [x] Implement Qt open-traverse solve, cooperative cancellation, command-change invalidation, recoverable solve failure, and existing accept/commit routes using domain services.
- [x] Add focused Qt workflow coverage for successful ENDPOINT solve, cancellation, validation/failure recovery, fuzzy command suggestion, and existing accept behavior; domain unit coverage asserts cancellation returns no partial result.
- [x] Update the 0.7.4 parity ledger and Qt migration inventory with routes, coverage, and remaining gaps.
- [x] User approved the final captures and authorized a PR to `beta` (2026-10-19).

## 5. Initial inventory

- At task start, `AdjustmentWindow` provided command text, import/save, asynchronous solve, reports, settings, accept, and commit. Its active-drawing guard prevented committing a result to a different drawing.
- At task start, `EditorDialogs` routed Traverse Editor and closure-window requests to `ShowAdjustment`, but the Refresh path reported that a Traverse modal required an explicit Qt workflow rather than opening the adjustment window. This remains an adjacent routing question unless the user-facing route reaches the new window.
- At task start, the worker future was waited to completion when the window was destroyed, with no user-facing in-flight solve cancellation. #914 now adds cooperative Stop and recoverable failure states.
- The parity ledger described adjustment as partial: no full form/raw-set editor, complete project settings, plotting, cancellation, merge-import, or command-editor parity. #914 closes the cancellation gap; adjacent #912, #913, and #915 work remains tracked separately.

## 6. Verification result

- Planning verified against REQ-017, REQ-201, REQ-419, REQ-420 and architecture §8. The open-chain acceptance case is explicitly defined by REQ-017: a known ENDPOINT supplies redundancy. Qt reuses the existing solver, parser, result acceptance, commit and undo services; cancellation follows the documented atomic cooperative-worker model. #912 provides the editor reading aids, while #913 and #915 retain their adjacent importer and settings/report scopes.
- Release builds: `build.bat qt` passed after syncing to latest `origin/beta`; `build.bat imgui` passed before the beta sync (the sync added Qt editor changes only).
- Domain test `Least-squares cancellation returns no partial solution`: passed (1 case, 7 assertions).
- Focused Qt tests `issue914OpenTraverseSolveAndCommandErrorRecovery` and `issue914StopDiscardsSolveAndPreservesInputs`: each passed (3 cases including Qt initialization/cleanup, 0 failed); screenshots captured below `build/artifacts/Release/qt-914-*.png`.
- The post-sync editor screenshot `qt-914-command-highlighting-fuzzy-match.png` shows #912's gutter and inline error plus the #914 `STATION` fuzzy suggestion. `issue914OpenTraverseSolveAndCommandErrorRecovery`, `issue914StopDiscardsSolveAndPreservesInputs`, and the #912 `issue912HighlighterColoursEachTokenKindAndKeepsText` regression test pass in isolated hidden-desktop runs.
- Full prescribed `ctest --preset ninja-release -R "^qt[.]editor$"` fails later in existing `approvedTopographyStaysInPanelBackgrounds` at `QWidget::grab` (access violation); focused #914/#912 tests pass separately before this crash.
- `git diff --check`: passed.
- User selected A (side-by-side workbench) and cooperative Stop, then approved the final screenshots and authorized a PR to `beta` (2026-10-19). Latest base synchronized from `origin/beta` at #954 / #912.
