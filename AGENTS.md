# AGENTS.md

Instructions for AI coding agents working in this repository.

**Read [`CLAUDE.md`](CLAUDE.md) first.** It is the governance document for this repo (SPEC ->
VERIFICATION -> WORKSHOP, the git workflow, the Windows build tooling, and the SPEC GAP rules) and
applies to every agent, not only Claude.

## Build, test and clean output

Use the Windows MSVC/CMake/Ninja workflow in [CLAUDE.md §1](CLAUDE.md#1-environment).
From the repository root in PowerShell:

- Build both frontends and all tests/helpers: `.\build.bat`.
- ImGui: `.\build.bat imgui`, then `.\build\GoSurveyTestImGui.exe`.
- Qt: `.\build.bat qt`, then `ctest --preset ninja-release -R "^qt[.]editor$"`.
  This runs GUI tests on a hidden desktop. Set `$env:GOSURVEY_QT_ROOT` if the installed
  MSVC x64 Qt 6.8+ kit differs from `C:\Qt\6.8.3\msvc2022_64`.
- Full suite: run `.\build.bat` first, then `ctest --preset ninja-release`.
  A frontend-only build does not build every headless helper. Report failures explicitly.
- Debug: `.\build.bat imgui debug` or `.\build.bat qt debug`; run
  `.\build\debug\GoSurveyTestImGui.exe` or `ctest --preset ninja-debug -R "^qt[.]editor$"`.
  For the full Debug suite, run `.\build.bat debug`, then `ctest --preset ninja-debug`.
- Check output after building both frontends:
  `ctest --preset ninja-release -R "^build[.]output-layout$"`.

Keep only the four application executables (`GoSurvey.exe`, `GoSurveyTestImGui.exe`,
`GoSurveyQt.exe`, `GoSurveyQtTests.exe`) and required runtime files beside them in `build/`.
Debug applications use `build/debug/`. Keep intermediates/helpers under `build/.cmake/`
and screenshots/logs/reports under `build/artifacts/`. Retain runtime DLLs, `qt.conf`,
`qt-plugins/`, `resources/` and runtime `.ini` files; resource images are required assets.

Successful builds archive old clutter under `build/artifacts/legacy-output/`. To tidy without
building, close apps/tests and run
`powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\tidy-build-output.ps1 -RuntimeDirectory .\build`.
Use `.\build\debug` for Debug cleanup. Preserve archives for recovery and limit cleanup to the
current checkout. For a clean rebuild, use `.\build.bat release --clean-first`.

## Qt UI changes: show screenshots in chat

When the work adds or changes visible Qt UI (a new window, dialog, ribbon or Settings page, a layout
tweak, a restyle), running the tests is not enough. Before calling the work done:

* Capture screenshots of every affected surface and its meaningful states (for example: normal,
  busy, error, empty, disabled). Use the Qt test run on the hidden desktop
  (`tools/run-qt-tests-hidden.ps1`, used by `ctest --preset ninja-release -R qt.editor`) with
  `widget->grab()` or, for the 3D viewport, `grabFramebuffer()`. Save the PNGs under `build/artifacts/`; never commit them.
* Paste them **inline in the chat reply** as Markdown images with absolute forward-slash paths, for
  example `![Settings](C:/Users/<user>/source/repos/GoSurvey/build/artifacts/qt-shot.png)`. Do **not** publish
  them as an HTML page, and do not put them inside a question popup or other tool call: the user could
  not see images shown that way. Crop or split wide captures so text is readable at chat width.
* Label each image, say what changed, then **stop and wait** for the user to approve or ask for tweaks.
  Apply the requested changes and show new screenshots until they approve. Only then commit and open
  the PR.
