# TASK-1100 — Qt ZOOM WINDOW first corner is ignored

- Type: bug
- Status: complete; ready for review
- Opened: 2026-10-27
- Owner: Workshop

## 1. Authority
- Goal: Qt parity with the retired ImGui viewport; `spec/project.md` has no accepted numbered GOAL entry.
- Requirements: REQ-122 (accepted), including its shared `FrameWorldRect` behavior and one-world-unit minimum span; REQ-201 for a reason when nothing changes.
- Constraints: No numbered CON applies; follow `spec/architecture.md` layering and the Windows/MSVC build rules in `spec/project.md` §7.
- Acceptance: ZOOM WINDOW's first click starts the existing selection box, the second click finishes it and queues the rectangle, and the Qt viewport consumes the queued zoom. The active command remains Zoom between clicks. Regression uses the real Qt viewport input path.

## 2. Scope and approach
- In scope: `src/qt/EditorViewport.cpp`, `tests/QtEditorTests.cpp`, this task log.
- Out of scope: zoom framing math, command semantics, other frontends, UI restyling, and documentation/spec changes.
- On a left click, when the active command is Zoom and no first corner is waiting, call the existing `BeginSelectionBoxCorner` with the same world and screen coordinates used by the existing idle drag-selection path. Let the existing `SubmitViewportPick` Zoom branch handle the second click.
- Add a focused test: start ZOOM WINDOW, click the first corner and assert `selBoxWaitingSecond`; click the second corner and assert the queued world rectangle, then verify the Qt viewport consumes it.

## 3. Architectural boundary check
- UI interaction stays in `EditorViewport`; command state and zoom calculation remain in Commands. Reuses existing state and corner-start API. No new abstraction, dependency, ownership, or state.

## 4. Verification plan
- Prove the regression test fails on the unpatched branch and passes after the fix.
- Run `build.bat`, then Qt editor tests and the full CTest suite per repository rules; record build/test results.
- Self-review against `verification/skills/{architecture-review,code-review,testing}` and run applicable build-project checklist.
- No visible surface/layout restyle is introduced; the change enables an existing interaction.

## 5. Documentation review (before PR)
- `README.md`: reviewed; no update needed (interaction detail belongs in the manual, and the existing manual behavior is unchanged).
- `PRIVACY.md`: reviewed; no update needed (no data collection or handling change).
- `resources/wiki/`: reviewed relevant ZOOM WINDOW and navigation pages; they already describe two-click framing, so no update needed.
- Public manual wiki: reviewed `Views-and-Navigation.md`; it already describes two-click framing, so no update needed.

## 6. Implementation and verification log
- 2026-10-27 — Added first-corner initiation for an active Zoom `SelectionBox` route, returning before that click reaches `SubmitViewportPick`; added a two-click Qt regression that observes the first-corner state and confirms the viewport applies the queued rectangle.
- 2026-10-27 — `build.bat` Release: PASS (all 455 build steps completed).
- 2026-10-27 — Focused hidden-desktop Qt test `issue1100QtZoomWindowClicksBothCorners`: PASS (3 passed, 0 failed including init/cleanup).
- 2026-10-27 — Full Qt editor CTest shards: PASS (6/6, 575.14 seconds).
- 2026-10-27 — Full `ctest --preset ninja-release --output-on-failure`: PASS (191/191, 711.16 seconds), including build output layout, Qt deployment and parity ledger.
- 2026-10-27 — Plan review: PASS (`verification/reviews/TASK-1100-plan-review.md`).

## 7. Self-verification and outcome
- Architecture: PASS — Qt owns the interaction; existing command API/state reused; no new layering, dependencies, or state.
- Code review: PASS — first click is handled once and returns; second click follows existing `SubmitViewportPick` and pending-zoom path. Focused regression exercises both real clicks and verifies the zoom application.
- Build gate: PASS for the repository Release build and suite; no warnings observed. Clean-first rebuild and deterministic double-build were not run.
- Diff hygiene: `git diff --check` PASS. No generated artifacts are included in the source changes.
- Documentation review: README, PRIVACY, in-app wiki and public manual review outcomes recorded in §5; no updates needed.
- Requirements satisfied: REQ-122 ZOOM WINDOW uses the Qt viewport's two-click input and existing shared framing operation.
