# TASK-917 — Extract remaining ImGui-only APIs from shared code

- Type: refactor
- Status: implemented; verification recorded below
- Opened: 2026-10-19
- GitHub issue: #917, child of #878

## 1. Authority

- Goal: Qt migration to beta 0.7.4 parity, authorized by issue #917 ("Remove ImGui font measurement, SHX draw-list paths, and ribbon measurement from shared/domain APIs used by Qt").
- Constraints: ADR-031 (c′) (headless measures text through the same font as the app), REQ-203 (headless link line carries no GLFW/GL), architecture §1 layering. Behavior of the ImGui build and the headless transcripts must not change.
- Issue acceptance: preserve 0.7.4 behavior; update the parity ledger; add focused coverage. The "visible UI work" screenshot clause does not apply: no visible surface changes.
- Follow-on: #918 removes ImGui from the Qt link graph and verifies deployment; this task removes the shared-library reason it could not.

## 2. Scope

- In scope: stop `gosurvey_domain` (and so `GoSurveyQt`) from compiling or linking ImGui core.
- Out of scope: removing ImGui/GLFW from the Qt target's own link line, deployment checks (#918), production default, any ImGui UI behavior.

## 3. Inventory at task start

`gosurvey_domain` linked `imgui_core` PUBLIC because of:

1. `src/ui/MtextRichFormat.cpp` — pure rich-wire helpers (`Normalize`, `FlattenToPlain`, `ColorTag`) mixed with the ImGui wrapped layout / measure.
2. `src/ui/FontRegistry.cpp`, `src/ui/LegacyApplicationFonts.cpp` — `ImFont` registry and atlas loading.
3. `src/ui/ShxDraw.cpp` — SHX strokes into an `ImDrawList`.
4. `src/ui/RibbonLayoutMeasure.cpp` — ribbon measure via `ImGui::CalcTextSize`.
5. `src/survey/SurveyPoints.cpp` — survey-label box fell back to `ImGui::GetFont()` when no measurer was installed.

## 4. Plan and architecture check

- Split `MtextRichFormat` into a pure part (stays in the domain list) and `MtextRichImGui.{hpp,cpp}` (layout, draw, `MtextRichMeasureAnnotationImGui`).
- New static library `gosurvey_imgui_text` (`GOSURVEY_IMGUI_TEXT_SOURCES`) holds items 1–4; it links `imgui_core`. The ImGui app, `gosurvey_headless` and `GoSurveyTestImGui` link it; the Debug Developer Shell compiles the same sources into `GoSurvey` for the Test Engine ABI (ADR-040).
- `SurveyPoints.cpp` loses its ImGui fallback. It uses the existing `AppCommandState::measureAnnotationText` seam (already used by Qt), with the same no-font estimate when none is installed. The ImGui app and the headless driver install `MtextRichMeasureAnnotationImGui` on their state (also after the driver's `NEW`).
- A configure-time check fails CMake if `gosurvey_domain` ever links ImGui again.
- No new dependency; the one new abstraction (the library) has four current users.

## 5. Verification

See the PR description for build and test results.

### Results (Release unless stated)

- `build.bat` (both frontends and helpers): passed. `build.bat imgui debug` (Developer Shell): passed after adding `Laszip::laszip` to the Developer Shell link line, which was missing there independent of this change.
- `GoSurveyTestImGui.exe`: 2,499 cases / 9,211,016 assertions passed. `[issue917]` (3 cases, 16 assertions) also passes in Debug.
- `ctest --preset ninja-release` excluding `qt.editor`: 2,676 of 2,677 passed (all other headless transcripts, `build.output-layout` included). `headless.regression-surfacedelete-selection` fails identically on an unmodified `origin/beta` build, so it is pre-existing.
- `qt.editor`: crashes later in `approvedTopographyStaysInPanelBackgrounds` at `QWidget::grab`, the same known failure recorded in TASK-914. `surveyDisplayMarkerTiming` and `issue914OpenTraverseSolveAndCommandErrorRecovery` pass in isolated hidden-desktop runs.
- `GoSurveyQt.exe` link line contains no ImGui library.
