# REVIEW VERDICT — REQ-453 / TASK-1036 — 2026-10-23

- Outcome: PASS (implementation conforms to REQ-453; user visual approval pending)
- Domains: architecture ✓  quality ✓  dependencies ✓  performance ✓
- Findings: 0 blocking, 0 advisory

## Scope and evidence

The user approved REQ-453 as written. The request is limited to identifying development windows by their worktree branch and documenting worktree-local build/launch behavior. The normal executable stays `GoSurvey.exe`; the standalone manager and build/restart watcher are explicitly deferred.

Repository inspection confirms that `run.bat` changes to its own script directory before selecting `build\GoSurvey.exe` or `build\debug\GoSurvey.exe`. `build.bat` likewise anchors itself to its script directory, and CMake places runtime output in that checkout's `build/` directory. Each worktree therefore has an independent executable path, so one worktree's running binary does not occupy another worktree's output path. The title has two existing update sites in `src/qt/EditorWindow.cpp`: constructor initialization and the refresh path around line 1975; the latter must preserve the development label.

## Architecture

The proposed value flow is explicit: `run.bat` sets a child-process environment value, `main.cpp` reads it, and passes it into `EditorWindow`. `EditorWindow` owns only the label string and composes it at existing title-update points. This adds no global mutable state, dependency, subsystem, build output, or executable naming variation. Worktree rules belong in `CLAUDE.md` §1, the authoritative environment instructions already referenced by `AGENTS.md`.

## Risk and acceptance checks

- Named branch label might be lost during document/title refresh; tests must cover a refresh after launch.
- Detached/direct launch must retain the existing normal title.
- Launcher must derive the branch after switching to its own directory and confine the environment value to its child process.
- Documentation must explicitly forbid redirecting another checkout's build output and stopping an app from another worktree.
- Visible Qt title changes require screenshots and a user review through the T3 visualizer before final completion.

## Verification plan

1. Add focused Qt coverage for labeled and unlabeled window titles and persistence through title refresh.
2. Implement launcher-to-startup-to-window value flow, leaving `build.bat`, CMake output paths, and executable names unchanged.
3. Build and run the relevant Qt test target; check output layout if the build touches output configuration (not expected here).
4. Review the diff against REQ-453, architecture §11, and coding standards; no dependency or hot-path review work is triggered.
5. Capture the affected title states, publish the required T3 visualizer, and await user approval.

## Implementation review

The implementation follows the reviewed plan. `run.bat` clears and derives `GOSURVEY_DEV_BRANCH` after changing to its own checkout; `main.cpp` passes that value into the `EditorWindow` constructor. Both constructor initialization and the existing refresh path compose the native title and custom title label consistently. The no-label path preserves `GoSurvey`. `build.bat`, CMake output paths, and executable names are unchanged. `CLAUDE.md` now requires worktree-root builds/launches and prohibits stopping another worktree's running app to unblock a build. The user requested a PR to `beta` after the visual review, authorizing submission of this increment.

Verification evidence: `.\build.bat` passed; all 6 registered `qt.editor.shard-*` tests passed; `build.output-layout` passed; `git diff --check` passed. The new tests assert the exact branch in both title surfaces and after refresh, and assert the unchanged title with no branch. Both screenshot artifacts were inspected and presented in the T3 visualizer. No architecture, dependency, performance, or correctness findings remain.
