# TASK-1164 — Grading-group elevation optimization (GitHub #813)

- Type: feature
- Status: completed (handoff before PR; no PR created)
- Opened: 2026-11-09
- Authority: REQ-523, D-2026-11-10-b/f, REQ-073, REQ-398 and REQ-404.
- User decision: keep plan footprints fixed, adjust grading-group elevations only, and offer both zero net balance and a user-entered net-volume target.

## Requirement and acceptance

Implement accepted REQ-523: preview a deterministic shared vertical offset for a built grading-group surface against a built ground surface, apply/cancel safely, persist through DWG, and preserve undo/read-only behavior. The shared offset applies to all baked member elevations, preserving XY coordinates, relative grades, triangle topology and source grading definitions. REQ-073 supplies volume calculation and its fill-minus-cut sign convention. See the REQ for all acceptance criteria.

## Implementation plan

1. Add a pure optimization API beside the existing surface-volume computation. It evaluates candidate uniform offsets, brackets the requested fill-minus-cut value, bisects to the specified tolerance, and returns the offset, resulting cut/fill/net, error, iteration count or a precise failure. Reject malformed TINs and no-overlap pairs without mutation.
2. Add an undoable group-apply command/service that revalidates the group, target, input revisions and preview offset; translate every baked grading strip and the matching group TIN Z values by the same finite offset. Keep XY, topology, source feature lines and grading criteria unchanged. Mark dependent volume surfaces dirty.
3. Add the Qt Grading Optimization tool reachable from Home → Optimize. It lists built group and ground surfaces, provides zero-balance and entered-target modes, shows preview results and iteration count, and exposes Apply/Cancel with visible failure states. Apply uses the existing atomic command path.
4. Persist through the existing grading-group payload; add no entity kind, independent store, dependency or mutable global. Keep computation in the pure surface-volume layer, state mutation in Commands, persistence in IO, and Qt as an adapter.
5. Add deterministic solver, command/undo/read-only/DWG tests and Qt integration/state screenshots. Review every REQ-523 criterion and run the required Release build and full suite.
6. Run the requested independent council review on the complete diff using two Codex models and one Claude Haiku model. Fix the full set of findings and rerun review as needed before handoff. Capture GoSurvey screenshots; the user waived the Civil 3D comparison panel. Do not create a PR.

## Planned files / boundaries

- src/util/surfacevolume.hpp/.cpp: pure solver result and bisection over the existing ComputeSurfaceVolume engine.
- src/commands/CadCommands.hpp/.cpp: validate and apply one shared offset as one undo step; keep group surfaces and dependent volume surfaces ready/dirty correctly.
- src/io/GsIo.cpp: only if existing strip serialization does not already persist Z changes (expected to require no schema change).
- src/qt/RibbonCatalog.hpp and existing Qt editor surface/grading UI entry points: launch and implement the nonmodal optimization tool.
- tests/SurfaceVolumeTests.cpp, tests/QtEditorTests.cpp, plus headless transcript tests if the repository command workflow requires it.
- resources/wiki/Point-Groups-and-Surfaces.md: update the in-app grading-group manual if it is the appropriate user-facing page; review README, PRIVACY and public manual before any future PR.

No new public geometry abstraction without multiple uses, no per-member independent optimization variables, no footprint changes, and no parallel volume engine.

## Risks and verification

- Verify positive net means fill (fill minus cut) by testing below-ground and above-ground design surfaces.
- Confirm the offset preserves every XY coordinate and triangle index, including disconnected groups; only common coverage contributes to volume.
- Confirm finite target-side expansion, bisection tolerance, an unbracketable target, zero overlap, non-finite input and cancellation are reported without mutation.
- Confirm Apply refuses stale previews/read-only drawings and is one undo step; undo/redo cover strips, TIN and dependent volume readiness.
- Confirm DWG reopen retains optimized strips and same volume; ordinary surfaces and legacy group payloads remain unchanged.
- A representative large-TIN timing was not recorded. Preview computation runs on a cancellable worker, keeping the UI responsive.

## Plan verification

- REQ-523 and user-selected scope agree: one shared Z offset, no XY changes, both target modes, preview before apply.
- REQ-073 defines the volume oracle and sign convention; REQ-404 defines grading groups as baked ordinary surfaces and permits explicit refresh from source grading.
- The design stays within existing surface, command, IO and Qt owners; no new volume engine, entity kind or package is planned.
- The accepted requirement records the otherwise underdetermined optimization choice explicitly, so independent grading priorities and slope controls are not inferred.
- Threading follows architecture §8: the preview worker receives copied TINs, supports cooperative cancellation, and applies results only when document generation and surface revision still match.

## Completed work and verification

- Implemented Home → Optimize → Grading Optimization with both zero-net and target-net modes, asynchronous preview/cancel, stale-input checks, read-only messaging and one-step Apply/undo/redo.
- Applied a single offset to the group TIN and baked member strips; preserved XY/topology and refreshed linked labels, profiles, draped entities and dependent volume surfaces. Apply stores the exact solver volume result with geometry in the same undo snapshot; a ground using re-draped input linework stays stale until rebuilt.
- Added REQ-523 and decisions D-2026-11-10-b/f. Updated the in-app wiki with the supported workflow and the existing fresh-add/transfer/regrade behavior.
- Release `build.bat`: passed. `ctest --preset ninja-release --parallel 4`: **194/194 passed**.
- Focused REQ-523 solver tests: **25 assertions / 4 cases passed**, covering both offset directions, zero and target net, no overlap, malformed/non-finite input, unbracketable target and cancellation.
- Focused Qt verification passed for Apply/read-only, asynchronous preview/cancel, stale preview, target mode, repeat preview after Apply, undo/redo and save/reopen. The existing `gradingGroupMembershipTransferUndoAndDwgRoundTrip` test now applies an optimization and verifies the transferred baked snapshot survives actual DWG save/reopen.
- `git diff --check`: passed. The complete visual state captures are in `build/artifacts/Release/qt-813-optimization-{empty,invalid,cancelled,ready,preview,applied}.png`.
- Council round 1 used Codex `gpt-6.1-sol`, Codex `gpt-6-astra` and Claude Haiku 5.5. Haiku had an initial rate-limited request; later full Haiku reviews completed. Those reviews led to fixes for playback lifecycle, viewport rendering, ownership, and test coverage.
- A representative large-TIN timing was not measured; the UI uses a cancellable worker so preview does not block repaint/input. README, PRIVACY and the public manual wiki review/update remain part of the repo's required pre-PR documentation pass and were deferred because this is the requested pre-PR handoff.

## Final handoff update (2026-11-10)

- The REQ-523 command and DWG regression verify the optimized volume report is saved and restored with geometry. A regression also verifies ground freshness when the ground definition uses linework re-draped by the group.
- Release build passed. Final `ctest --preset ninja-release --parallel 4`: **194/194 passed**. `git diff --check`: passed.
- Full council review continues under TASK-1165. No PR was created; the user requested a pre-PR handoff.
- The volume report is now serialized per drawing and verified after opening the DWG into a fresh command state; the test uses volumes recomputed for the applied +3.0 elevation.
- Zero-offset Apply advances the save-dirty revision while retaining the freshness status of surfaces that were current before the report update.
- Grading optimization reports are excluded from reusable `.gst` templates. JSON round-trip, no-report, tab-snapshot isolation and template exclusion are covered by `[req522]` tests.
- Applying the offset retains standard dependent refresh behavior; REQ-523 records that draped dependents follow the group and dependent ground surfaces remain stale for rebuild.
