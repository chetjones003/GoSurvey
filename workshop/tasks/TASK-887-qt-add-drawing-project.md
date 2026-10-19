# TASK-887 — Port Add Drawing to Project to Qt

- Type: feature
- Status: complete
- Opened: 2026-10-18
- Owner: Codex

## 1. Authority

- GitHub issue #887 (parent tracker #878); accepted **REQ-378**, with project rules from REQ-373–377 and the Qt parity constraint from #878.
- The existing ImGui `DrawAddDrawingModal` and shared `PrepareAddDrawing` / `ChooseAddConvert` / `CommitAddDrawing` flow are the 0.7.4 behavior reference.
- Issue acceptance requires a usable native Qt workflow, preservation of cancel/validation/failure/undo/read-only behavior where applicable, focused workflow coverage, and an updated parity ledger. It also requires 2–3 integrated screenshot options for each major affected surface and the user's selected direction before completion.

## 2. Scope

- Port drawing selection, point preview, individual conflict choices, unit/coordinate mismatch conversion, settings overrides, and copy-in to Qt.
- Preserve the existing original file and project on cancel or failed commit; honor project read-only state and the shared domain services.
- Add focused Qt workflow coverage and update the 0.7.4 parity ledger.
- Out of scope: changing REQ-378 conversion rules, project formats, domain behavior, or non-Qt frontends.

## 3. Architectural boundary check

- Does this need a new abstraction, layer, dependency, ownership change, public API, data format, or algorithm not specified by REQ-378?
  - [x] No — proceed within Qt UI and existing shared command/domain/IO services.
  - [ ] Yes → stop and resolve a SPEC gap.

## 4. Questions

| # | Question | Asked (date) | Answer |
|---|---|---|---|
| Q1 | Which integrated Qt visual direction should the Add Drawing workflow use? | 2026-10-18 | Option C — guided Drawing / Align / Points / Confirm steps. |

## 5. Plan

- First inspect the shared flow, current Qt project routes, and relevant Qt test seams.
- [x] Present integrated screenshot options; apply the user's selected direction (C).
- [x] Implement with existing `PrepareAddDrawing`, `ChooseAddConvert`, and `CommitAddDrawing` services; keep UI state staged until Add is confirmed.
- [x] Cover summary counts/conflict choices, conversion and blockers, success/copy-in, cancel/failure preservation, and project read-only behavior in focused Qt workflows.
- [x] Update `docs/qt-074-parity-ledger.md` with the native route and evidence.
- Build and run focused Qt verification using the documented Windows workflow, then capture final meaningful UI states inline for user approval.

## 6. Verification

- `build.bat qt` — PASS.
- Focused hidden-desktop `addDrawingGuidedWorkflowConvertsReviewsConflictsAndCopiesIntoProject` — PASS (3 QtTest phases).
- Full `ctest --preset ninja-release -R "^qt[.]editor$" --output-on-failure` — 69 passed, 1 failed. The unrelated existing `tabsAppearInitiallyAndTypingRoutesFromEditorChrome` fails because `tree->hasFocus()` is false at `tests/QtEditorTests.cpp:1476`.
- `git diff --check` — PASS.
- Review against REQ-378 and architecture: behavior stays in the existing Qt UI boundary and calls the shared plan / convert / commit services; no data format or domain algorithm changed.
- Screenshot states captured in `build/artifacts/Release/qt-887-add-drawing-{align,aligned,points,confirm,cancel,write-error,database-save-error,read-only}.png`.

## 7. Completion report

Implementation and verification are recorded above. The user approved the final screenshot set. The guided workflow implementation is complete; the full Qt suite still reports the unrelated focus assertion failure recorded above.
