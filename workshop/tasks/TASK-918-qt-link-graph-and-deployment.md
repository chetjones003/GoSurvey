# TASK-918 — Remove ImGui from the Qt link graph and verify deployment

- Type: chore (build guard + verification), with one test-path fix
- Status: implemented; verification recorded below
- Opened: 2026-10-19
- GitHub issue: #918, child of #878

## 1. Authority

- Goal: Qt migration to beta 0.7.4 parity, authorized by issue #918 ("Ensure Qt builds and launches without ImGui runtime dependencies while keeping the existing ImGui executable available during parity work").
- Constraints: ADR-031 (c′) and TASK-917 (the shared domain library has no ImGui core); architecture §1 layering. Tracker #878: Qt stays additive, ImGui stays in production, the production default does not change.
- No `REQ-NNN` covers this: it changes no product behaviour, only how the Qt build is guarded and checked, as with TASK-917. If the user wants a requirement for "the Qt build carries no ImGui", that is a SPEC change to be recorded separately.
- Issue acceptance: preserve 0.7.4 behavior; update the parity ledger; add focused coverage. The "visible UI work" screenshot clause does not apply: no visible surface changes.

## 2. State at task start

TASK-917 already removed `imgui_core` from `gosurvey_domain`, so `GoSurveyQt` linked no ImGui library, and `src/qt/` and the domain sources include no ImGui header. What was missing:

1. The guard covered only `gosurvey_domain`'s direct links. A dependency added to a Qt target, or to something it links, could bring ImGui or GLFW back with no failure.
2. Nothing checked the *deployed* Qt set: that it holds no ImGui/GLFW file, and that it starts from a directory holding only what is deployed.
3. `GoSurveyQt.exe --smoke-test`, the only unattended "does it launch" check, was broken on `beta` (it never reached its checks, see §4).

## 3. Plan

- `gosurvey_assert_no_imgui_in_link_graph(<target>)` in `cmake/GoSurveyQt.cmake`: walks the whole link closure of `GoSurveyQt` and `GoSurveyQtTests` (`LINK_LIBRARIES` + `INTERFACE_LINK_LIBRARIES`, through aliases and `$<LINK_ONLY:>`) and fails the configure step on an `imgui*`/`glfw*` target, on `gosurvey_imgui_text`, on the ImGui executable target, or on a source file named for ImGui. The #917 check on `gosurvey_domain` stays.
- `tools/verify-qt-deployment.ps1`, registered as ctest `qt.deployment`: (1) required Qt runtime files present; (2) no ImGui/GLFW file in the deployed set; (3) `GoSurveyQt.exe` holds no ImGui/GLFW library text; (4) a copy of only the deployed set (no `GoSurvey.exe`, no tests, no other files) with `PATH` cut to the Windows directories starts and passes `--smoke-test` on the hidden desktop.
- `tools/run-qt-tests-hidden.ps1` gains `-PlainArguments` so it can start any Qt executable, not only QtTest ones.
- Fix `EditorWindow::RunSmokeChecks` (below).
- Reuse: the hidden-desktop runner, the existing `--smoke-test` checks, `build.output-layout` (unchanged).
- Architectural-boundary check: no source dependency changes; the new function has two current uses (`GoSurveyQt`, `GoSurveyQtTests`).

## 4. Smoke-test fix (found while verifying launch)

`--smoke-test` failed on `beta` with "OpenGL renderer failed to initialize". Two later changes had invalidated it: the window now opens on the Start tab (whose viewport is never shown, so the renderer never starts), and `NewDrawing()` now opens the template chooser, a modal dialog that hangs an unattended run. `RunSmokeChecks` now switches to the first drawing and waits (up to 5 s) for the renderer, and creates its second drawing with `CreateDrawingFromTemplate()`. Its checks (LINE, UNDO/REDO, document isolation, DWG round trip, PDF render, adjustment solve) are unchanged.

## 5. Verification

Results are recorded in the PR description.

## 6. Assumptions and technical debt

- The byte scan in step 3 looks for strings only the ImGui and GLFW libraries carry (`Dear ImGui`, `imgui.ini`, `ImGui::`, `The GLFW library is not initialized`, `GLFW_PLATFORM_ERROR`). The `glfw_init`/`glfw_window` stage names in the startup-failure report are REQ-425 wire names, not the library, and are deliberately not matched.
- The ImGui `GoSurvey.exe` still shares the output directory with the Qt set; the clean-copy launch is what proves the Qt set does not depend on it.
- The production default and the installer are unchanged (tracker #878).
