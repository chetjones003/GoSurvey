# TASK-1165 — Show grading optimization evaluations live (GitHub #813 follow-up)

- Type: feature
- Status: implementation complete; council review handoff pending
- Opened: 2026-11-09
- Owner: Codex
- Authority: REQ-524, D-2026-11-10-d, ADR-070, REQ-523.
- User choices: fixed footprints and shared elevation-only adjustment remain per REQ-523; automatic playback only; keep the current grading visible as a ghost; use a viewport-owned transient rendering path separate from saved drawing state. Apply remains separate.

## 1. Acceptance conditions

- Solver evaluations (zero offset, bracket expansions, bisection candidates) are emitted in order with offsets and volume results, and the final successful evaluation matches the Apply result.
- The main viewport displays every evaluation in order, with the candidate grading and source grading ghost visible together. No evaluation is silently skipped.
- Playback leaves drawing state, surface revisions, and undo history unchanged. Apply alone commits the final result as one undoable action.
- Cancel, close, failure, or stale inputs clear temporary preview state and leave drawing geometry unchanged.
- Already-in-tolerance evaluation is displayed before Apply is enabled; invalid/no-overlap/unbracketable results do not leave a preview.

## 2. Scope

- In scope: stream scalar solver samples; render the candidate and source ghost via one viewport-owned transient preview; ensure UI event order and safe cleanup; add solver and Qt coverage; capture affected UI states.
- Out of scope: manual play/pause/step controls, a convergence chart, different solver math, persistent preview data, new dependencies, generalized preview framework, Civil 3D comparison screenshots (explicitly waived by the user).
- Smallest change: record each valid offset/volume evaluation, post it to the UI thread in order, update one transient preview offset and clear it on every terminal path.

## 3. Architectural boundary check

- The user accepted D-2026-11-10-d and ADR-070: one narrow renderer entry point, viewport-owned transient state, queued immutable evaluation events, and no mutation of drawing state during playback.
- No new dependency, persistent format, entity kind, or general-purpose abstraction is planned.

## 4. Plan

- Files/functions to touch: `src/util/surfacevolume.hpp/.cpp`; `src/render/ViewportRenderer.hpp/.cpp`; `src/qt/EditorViewport.hpp/.cpp`; `src/qt/EditorGradingOptimization.cpp`; `src/qt/EditorWindow.hpp`; `tests/SurfaceVolumeTests.cpp`; `tests/QtEditorTests.cpp`; relevant CMake source list if needed; task documentation.
- Tests: solver callback order and candidate values for zero-offset, expansion, and bisection paths; immediate-tolerance behavior; failure/cancellation callback behavior; Qt preview updates and final frame ordering; ghost/candidate transient cleanup on Apply/Cancel/close/failure/stale inputs; drawing/revision/undo unchanged before Apply; existing final apply/undo behavior stays intact.
- Steps:
  1. Add an optional evaluation callback to the pure solver and verify it emits the exact result values in deterministic order.
  2. Add the narrow renderer path and viewport-owned transient candidate/ghost state; keep all GL work inside Renderer.
  3. Queue immutable UI updates from the worker, automatically present in order, and clear transient state on every terminal path.
  4. Add solver and Qt regression tests, then run the prescribed Windows Release build and focused/full suites.
  5. Capture empty, active playback, terminal failure/cancel, ready-to-apply and applied viewport states under `build/artifacts/`; present screenshots in T3 visualizer.
  6. Run full code review with two Codex models and one Claude Haiku, resolve all findings, self-review diff and hand back without creating a PR.

## 5. Plan verification — before implementation

- Requirement authority: REQ-524 is accepted and covers playback order, viewport surfaces, mutation boundaries, and failure cleanup. REQ-523 remains the authority for solver correctness and Apply/undo.
- Architecture: ADR-070 assigns solver evaluation production to Surface, queued delivery to Qt, temporary state to Viewport, and drawing to Renderer. Worker does not access `AppCommandState`; temporary state is not serialized or undoable.
- Risks: queued updates arriving after widget destruction; callback order/final frame races; ghost hidden by the normal surface pass; cleanup on every exit; per-evaluation rendering cost. Use context-bound queued events, test final-frame ordering, inspect GPU depth/style behavior, and keep evaluation messages scalar rather than copying TINs.
- Verification: deterministic callback assertions; Qt state/lifecycle tests; Release build; focused Qt editor tests and full CTest; viewport captures; council review. The user waived the Civil 3D parody comparison screenshots.
- Verdict: APPROVE. No implementation begins until the recorded REQ-524 and ADR-070 are in force; both are now accepted by user decision D-2026-11-10-d.

## 6. Implementation log

- 2026-11-09: User chose automatic playback only and approved a dedicated viewport-owned transient preview path with the original grading shown as a ghost.
- Added ordered scalar evaluation events to the grading offset solver. The UI receives immutable events on the main thread and presents each candidate in sequence.
- Added viewport-owned transient candidate and source-ghost render buffers. The selected grading's ordinary scene geometry is suppressed during preview so the ghost/candidate pair stays visible. Transient rendering does not mutate saved drawing state or undo history.
- Apply is enabled only after the final evaluation has been painted. Cancel, close, failure and stale-input paths clear the preview. Apply commits the resulting grading as one undoable action.
- Added solver callback-order/final-evaluation coverage and Qt lifecycle coverage for playback, cancel/failure cleanup, unchanged drawing before Apply and Apply behavior.
- Updated `resources/wiki/Point-Groups-and-Surfaces.md` with the optimization preview workflow.

## 7. Self-verification and outcome

- `.\build.bat`: passed.
- `ctest --preset ninja-release --parallel 4`: 194/194 passed (latest full suite after final implementation and screenshot changes).
- Focused `ctest --preset ninja-release -R "^qt[.]editor[.]shard-1$"`: passed.
- `git diff --check`: passed.
- Captures are under `build/artifacts/qt-editor/manual/shard-1/artifacts/`, including `qt-813-iteration-playback.png`, `qt-813-iteration-status.png`, and `qt-813-optimization-preview.png`. The playback capture was visually inspected; both white source ghost and cyan candidate are visible.
- Screenshots were presented in the T3 HTML visualizer.
- Council round 2: Codex Sol **7/10**, Codex Astra **6/10**, Claude Haiku 5.5 **5/10**. All three identified renderer-token reuse across dialog sessions, incomplete cleanup after interrupted playback, and the MSAA ghost depth attachment. Those findings are fixed; round 3 is requested against the updated diff. Round 2 also raised Apply volume-report semantics and ground-input dependencies; both are addressed and tested.
- Pre-PR documentation review: updated local `resources/wiki/Point-Groups-and-Surfaces.md`; `README.md`, `PRIVACY.md`, and public manual wiki review remains required before PR creation per repository governance.
- No PR was created, per user request.

## 8. Round 2 remediation and final verification (2026-11-10)

- Renderer cache tokens now increment for the viewport's lifetime; clearing a session cannot make a later dialog reuse an earlier VBO.
- Every interrupted-playback terminal path clears the preview, queued evaluations and in-flight frame accounting. A Qt test switches to paper space during playback and confirms the overlay is cleared.
- MSAA resolves both color and depth before the transient pass. The source ghost is depth-tested against scene depth; the cyan candidate remains a comparison overlay through scene depth so its shape stays legible.
- Playback metrics update when each corresponding frame is painted, not when a later evaluation is queued.
- Apply stores the exact final volume result with optimized geometry in the same undo step. A DWG round-trip test checks the saved report and baked member strips. Ground freshness is retained only when the ground definition does not consume re-draped group dependents.
- Targeted command and UI tests passed; final Release suite passed **194/194**. Updated captures show both the white source ghost and cyan candidate.
- Council round 3 (two Codex models and Claude Haiku 5.5) is underway. This remains a pre-PR handoff; no PR was opened.

## 9. Round 3 remediation and closing review (2026-11-10)

- Volume reports now follow each drawing tab and serialize in the additive GoSurvey document payload used by DWG save/open. Loading an older file clears the report; a fresh-state DWG regression computes and verifies the report from the applied offset and checks baked member strips.
- Zero-offset Apply now records the report as a dirty, undoable document change while preserving the current/stale state of all surfaces. It does not trigger unrelated surface rebuilds.
- Depth writes are enabled while clearing the scene depth buffer, including 2D Wireframe, before the visual style's normal depth mask is restored.
- Drawing revision and document-generation mismatches interrupt playback immediately, cancel the worker, clear queued/in-flight evaluations and remove the temporary viewport preview. A Qt regression changes the revision mid-playback.
- Release build passed. Focused Qt tests passed (5 passed, 0 failed). Full CTest passed **194/194**, including all six Qt editor shards. `git diff --check` passed.
- Round 3 reviews: Codex Astra **6.5/10**, Codex Sol **7/10**, Claude Haiku **7/10 on reading**. The report-persistence, wireframe-depth and revision-interruption findings are fixed and covered. Claude also noted zero-offset dirty tracking must avoid staling unrelated surfaces; the implementation preserves the prior freshness state.
- Closing council round 4 (Codex Sol, Codex Astra, Claude Haiku 5.5) is reviewing the validated tree. No PR was created; handoff remains pre-PR.

## 10. Round 4 findings and follow-up verification (2026-11-10)

- Round 4 Codex Astra and Codex Sol both passed at **9/10** with no actionable findings. Claude Haiku returned a conditional pass and identified template leakage of volume reports, missing direct JSON/tab/template coverage, a duplicated read-only helper, and a requirement clarification for dependent refreshes.
- Template serialization now erases the four volume-report fields. New coverage verifies JSON save/load both with and without a report (including no-overlap results), per-tab snapshot isolation, and template exclusion. The DWG test opens into a fresh state and checks the applied report.
- Read-only checks now share `ActiveProjectDrawingReadOnly`. REQ-523 and D-2026-11-10-a state that normal dependent refresh follows the optimized group, while a ground using re-draped inputs remains stale until rebuilt. The ground test checks the draped elevations.
- Corrected the 2D Wireframe depth-clear comment. The remaining preview timeout and unconditional MSAA depth-blit performance notes are advisory; large-TIN timing is still not recorded.
- Release build passed. New focused persistence/solver checks passed (20 assertions in 1 REQ-523 case); focused Qt tests passed (5 passed, 0 failed); final full CTest passed **194/194**; `git diff --check` passed.
- Requested closing round 5 from two Codex reviewers and Claude Haiku against this updated tree. No PR was created.

## 11. Round 5 closing review status (2026-11-10)

- Round 5 Codex Sol and Codex Astra reviews both failed on the Codex usage limit (reset 10:51 AM). No Codex verdict was obtained for the round 5 tree.
- User decision: close the council with Claude Haiku 5.5 alone and record the Codex gap in the handoff. The Codex reviewers are not replaced by other models.
- Round 5 Claude Haiku 5.5 verdict: pending at the time of this entry; any blocking findings will be fixed and recorded in a follow-up section before handoff.
- Still outstanding before PR creation: README.md, PRIVACY.md and the public manual wiki review required by REQ-453. No PR has been created.
