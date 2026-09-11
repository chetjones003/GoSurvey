# `dev/` — WSL/Linux → Windows developer interface

GoSurvey is a **Windows-native application**. The build is MSVC (`cl`) + CMake +
the Ninja generator, driven by `build.bat` / `CMakePresets.json`, and it stays
that way. Windows is the target platform and the authoritative build
environment.

An AI coding agent, however, may run inside **WSL / Linux / Git Bash**. The
`dev/` scripts are a thin adapter so that agent can drive the *existing* Windows
tooling with predictable commands, without re-implementing any of it.

```
Linux/WSL agent  →  dev/*  →  build.bat / ctest / gh.exe  →  MSVC · CMake · Ninja · GitHub
```

Nothing here is a second build system. `dev/build` is ~3 lines that call
`build.bat`; `dev/test` adds `ctest`. If a command's behaviour ever disagrees
with running the Windows tool directly, the Windows tool wins — fix the wrapper.

## Commands

| Command | Bridges into Windows? | What it does |
|---|---|---|
| `./dev/help` | no | List these commands |
| `./dev/status` | no — native `git` | Branch, upstream delta, working tree, last commit, build-tree state |
| `./dev/build [release\|debug] [args]` | yes — `cmd.exe` → `build.bat` | Canonical build. Optional first arg picks the config (default `release` → `build/`; `debug` → `build/debug/`). Remaining args go to `cmake --build` (e.g. `--target GoSurveyTests`, `--clean-first`, `-- -v`). `debug` needs `third_party/imgui_test_engine/` vendored — see `build.bat`'s header |
| `./dev/run [release\|debug] [args]` | yes — `cmd.exe` → `run.bat` | Launches the built `GoSurvey.exe`. `target` is `release` / `debug` (default: whichever is present, newer one if both). Does not build. Starts a GUI process — no window from a headless shell |
| `./dev/clean [--all]` | no | `rm -rf build/`. `--all` also removes `build-debug/ build-release/ out/`. `build.bat` re-configures on the next build |
| `./dev/test [args]` | yes — `build.bat` + `ctest` | Builds the test targets, then `ctest --test-dir build --output-on-failure`. Args forwarded to `ctest` (e.g. `-R headless`, `--rerun-failed`). Non-zero exit if the build **or** any test fails |
| `./dev/gh <args>` | yes — Windows `gh.exe` | Pass-through to the user's existing Windows GitHub CLI. Args, stdout, stderr and exit code preserved. Auth comes from the Windows `gh` |
| `./dev/issue [...]` | yes — `gh.exe issue` | `gh issue <...>`; bare `./dev/issue` = `gh issue list` |
| `./dev/pr [...]` | yes — `gh.exe pr` | `gh pr <...>`; bare `./dev/pr` = `gh pr list` |
| `./dev/win <cmd>` | yes — `cmd.exe` | Escape hatch: run an arbitrary Windows command from the Windows repo root. `--ps <cmd>` runs it through PowerShell instead. Does **not** load the MSVC environment — use `./dev/build` for compiler work |

### Canonical commands being wrapped

```
build : cmake --preset ninja-release        (first run only; configures build/)
        cmake --build build                 (every run)
        — both via build.bat, which sources vcvars64 first
test  : ctest --test-dir build --output-on-failure
clean : rm -rf build/
```

These mirror `.github/workflows/release.yml` (the CI build/test steps).

## How the Windows bridge works

* **Environment detection** (`dev/_common.sh`): `msys` (Git Bash), `wsl` (Linux
  kernel with Windows interop), or `linux` (no Windows access — Windows-bound
  commands fail with a clear message).
* **Path conversion**: `cygpath -w` under MSYS, `wslpath -w` under WSL.
* **`cmd.exe` invocation**: the command is written to a temporary `.cmd` file
  and run as `cmd /d /c <file>`. Passing it as `/c "…string…"` is not safe —
  the MSYS runtime rewrites embedded `"` to `\"` before the child sees it,
  which corrupts any quoted Windows path. File contents are untouched.
* **`gh.exe` discovery**: `PATH` first, then
  `C:\Program Files\GitHub CLI\gh.exe` and the x86 location. No Linux `gh` is
  ever installed or substituted.
* **Exit codes**: every wrapper forwards the underlying tool's exit code
  unchanged. `dev/gh` / `dev/issue` / `dev/pr` `exec` into `gh.exe`.
* **Output**: nothing is redirected or suppressed — compiler and test output
  reach the agent verbatim on stdout/stderr.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `ERROR: cmd.exe was not found` | Running under plain Linux with no Windows interop. Use WSL with interop enabled, or Git Bash on Windows. |
| `ERROR: gh.exe … was not found` | Install the **Windows** GitHub CLI: `winget install --id GitHub.cli`. Do not install a Linux `gh`. |
| `dev/build` fails first run with a vcvars / vswhere error | `build.bat` could not locate the MSVC toolchain. Install the *Desktop development with C++* workload (MSVC v143 + Windows SDK). Incremental builds afterwards do not need vcvars — CMake caches the compiler path. |
| `dev/build` reports a bogus `cannot open source file <vector>` | The MSVC environment was not loaded. `build.bat` handles this; if you bypassed it, run `dev/build`, not a bare `cmake --build`. |
| `dev/test` exits non-zero but all tests passed | The build step failed — scroll up. `dev/test` fails if the build OR the suite fails. |
| A wrapper hides a problem you suspect is real | Reproduce with the Windows tool directly: `./dev/win ctest --test-dir build -N`, or run `build.bat` from a Windows terminal. |
