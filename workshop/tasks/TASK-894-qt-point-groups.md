# TASK-894 — Port Qt point-group management dialogs

- Type: feature
- Status: approved; pull request pending CI and merge
- Opened: 2026-10-18
- Owner: Codex

## 1. Authority

- GitHub issue #894 (parent tracker #878): port the 0.7.4 group creation and management dialogs for shared group commands; preserve cancel, validation, failure, undo, and read-only behavior where applicable; make the Qt workflow fully usable; update the 0.7.4 parity ledger and add focused workflow coverage.
- Behavior authority: accepted **REQ-067** and `DrawPointGroupManagerWindow` in `src/ui/CadUi_PointGroups.cpp`; shared group command semantics from `src/commands/CadCommands.*` and `src/commands/SurveyPoints.*`.

## 2. Scope

- Provide a native Qt workflow to create, rename, delete, and edit point groups, including id ranges, description wildcard, raw-description wildcard, explicit selected ids, live resolved membership, and empty/no-match messaging.
- Preserve 0.7.4 validation, failure reporting, cancel/close, undo, and applicable drawing read-only behavior; use existing domain commands/state and avoid a second group-rule implementation.
- The 0.7.4 manager allows drawing-owned point-group edits while a project is open read-only (it does not write the project point database); Qt retains that behavior, and saving the drawing remains governed by the existing save path.
- Add focused Qt workflow coverage and update `docs/qt-074-parity-ledger.md`.
- Out of scope: changing REQ-067 semantics, group persistence, surface behavior, and non-Qt frontends.

## 3. Architectural boundary check

- Planned boundary: Qt UI over existing point-group model and shared resolution/undo services; no new abstraction, layer, dependency, data format, ownership model, or public API.

## 4. Questions

| # | Question | Asked | Answer |
|---|---|---|---|
| Q1 | Which integrated Qt visual direction should the Point Groups manager use? | 2026-10-18 | **Option B — guided sections with the live match count and member preview always visible.** |

## 5. Plan

- [x] Inspect point-group model, undo/read-only rules, command routing, and Qt workflow test seams.
- [x] Present 2–3 integrated visual options for the manager and apply the user's selection (Option B).
- [x] Implement the Qt manager and wire the ribbon route to it.
- [x] Cover create/edit/rename/delete, validation (including malformed ranges), live resolution, close, undo, and the 0.7.4 read-only-project behavior.
- [x] Update the 0.7.4 parity ledger with route and evidence.
- [x] Run the Windows Qt build and focused workflow verification; capture final meaningful Qt states under `build/artifacts/`.
- [x] Receive user approval of the final screenshots.

## 6. Verification

- `build.bat qt` — PASS.
- Hidden-desktop `pointGroupManagerCreatesAndUndoesFromEmptyState` — PASS (3 QtTest phases).
- Hidden-desktop `pointGroupManagerEditsRulesValidatesAndPreviewsMembers` — PASS (3 QtTest phases).
- `git diff --check` — PASS.
- The full Qt suite was not run; verification was limited to the two focused Point Groups workflows.
- Final screenshots: `build/artifacts/Release/qt-894-point-groups-{populated,validation,no-match,empty}.png`; approved by the user.

## 7. Completion report

Implemented and verified. The selected Option B direction is applied and the final screenshots are approved. Awaiting CI, admin merge, and issue closure.
