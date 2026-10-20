# TASK-966 — Remove ImGui support and dependencies

- Type: refactor / build / documentation
- Status: complete; Qt visual review approved; Release verification and audits passed
- Opened: 2026-10-19
- GitHub issue: #966

## 1. Authority

- Goal: maintain one supported desktop frontend and remove the maintenance and build cost of the retired ImGui application.
- Requirement: REQ-440 (accepted; D-2026-10-19-k).
- Constraints: REQ-200 (reproducible build), REQ-201 (no silent failures), REQ-300 (dependency discipline), CON-06 (smallest correct solution).
- Acceptance: Qt-only configure/build/test/package/CI; remove ImGui-only sources, tests, libraries and vendored dependencies; retain supported user workflows; full Release build and CTest pass; no stale operational ImGui references.

## 2. Scope

- In scope: enumerate all ImGui and frontend-only source, assets, dependencies, targets, scripts, tests, CI, packaging and documentation; preserve features still required by Qt/headless; remove dead ImGui-only functionality and vendored dependencies.
- Out of scope: changing Qt workflows or shared domain semantics except where necessary to detach them from ImGui; removing non-ImGui dependencies still used by a supported target.

## 3. Architecture check

- The accepted SPEC decision retires the ImGui frontend and defines Qt as the supported frontend. No new layer or frontend abstraction is authorized.
- Preserve frontend-independent domain services. Move needed text/layout behavior to existing Qt/shared APIs where necessary; remove unsupported ImGui-only interaction behavior.

## 4. Plan (before implementation)

- Inventory CMake targets and source lists, headers/includes, assets, third-party dependencies, runtime staging, scripts, CI, tests, docs and build-output checks. Trace each dependency to remaining consumers.
- Remove ImGui executable, its UI and tests, Test Engine, and only dependencies/assets made unused by that removal. Retain GLFW/GLEW/OpenGL or other components if any supported target still consumes them.
- Resolve shared/headless ImGui dependencies without changing their observable supported behavior; add or update focused tests for retained workflows.
- Convert build selectors, presets/CI, packaging, output layout, README and contributor/build instructions to the Qt-only workflow.
- Search repository for stale operational references; keep only historical decision/task records where needed.
- Run documented Qt build, full Release build and CTest; run output-layout check and inspect produced artifacts.

## 5. Files / subsystems expected

- `CMakeLists.txt`, `build.bat`, `build-qt.bat`, `CMakePresets.json`, CI workflows, packaging and output-layout scripts/tests.
- `src/` frontend/UI and shared text/font services; `tests/` frontend-specific tests; `third_party/` ImGui, Test Engine and audited frontend-only libraries.
- `resources/`, `README.md`, `CLAUDE.md`, `AGENTS.md`, `docs/`, `spec/`, `verification/` and `workshop/` build guidance.

## 6. Verification

- Happy path: configure/build Qt and all supported helpers; Qt tests, headless tests, full CTest and output-layout check pass.
- Failure mode: CMake must no longer require ImGui/Test Engine paths; missing Qt kit fails with the documented actionable error; no script silently attempts to build an ImGui target.
- Dependency audit: every removed third-party tree is shown to have no remaining consumer; scan sources and generated link/deployment manifests.
- No Qt visible-surface changes are intended; if implementation changes a visible Qt surface, follow the screenshot approval workflow.

## 7. Implementation log

- 2026-10-19 — Requirement and decision recorded from the user's instruction to implement #966; plan written before code.
- 2026-10-19 — Removed the legacy frontend, its source/tests, Test Engine, GLFW and ImGui vendored trees; made Qt the only configure/build/package path while keeping the executable name `GoSurvey.exe`. Replaced the shared text measurement dependency with Qt text layout, ported the block editor picker to a Qt dialog, and repaired the deleted frontend's DWG surface-delete selection cleanup.
- 2026-10-19 — Release build passed. Initial full CTest: 2661/2663 passed; the Qt editor test process crashed in `approvedTopographyStaysInPanelBackgrounds`, and one DWG attribute test failed once and passed when rerun alone. The output-layout check passed; the new block editor picker Qt test passed and captured its review image.
- 2026-10-20 — Fixed the Qt crash: switching drawing tabs replaces the Properties tree with a deferred-deleted widget, and the screenshot test retained that stale pointer. It now reacquires the tree after switching back. Fixed the DWG test's intermittent attribute failures: it read the version from the writer-side `Dwg_Data` after that object had been freed; decoding now uses the live reader-side version. Repeated the DWG test 20 times successfully. The full Qt run then exposed three independent issues: the polar settings screenshot path did not create its subdirectory, a command-bar test expected edit focus while the Zoom completer popup was active, and Escape from calibration selected Select instead of Browse. Those paths were corrected. The affected Qt tests passed together (8 QtTest cases), the Release build passed, and full CTest passed 2663/2663, including output layout.
- 2026-10-20 — Final source/documentation audit removed stale ImGui/GLFW inventory entries and references from active source comments, Qt parity notes and headless transcripts. Repository searches across source, tests, tools, build files, CI, packaging and current documentation found no remaining retired-frontend references. Dependency review confirmed OpenGL/GLEW remain because `gosurvey_qt_frontend` links `OpenGL::GL` and `glew_static`; ImGui, Test Engine and GLFW are absent from supported build and runtime targets. Architecture review found no new layer/dependency edges or ownership changes. Code review found no documented-standard or correctness blockers; one small advisory notes duplicated Qt offscreen bootstrap code in the headless driver and DWG fixture, which are separate executables.
- 2026-10-20 — Rebased the issue change on the latest `origin/beta`. That branch already assigns REQ-438/REQ-439 to issues #901/#902, so #966's accepted requirement was assigned the next available ID, REQ-440, while preserving both existing requirements. The first rebased full-suite run exposed that the #918 deployment check still expected the internal target filename `GoSurveyQt.exe`; updated the check to verify the public `GoSurvey.exe` name. Release build passed and the final full Release CTest run passed 2665/2665 (604.18 seconds), including `qt.editor`, output layout, deployment, and parity-ledger checks.

## 8. Self-verification

- [x] build-project
- [x] architecture-review
- [x] code-review
- [x] dependency-audit
- [x] testing (full Release CTest passed 2665/2665 on latest `beta`)

## 9. Verification result

- Verdict: PASS — Release build, full CTest, architecture review, code review and dependency audit passed.

## 10. Outcome

- Requirements satisfied: REQ-440 (Qt visual review approved)
- Done: ImGui/Test Engine/GLFW removed; Qt is the sole frontend and ships as `GoSurvey.exe`.
