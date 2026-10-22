# TASK-1038 — Redesign the Qt least-squares adjustment workspace

- Type: feature
- Status: implementation in progress; awaiting final verification and screenshot approval
- Opened: 2026-10-22
- Owner: Workshop
- GitHub: #1038

## 1. Authority
- Requirements: REQ-431 (D-2026-10-22-c through D-2026-10-22-i), REQ-081, REQ-201, REQ-417..423.
- Acceptance:
  - Session, Editor, and Results remain visible in resizable panes. Session contains files, summary, and plan preview. Settings open from File in a separate window. Results has report text, adjusted coordinates, and residuals; the plan is not duplicated beside the report.
  - For an open traverse without ENDPOINT, known CHECK coordinates act as fixed controls; their angle and distance observations contribute to adjustment and residuals.
  - Valid editor text automatically solves after a 600 ms pause and refreshes the report and result tables. Malformed edits show the parse error and stale tables are cleared. Edits during a running solve cancel stale work and apply the latest valid text.
  - Import, validation, Run/cancel, review, accept, commit, save/export, and settings remain available.
- Owning subsystem: `src/qt/AdjustmentWindow` and `src/traverse/TraverseLeastSquares.cpp`.

## 2. Scope
- In scope: three-pane workspace, File → Settings window, report-only Results tab, CHECK controls for open-traverse LSA, live debounced adjustment, residual family tables, graphite editor/preview surfaces, all-parser-record insertion dialogs, regressions and screenshot evidence.
- Out of scope: changes to command language semantics, solver math, persistence, and adjustment workflows beyond REQ-431.

## 3. Architectural boundary check
- [x] No new dependency or general abstraction. Existing Qt window and traverse least-squares service own the behavior.

## 4. User decisions
| # | Question | Asked | Answer |
|---|---|---|---|
| 1 | Approve proposed visual criteria for #1038? | 2026-10-22 | Yes. |
| 2 | Test public Qt widget tree and capture screenshots? | 2026-10-22 | Yes. |
| 3 | How should open traverses with CHECK coordinates and no ENDPOINT adjust? | 2026-10-22 | Use CHECK coordinates as adjustment controls. |
| 4 | Should command edits require pressing Run? | 2026-10-22 | No; the solver should be live. |

## 5. Plan and evidence
- [x] Add failing widget-tree test for layout and Settings access.
- [x] Move Settings to a separate File-menu window; remove the duplicate report plot.
- [x] Add failing domain regression for known CHECK-controlled open traverse; implement fixed target coordinates and weighted angle/distance observations and residuals.
- [x] Add Qt regression using the supplied five-leg command shape.
- [x] Add widget-tree regression proving network editor commands solve and refresh report/tables without pressing Run; malformed text replaces stale tables with the parse error.
- [x] Start a 600 ms live-solve debounce after valid editor changes; cancel stale work and solve the latest text.
- [x] Rebuild and run focused and full Release suites after the live-adjustment change.
- [x] Capture and present live-report/error screenshots in the T3 visualizer; wait for user approval before commit/PR.

## 6. Implementation log
- 2026-10-22: User approved visual criteria; recorded D-2026-10-22-c and extended REQ-431.
- 2026-10-22: Moved Settings from Session to File → Settings in a separate dialog (D-2026-10-22-d); moved plot to Session Preview so Results is report-only (D-2026-10-22-e).
- 2026-10-22: User selected known CHECK coordinates as fixed open-traverse controls (D-2026-10-22-f). Added domain regression, weighted angle/distance equations, residual rows, and Qt report/table coverage.
- 2026-10-22: User clarified the solver is live and should not require Run. The widget-tree test failed before the change; the 600 ms debounced solve, automatic network report generation, changed-input recomputation, and parse-error state pass in the focused Qt run. Recorded D-2026-10-22-g.

## 7. Verification
- Prior Release build and CHECK-control domain regression passed before live-adjustment changes.
- Latest build passed; live network editor test plus workspace/report tests passed (5 QtTest functions).
- Full Release suite passed: 191/191, including all six Qt editor shards, `build.output-layout`, and `qt.deployment`.
- `git diff --check` passed.
- Live report, parse-error, File-menu Settings, Settings window, and workspace screenshots were captured and presented in a T3 visualizer.
- The updated `build/GoSurvey.exe` was started for the user (PID 41064; main-window handle `0x77d014`).

## 8. Outcome
Live adjustment is implemented and verification is green. Screenshot review is presented; wait for user approval or requested tweaks before commit or PR.

## 9. Approved extension — residual groups, theme surfaces, Records

### Specification
- The user approved D-2026-10-22-i on 2026-10-22. REQ-431 clauses 11–13 and their acceptance criteria define this increment.
- Residual mapping: Angle/Direction/Zenith → Angle; Distance → Distance; Vertical/GPS-up/Coordinate-up → Vertical; GPS east/north → GPS; coordinate east/north → Coordinate. Omit empty family tables.
- Records covers all 23 parser entry forms/options: `IMPORT` marker, `STATION`, `BEARING`, `ENDPOINT`, `LOOP`, `SCALE`, `ERRORS`, `STN`, `HANG`, `HDIST`, `SDIST`, `VANG`, `DESC`, `PRISM`, `POLE`, `TSERR`, `CTRL`, `ANGLE`, `DIST`, `AZIMUTH`, `EDIF`, `ZENITH`, and `DIR`. `SIDE` and `CHECK` are `STN` modifiers, not standalone directives.

### Verification review
- **Architecture — APPROVE.** Residual grouping and Records dialogs belong to Qt presentation in `AdjustmentWindow`. The existing `TraverseCommandLang::Parse` remains the grammar and validation authority; no domain grammar, persisted format, solver API, dependency, global state, or ownership change is required. The preview background reads the active Qt palette. The menu/dialog implementation stays local to the existing Qt workspace; no shared abstraction is needed.
- **Risks:** (1) Residual-kind classification could silently omit kinds; use exhaustive switch coverage and direct widget-tree assertions. (2) Empty-group visibility and stale rows after parse failure/live changes could regress; assert populated and empty family states. (3) Dialogs can emit malformed syntax, lose precision, or alter adjacent text; assert representative formats, parser acceptance, caret insertion, selection replacement, invalid-value refusal, and Cancel preservation across representative directive families and menu coverage for every supported record. (4) Palette colors can become unreadable or drift from the active theme; assert surface color against the active palette and capture Dark/Light screenshots.
- **Required Qt verification:** add failing widget-tree tests for every residual kind, correct group assignment and omission; active-theme editor/preview backgrounds in Dark and Light; menu coverage for every parser directive/option; dialog required/numeric validation; valid insertion at caret and selected-text replacement; Cancel leaves text and selection unchanged. Cover header, leg, network, and marker record formatting. Run the focused Qt suite and full Release suite.
- **Implementation plan:** (1) Split the residual table into family tables in the Residuals page, preserving columns, precision, warnings, and clearing semantics. (2) Set editor and preview drawing-surface backgrounds from the active palette while retaining syntax legibility. (3) Add the Records menu adjacent to File and explicit typed entries for each supported directive/option; provide appropriate validated fields and Submit/Cancel behavior, formatting and inserting via `QTextCursor` at the caret. Keep `SIDE`/`CHECK` as STN form modifiers and `#IMPORT` as an editor marker. (4) Add focused Qt coverage and capture changed surfaces/states under `build/artifacts/`; present the captures in a T3 visualizer and wait for approval.
- **Verification verdict: APPROVE.** Requirements and acceptance are explicit, scope remains within the existing Qt subsystem, risks have tests, and the implementation uses existing parser, palette, and editor cursor behavior without a new architectural decision.

### Implementation status
- Implemented for D-2026-10-22-i. The residual tab now separates Angle, Distance, Vertical, GPS, and Coordinate families and hides empty groups. The editor and plan preview use the active theme's graphite surface. The Records menu covers all 23 supported forms/options; validated Submit/Cancel dialogs insert at the caret or replace a selection.
- Added widget-tree coverage for residual assignment/visibility/clearing, Dark and Light surface colors, every Records action/group, dialog validation, cancellation, and insertion. The Records menu screenshot captures the open popup.
- Latest `build.bat` build passed. Focused Records, live adjustment, workspace, and stop/discard checks passed; all six Qt editor shards passed; the full Release suite passed 191/191. `git diff --check` passed.
- Captures are in `build/artifacts/Release/` for residual groups, editor and preview in Dark/Light, the Records menu, validation, and insertion. Present these in the T3 visualizer and wait for the user's review before commit/PR.
