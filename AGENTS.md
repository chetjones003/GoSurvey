# AGENTS.md

Instructions for AI coding agents working in this repository.

**Read [`CLAUDE.md`](CLAUDE.md) first.** It is the governance document for this repo (SPEC ->
VERIFICATION -> WORKSHOP, the git workflow, the Windows build tooling, and the SPEC GAP rules) and
applies to every agent, not only Claude.

## Before creating a pull request

Complete the [pre-PR product documentation review in `CLAUDE.md`](CLAUDE.md#pre-pr-product-documentation-review-req-453): inspect `README.md`, `PRIVACY.md`, `resources/wiki/`, and the public manual wiki linked from `gosurveycad.com`. Update affected sources before opening the PR, and record each review and its outcome in the task log or PR description. An affected external wiki update must be completed before opening the PR.

## Build, test and clean output

Use the Windows MSVC/CMake/Ninja workflow in [CLAUDE.md §1](CLAUDE.md#1-environment).
From the repository root in PowerShell:

Start every new task in its own new worktree and branch cut from the current `master` (or the
intended base); never reuse another task's worktree. After a PR merges, GitHub deletes the remote
branch automatically; run `pwsh scripts/cleanup-merged-worktrees.ps1` to remove the merged local
worktree and branch (it skips any worktree with uncommitted changes).

Build and run only from the root of your assigned worktree, using that checkout's
`./build.bat` and `./run.bat` (or their `debug` variants). Leave GoSurvey instances
started from other worktrees untouched; do not close them or interfere with their
builds or runtime files.

- Build both frontends and all tests/helpers: `.\build.bat`.
- Qt and domain tests: `.\build.bat`, then `ctest --preset ninja-release -R "^qt[.]editor$"` for GUI tests.
  This runs GUI tests on a hidden desktop. Set `$env:GOSURVEY_QT_ROOT` if the installed
  MSVC x64 Qt 6.8+ kit differs from `C:\Qt\6.8.3\msvc2022_64`.
- Full suite: run `.\build.bat` first, then `ctest --preset ninja-release`.
  A frontend-only build does not build every headless helper. Report failures explicitly.
- Debug: `.\build.bat debug`; run `ctest --preset ninja-debug -R "^qt[.]editor$"` for GUI tests.
  For the full Debug suite, run `.\build.bat debug`, then `ctest --preset ninja-debug`.
- Check output after building both frontends:
  `ctest --preset ninja-release -R "^build[.]output-layout$"`.

Keep only the two application executables (`GoSurvey.exe`, `GoSurveyQtTests.exe`) and required runtime files beside them in `build/`.
Debug applications use `build/debug/`. Keep intermediates/helpers under `build/.cmake/`
and screenshots/logs/reports under `build/artifacts/`. Retain runtime DLLs, `qt.conf`,
`qt-plugins/`, `resources/` and runtime `.ini` files; resource images are required assets.

Successful builds archive old clutter under `build/artifacts/legacy-output/`. To tidy without
building, close apps/tests and run
`powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\tidy-build-output.ps1 -RuntimeDirectory .\build`.
Use `.\build\debug` for Debug cleanup. Preserve archives for recovery and limit cleanup to the
current checkout. For a clean rebuild, use `.\build.bat release --clean-first`.

## Qt UI changes: present screenshots in T3 visualizer

When the work adds or changes visible Qt UI (a new window, dialog, ribbon or Settings page, a layout
tweak, a restyle), running the tests is not enough. Before calling the work done:

* Capture screenshots of every affected surface and its meaningful states (for example: normal,
  busy, error, empty, disabled). Use the Qt test run on the hidden desktop
  (`tools/run-qt-tests-hidden.ps1`, used by `ctest --preset ninja-release -R qt.editor`) with
  `widget->grab()` or, for the 3D viewport, `grabFramebuffer()`. Save the PNGs under `build/artifacts/`; never commit them.
* Present the screenshots in one labeled, clickable T3 visualizer. Use an HTML page as the
  container when needed, check it with `html_preview`, and publish it with `html_render`. Group
  related states in the same visualizer and make wide captures readable at chat width.
* Label each image, say what changed, then **stop and wait** for the user to approve or ask for tweaks.
  Apply the requested changes and show new screenshots until they approve. Only then commit and open
  the PR.
