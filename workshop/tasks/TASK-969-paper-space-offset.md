# TASK-969 — Paper-space OFFSET (GitHub issue #975)

- Type: feature
- Status: in progress
- Opened: 2026-10-20
- GitHub issue: #975

## 1. Authority

- Requirement: accepted REQ-039 (paper-space modify parity), REQ-103 (modify command behavior and undo), and REQ-329 increment 7 (OFFSET distance/side resolution where applicable).
- Constraints: CON-01 (Windows/MSVC build), REQ-201 (unsupported/invalid input is reported), CON-06 (smallest correct solution).
- Acceptance: OFFSET creates a copy in the active paper layout; source model and other layouts remain unchanged; supported types follow existing OFFSET semantics; unsupported types receive a named refusal; successful work is undoable and persists through `.gs`.

## 2. Scope

- In scope: paper LINE, CIRCLE, ARC, ELLIPSE, and POLYLINE offset using the existing typed-distance and side-pick behavior; through-point behavior for the existing supported line/circle/arc types; repeated picks with the same distance; explicit refusal for paper TEXT/MTEXT/BLOCK and through-mode types the model command does not support.
- Out of scope: changes to model-space OFFSET geometry, tilted UCS behavior in paper space, and new offset geometry semantics.

## 3. Architecture check

- Follow ADR-014's active-space branch and `PaperLayout` ownership. Do not route paper picks through model stores.
- Reuse `OffsetCmd`'s existing geometry and side-selection functions on a temporary one-entity state, then append only the successful result to the active layout. This keeps model OFFSET semantics authoritative without introducing a cross-space abstraction.
- Push the real document undo snapshot only after the temporary operation succeeds. Keep the active layout as the `.gs` persistence boundary.

## 4. Plan (before implementation)

- Trace OFFSET phases, paper hit testing, model commit behavior, undo snapshots, and `.gs` per-layout serialization.
- Add an active-paper branch to the viewport pick route. Pick with paper-inch tolerance and map supported `PaperRef` types to the existing OFFSET entity kinds.
- For the side/through click, stage only the picked paper entity, invoke the existing offset side/through solver, and copy the resulting entity plus inherited attributes to the active layout after one real undo snapshot.
- Keep the OFFSET distance/mode loop alive after success and unchanged on refusal. Name unsupported paper types and preserve all non-active stores.
- Extend headless geometry expectations and add transcript coverage for distance, both sides, through mode, unsupported/invalid picks, undo, round-trip, and model-space isolation.
- Build and run focused transcripts, the full Release CTest suite, and the Qt editor verification required by the repo.

## 5. Files / subsystems expected

- `src/commands/CadCommands.cpp/.hpp`: paper hit route, staged reuse of OFFSET geometry, active-layout commit and command state.
- `tests/headless/HeadlessDriver.cpp` and `tests/headless/transcripts/req039-paper-offset.txt`: paper result assertions and acceptance coverage.
- `workshop/tasks/TASK-969-paper-space-offset.md`: implementation and verification record.

## 6. Verification

- Happy path: paper line offset on either side, circle/arc/ellipse/polyline typed-distance offsets, supported through-point offsets, repeated pick, one-step undo, and `.gs` round trip.
- Isolation: paper offset changes only the active layout; model arrays and other layouts remain byte-for-byte geometrically unchanged.
- Failure path: empty-space, text/block, invalid distance, degenerate geometry, and unsupported through-mode picks report a reason without partial geometry changes or an undo entry.
- Regression: existing model OFFSET tests/transcripts and full Release CTest.

## 7. Implementation log

- 2026-10-20 — Read issue #975, accepted REQ-039/103/329, ADR-014, coding standards, and the current OFFSET pick/commit helpers. Plan verified against the issue acceptance and the active-layout ownership boundary.
- 2026-10-20 — Added active-layout paper picking and a paper-only commit path that stages one entity through the existing OFFSET solver, snapshots one undo step, and appends only the result to its source layout. The paper target remains pinned through the repeat loop and is refused if the active layout changes. Text, blocks, and through-mode ellipse/polyline picks receive explicit refusals.
- 2026-10-20 — Paper arcs and ellipses now render in the Qt layout viewport so the OFFSET results are visible. Added Qt capture coverage and a headless transcript for side/distance handling, empty-space refusal, and model-space isolation.
- 2026-10-20 — `build.bat` PASS; all 177 headless CTest cases PASS after the final paper-pick tolerance change. Focused Qt `paperSpaceRendersSheetTablesAndViewportModelContent` and `paperOffsetSupportsNativeGeometryAndThroughMode` PASS (4 passed, 0 failed); the latter covers line, circle, arc, ellipse, polyline, through-point line offset, text refusal, one-step undo, and model-space isolation.
- 2026-10-20 — Final full `qt.editor` run PASS (CTest 1/1, 341.08 seconds); this exact final run supersedes the earlier flaky failures recorded during development. `git diff --check` PASS.

## 8. Self-verification

- [x] build-project
- [x] architecture-review — active-space and `PaperLayout` ownership preserved; no cross-space geometry writes or new dependencies.
- [ ] code-review
- [x] testing — all 177 headless cases, focused #975 Qt cases, and the final full Qt editor suite passed.

## 9. Verification result

- Verdict: PASS. Full Release build, all headless cases, focused #975 Qt cases, and full Qt editor verification passed; screenshot approved.

## 10. Outcome

- Requirements satisfied: active-layout line, circle, arc, ellipse, and polyline OFFSET route; paper-only commits; named refusals; repeated picks; undo snapshot; Qt curve visibility.
- Remaining: commit and beta pull request.
