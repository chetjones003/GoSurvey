# TASK-810 — View active layout Page Setup details

- Type: feature
- Status: implement
- Opened: 2026-11-04
- GitHub: #810 (View Details slice)
- Branch: `chore/triage-paper-space-issues` (shared paper-space batch)

## 1. Authority

- Goal: M-PaperSpace outcome; no numbered GOAL covers this Output-tab action.
- Requirements: REQ-487 (accepted), REQ-445, REQ-485.
- Constraints: REQ-200 (deterministic display of stored values), REQ-300 (dependency discipline), CON-06 (smallest sufficient scope).
- Acceptance: REQ-487 Acceptance 1–5.
- Owning subsystem: Qt UI. `PaperLayout` owns effective setup values; UI reads and formats them.

## 2. Scope

- In scope: enable Output-tab View Details for an active paper layout; show the active layout's effective output device, named/unnamed setup, paper dimensions/orientation, plot area, fit/ratio, centering, offsets, and CTB/STB selection in a compact read-only dialog. Disable it outside paper space, including floating model space. Permit read-only drawings.
- Out of scope: editing or applying settings, saved-setup browsing, output-device configuration, printing, DWF, and changes to plot behavior.
- Smallest change: route the existing ribbon item to a Qt dialog formatted from the active layout's existing fields; add a field-level test for its contents and immutability.

## 3. Architectural boundary check

- New abstraction, layer, dependency, ownership, global state, public API, data format, or algorithm: **No**. This is a read-only Qt presentation over existing state.

## 4. Questions and decisions

| # | Question | Asked | Answer |
|---|---|---|---|
| Q1 | Which #810 slice should be defined next? | 2026-11-04 | View Details: read-only active Page Setup summary. |
| Q2 | How should View Details present the summary? | 2026-11-04 | Compact read-only dialog. |
| Q3 | Is the assembled feature request approved for SPEC and Verification? | 2026-11-04 | Confirmed. |

## 5. Plan

- Approach: add a concrete dialog helper owned by `EditorWindow`; produce text from the active `PaperLayout`, using `PageSetupFromLayout` and existing paper preset names. Do not read or select other saved setups. Add the ribbon route and `RefreshRibbon` paper-space availability check.
- Files/functions:
  - `spec/requirements.md`, `spec/project.md` — accepted REQ-487 and D-2026-11-04-c.
  - `src/qt/RibbonCatalog.hpp`, `src/qt/EditorRibbon.cpp`, `src/qt/EditorWindow.hpp`, `src/qt/EditorWindow.cpp` — bind, gate, and show View Details.
  - `tests/QtEditorTests.cpp` — active-layout fields, custom dimensions, no-style/no-name, read-only access, model/floating disablement, and state/undo immutability.
  - `resources/wiki/Paper-Space-and-Layouts.md` or `resources/wiki/Plotting.md` — explain View Details once implementation establishes the exact labels.
- Test approach: happy path checks displayed values for preset and custom layouts and verifies no state/undo change; failure/edge path checks action disabled for model/floating states and the explicit unset labels for no name/no table. Verify read-only drawing access.
- Steps:
  - [x] Confirm feature request and record the accepted requirement/decision.
  - [x] Prepare implementation plan and complete Verification plan review before code.
- [x] Implement and add tests.
- [x] Build, run focused tests, capture UI evidence, and update help.

## 6. Verification review

See `verification/reviews/TASK-810-view-details-plan-review.md`.

## 7. Implementation log

- 2026-11-04: User selected this slice and compact read-only presentation. The complete feature request was confirmed before SPEC updates.
- 2026-11-04: Autodesk AutoCAD 2025 help was consulted for the related CTB/STB workflow; it distinguishes plot-style tables from output-device configuration but does not define GoSurvey's View Details behavior. Repository evidence is summarized in REQ-487. Civil 3D is unavailable here.

## 8. Outcome

### Completion report — 2026-11-04

- Requirements satisfied: REQ-487 Acceptance 1–5. Output-tab View Details reads the active layout's effective settings, is gated off outside paper space, supports read-only drawings, and leaves layout state and undo depth unchanged.
- Files changed for this slice: `src/qt/RibbonCatalog.hpp`, `src/qt/EditorRibbon.cpp`, `src/qt/EditorWindow.hpp`, `src/qt/EditorWindow.cpp`, `tests/QtEditorTests.cpp`, and `resources/wiki/Paper-Space-and-Layouts.md`. Existing unrelated paper-space batch changes are preserved.
- Tests: `issue810ViewDetailsShowsActiveSetupWithoutMutation` and `issue810ViewDetailsLabelsUnsetValuesAndIsUnavailableOutsidePaper` — **PASS**, 4 QtTest totals including init/cleanup, 0 failed. The first test covers custom paper, orientation, plot area, ratio, placement, named setup, CTB/STB path, read-only access, and immutability. The second covers preset paper, fit-to-paper, centered placement, absent setup/table labels, and disabled action in model/floating model space.
- Build: `.\build.bat` — **PASS** (Release build of both frontends and test/helper targets). The tool printed existing environment notices for absent `vswhere.exe` and optional Vulkan headers; configuration and build succeeded.
- UI evidence: screenshots and focused test log are under `build/artifacts/Release/` and `build/artifacts/qt-810-view-details.log`.
- Civil 3D comparison: **INCONCLUSIVE** because Civil 3D is unavailable on this machine. No pass is claimed for the cross-product panel.
- Self-review: UI reads `PageSetupFromLayout` at invocation and introduces no state mutation, persistence, API, dependency, or new abstraction. The action uses the existing Output ribbon item and is refreshed with the Preview item's paper-layout availability.
- Assumptions: none.
- Architectural decisions made by Workshop: none; D-2026-11-04-c is the accepted authority.
- Technical debt: none identified within this slice. #810 remains open for the combined paper-space PR as directed.
- Product docs: reviewed the in-app Paper Space and Layouts guide and added the View Details behavior. README and PRIVACY have no affected content; the public manual update is recorded in `TASK-paper-space-issue-batch-pr.md`.
