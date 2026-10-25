# TASK-1082 — Qt ORBIT and PAN left-drag

## Authority

- GitHub issue #1082 reports that Qt ORBIT and PAN prompts request left-button dragging, but the Qt viewport currently only responds to middle-button pan/orbit.
- Accepted REQ-045 defines PAN left-drag as a 1:1 pan of the active model, paper, or floating view. Escape, Enter, and right-click exit; middle-drag remains unchanged.
- Accepted REQ-084(c) exposes Free Orbit as the ORBIT view command; REQ-084 owner-layer assigns ORBIT command behavior across UI and Commands. `StartOrbitCommand`'s prompt and command comment state left-drag tumble and Esc/Enter/right-click exit.
- No new spec behavior is needed: this task completes the already accepted Qt command interaction behavior.

## Plan

- Files/subsystems: `src/qt/EditorViewport.cpp`, Qt editor regression tests in `tests/QtEditorTests.cpp`, this workshop task record.
- Smallest implementation: route left-button drag deltas to the shared existing camera orbit math when ORBIT is active in model space and to the shared existing view pan transform when PAN is active. Keep ordinary left-drag selection and existing middle-button gestures untouched. Refresh the hand cursor as soon as PAN mode starts, and respect viewcube hit routing.
- Test seam: public Qt viewport mouse press/move/release events; assert ORBIT azimuth/elevation and PAN view pan state change, and assert middle-button pan/orbit continue working. User approved this seam on 2026-10-24.
- Verification: focused Qt editor test via hidden desktop, then project build and full Release CTest suite per repository workflow; record any environment/toolchain failure accurately.
- Architecture boundary: UI-layer event routing and cursor refresh only; reuse existing view-state transforms and command state, with no new domain API, dependency, or persisted data.

## Verification outcome

- PASS. `.\build.bat` built all Release targets with MSVC after the review fixes.
- PASS. Focused `panCommandLeftDragMovesView`, `orbitCommandLeftDragTumblesView`, and `frontViewPanMovesInCameraPlane` Qt tests passed through the hidden desktop runner. The ORBIT test also checks paper-space drag does not change the camera; the PAN test checks the hand cursor is active on command entry.
- PASS. Existing Qt coverage for `issue1034ManualOrbitCancelsAnimationAndTheCubeTracksCamera` passed in the six-shard editor run.
- PASS. `ctest --preset ninja-release -R "^qt[.]editor" --output-on-failure`: 6/6 shards passed after the review fixes.
- PASS. `ctest --preset ninja-release --output-on-failure`: 191/191 tests passed after the review fixes, including `build.output-layout`, `qt.deployment`, and `qt.parity-ledger`.
- PASS. `git diff --check` reported no whitespace errors.
- Test setup note: an initial GUI test version waited on OpenGL initialization and hit the hidden runner timeout. The tests were changed to send press/move/release events directly through a lightweight public `EditorViewport`, avoiding OpenGL while still checking the viewport's observable state.
- Review follow-up: cursor refresh is now called by the editor's command-state refresh so PAN displays the hand as soon as it starts. ORBIT drag explicitly remains model-space-only. Pan/orbit delta calculations are shared with the existing middle-drag branches.

## Completion report

- Qt ORBIT left drag now tumbles using the existing Shift+middle orbit math; Qt PAN left drag now pans using the existing middle-drag view transform. The viewport consumes command-mode left presses so they cannot fall through into drawing/selection. PAN shows its hand cursor, and the manual ORBIT path cancels an active view animation as the middle-drag path already does.
- Scope is limited to accepted REQ-045 and REQ-084(c) command interactions. Existing middle-button pan/orbit and command exit behavior are unchanged.
- No assumptions about the spec or architecture were needed. No technical debt identified.
- Documentation review: no product documentation was changed because this is a Qt input-parity fix; task outcome and verification are recorded here. The pre-PR product documentation review remains required before opening any PR.
