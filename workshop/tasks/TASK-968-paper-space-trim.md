# TASK-968 — Paper-space TRIM (GitHub issue #974)

- Type: feature
- Status: in progress
- Opened: 2026-10-20
- GitHub issue: #974

## 1. Authority

- Goal: continue the user-authorized REQ-039 paper-space parity workstream; no GOAL-NN is assigned to this incremental requirement in the current specification.
- Requirement: REQ-039 (accepted), paper-space modify parity; REQ-056 (accepted), existing TRIM modes and TRIMSTATE behavior.
- Constraints: CON-01 as cited by #974 (Windows/MSVC build remains unchanged); REQ-201 (explicit refusal/no silent failure); CON-06 (smallest correct solution).
- Acceptance: support TRIM in the active paper layout; only mutate that layout; clearly refuse invalid/unsupported/non-intersecting picks without partial mutation; one-step undo and `.gs` persistence; headless coverage for results, isolation and refusals; preserve existing model-space behavior.

## 2. Scope

- In scope: route paper-layout viewport picks through active `PaperLayout` stores in paper-inch coordinates; supported paper cutting edges and line/polyline trim targets; smart-line and classic modes; undo, selection/pick refusal, visible hover/cutter feedback, and transcript coverage.
- Out of scope: floating model space and changes to existing model-space TRIM semantics.

## 3. Architecture check

- Follow ADR-014's active-space branching and per-layout stores. Keep command behavior in the existing command/domain path; do not add a cross-space abstraction or new dependency.
- Reuse the existing TRIM intersection/side-selection rules where possible while keeping paper mutations isolated from model arrays.
- `PaperLayout` stores are the persistence boundary; successful mutation must target only the active layout and use existing undo snapshots.

## 4. Plan (before implementation)

- Trace both TRIM modes, paper hit testing, command input routing, undo snapshots, and `.gs` per-layout serialization.
- Add an active-paper routing branch before the existing model-space picker/mutator; resolve paper entity references independently so model indices are never interpreted against paper arrays.
- Implement paper line/polyline cut-segment collection and target picking in paper inches, preserving existing refusal behavior for unsupported entity kinds and no intersections.
- Apply edits atomically to the active layout, record one undo step, and leave model/other layout stores untouched.
- Extend headless transcript commands as needed and assert classic/smart success, isolation, undo/persistence, and invalid/no-op behavior.
- Build Qt, run focused transcripts, capture the changed viewport states, and run the documented Release CTest suite.

## 5. Files / subsystems expected

- `src/commands/CadCommands.cpp/.hpp`: paper TRIM pick routing, geometry helpers, undo.
- `tests/headless/HeadlessDriver.cpp` and transcript fixtures: paper TRIM acceptance coverage.
- `src/qt/EditorViewport.cpp` and `tests/QtEditorTests.cpp`: paper TRIM hover/cutter feedback and capture coverage.
- `workshop/tasks/TASK-968-paper-space-trim.md`: implementation and verification record.

## 6. Verification

- Happy path: paper cutting edges trim paper lines/polyline segments in both TRIM modes; active layout only; undo restores geometry; `.gs` round-trip persists result.
- Failure path: unsupported pick, no object, no crossing cut, and degenerate input produce explicit refusals with geometry unchanged.
- Regression: model-space TRIM transcripts and full Release CTest remain green.
- Paper TRIM hover and staged cutter feedback are visible viewport states; capture and review them before completion.

## 7. Implementation log

- 2026-10-20 — Read the accepted REQ-039/REQ-056, ADR-014, #974 acceptance, and existing model TRIM code. Confirmed paper objects have separate line/polyline stores and current TRIM directly addresses model arrays. Verified Qt-only branch context after ImGui removal.
- 2026-10-20 — Added a paper-space TRIM path that stages only active-layout geometry through the existing intersection solver, keeps paper cutters in `PaperEntityRef` storage, commits line/polyline edits back to that layout, and records undo only after a successful edit. Routed Qt viewport TRIM clicks through `SubmitTrimViewportPick`; model space remains on its existing solver.
- 2026-10-20 — Connected paper TRIM hover to `PickPaperEntityAt` and rendered cyan prehighlight for the hovered paper entity and staged cutters, including circles, arcs, ellipses, and polylines. Added Qt framebuffer captures for staged cutters and hovered targets. Added paper polyline target coverage to the REQ-039 transcript.

## 8. Self-verification

- [x] build-project — `.\build.bat` passed after implementation and headless oracle changes.
- [x] architecture-review — implementation stays within active-layout stores and existing command/persistence boundaries; initial review found no blocking architecture issue.
- [x] code-review — initial standards/spec review found no blocking standards issue; resolved the paper hover/cutter highlight gap identified against REQ-056.
- [x] testing — focused `headless.req039-paper-trim` passed; focused `paperTrimHighlightScreenshots` passed (3 Qt checks); `git diff --check` passed.

## 9. Verification result

- Verdict: partial pending visual approval. The full `qt.editor` test run reached 192 passes then failed at the unrelated `failureReportWindowSendsWithoutClosingAndCloseSendsNothing` clipboard/browser expectation (actual: neither clipboard nor browser available). Earlier full Release CTest passed before the viewport-highlight addition. The latest focused paper TRIM transcript and Qt screenshot test pass.

## 10. Outcome

- Requirements satisfied: paper layout isolation, classic/smart line modes, line/polyline targets, explicit refusal cases, undo, `.gs` round trip, and paper-space hover/cutter feedback are implemented. Final completion and PR remain pending the required screenshot approval.
