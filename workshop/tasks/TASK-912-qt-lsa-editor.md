# TASK-912 — Qt least-squares adjustment editor reading aids

- Type: feature
- Status: implemented; visual approved (direction A); PR open
- Opened: 2026-10-19
- Owner: Workshop
- GitHub: #912, child of #878

## 1. Authority
- Goal: Qt migration to 0.7.4 parity.
- Requirements: REQ-431 (new, accepted by D-2026-10-19-c), REQ-201 (honest failure), REQ-403 (save/reload), REQ-417..423 (solver, language, report), REQ-081 (Qt appearance).
- Constraints: REQ-300 (no new dependency), REQ-301 (no abstraction without two uses).
- Issue acceptance: preserve cancel, validation, failure, undo and read-only semantics; complete Qt usability; ledger update; focused coverage; 2-3 screenshot options per major surface, apply the user's choice.
- Owning subsystem: Qt presentation (`src/qt/AdjustmentWindow`, new `src/qt/AdjustmentEditor`) over `src/traverse/`.

## 2. Scope
- In scope: command colours, line-number gutter, inline error, plan preview with error ellipses, Files and Summary panel, explained refusal of an empty commit, tests, ledger.
- Out of scope: autocomplete and keystroke live-apply (ImGui only), scale/units (#911), merge/JobXML import (#913), open-traverse and command-editor workflows (#914), settings and report plotting (#915), any change to the solver, language or report.

## 3. Architectural boundary check
- [x] The new widgets only display; `AdjustmentWindow` still owns the traverse, results and files. Checking text parses a copy. No new dependency or data format.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| 1 | SPEC GAP: no requirement described the editor's colours, line numbers, inline errors and plan preview. Record REQ-431 (full scope)? | 2026-10-19 | User: "Yes, full REQ-431". |
| 2 | Which of three visual directions? | 2026-10-19 | User: "A" (navy, follows the theme; paper on Light). |

## 5. Assumptions / technical debt
- The Qt keyword list mirrors the ImGui editor's (plus ENDPOINT); the two copies can be unified when #917 extracts ImGui-only code.
- Ellipses are drawn enlarged when invisible at true size (stated on screen); added to REQ-431 clause 5.
- Saving into a read-only project's folder still writes the file (as 0.7.4); only project tracking is skipped. Recorded in REQ-431 clause 7.

## 6. Verification
- Qt Release build: PASS (`.\build.bat qt`).
- Hidden-desktop run: the 7 existing adjustment tests and all 7 new `issue912*` tests PASS. The full Qt suite was not run: it crashes in the unrelated `approvedTopographyStaysInPanelBackgrounds` screenshot test (see TASK-907).
- Screenshots: `build/artifacts/Release/qt-912-final-{dark,light}-{solved,error}.png` (not committed).
