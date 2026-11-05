# Verification: TASK-1017 paper viewport scale list and match-to-scale implementation

Date: 2026-11-03

## Requirement coverage

**PASS for REQ-484 Acceptance 1–4.** The Qt Viewports manager exposes the drawing's named annotation scale entries in a distinct viewport drawing-scale combo. The combo applies to the manager's selected viewport rows. `VPMATCHSCALE <name>` applies the named value to the active paper-layout selection. The shared command helper validates the layout, name, scale ratio, and all target indices/lock states before taking one undo snapshot. It writes only `scaleModelPerPaperIn`; model center and paper frame fields are not touched. `VPSCALE` and the model editor's status-bar scale picker are unchanged.

## Architecture and implementation review

**PASS.** UI code owns presentation and uses the existing drawing scale list. Command code owns validation, ratio conversion, active-layout targeting, mutation, and undo. No new dependency, state field, global, or persistence format was introduced. A stale index or locked target rejects the whole command before mutation. Unknown scale and no-selection paths also return before undo. The named scale lookup is exact and uses the same drawing-owned scale values the dropdown displays.

## Verification evidence

- Release build: `build.bat` passed.
- Domain regression: `[req484]` passed, 22 assertions / 1 case.
- Qt regression: `viewportScaleWorkflowValidatesAndSupportsUndo` passed in the hidden desktop runner (3 QtTest totals including setup/cleanup).
- Qt shard: `ctest --preset ninja-release -R "^qt[.]editor[.]shard-5$" --output-on-failure` passed, 1/1.
- `git diff --check` passed.
- UI capture: `build/artifacts/Release/qt-1017-viewport-scale-dropdown.png` shows the separate annotative and viewport drawing-scale controls.
- Civil 3D is not installed/available in this environment. The paired visual comparison is **inconclusive**; no comparative pass is claimed.

## Verdict

**PASS for implementation against REQ-484.** Focused build and regression checks pass. The only outstanding verification limitation is the unavailable Civil 3D visual comparison; the issue remains open until the combined PR is ready.
