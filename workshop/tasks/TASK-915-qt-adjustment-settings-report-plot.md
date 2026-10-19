# TASK-915 — Qt adjustment project settings, report export, and plotting

- Type: feature
- Status: awaiting final screenshot approval
- Opened: 2026-10-19
- GitHub issue: #915, child of #878

## 1. Authority

- Goal: Qt migration to beta 0.7.4 parity, authorized by issue #915.
- Behavior requirements: accepted REQ-201 (visible failures), REQ-403 (traverse persistence), REQ-418 (measurement residuals), REQ-422 (project adjustment settings and `.gstrav` persistence), and REQ-423 (report content and TXT/PDF/RTF export), with REQ-417–421 as applicable.
- Issue acceptance: preserve beta 0.7.4 cancel, validation, failure, undo, and read-only behavior where applicable; complete usable Qt workflows; add focused workflow coverage; update the 0.7.4 parity ledger; present 2–3 integrated screenshot options for each major affected surface and apply the user's selected direction before completion.
- Owning subsystem: Qt presentation and workflow over existing traverse settings, report, and adjustment plan-view services.

## 2. Scope

- In scope: adjustment project settings; report viewing and supported TXT/PDF/RTF export; the adjusted station plot beside its report; focused Qt workflow coverage; parity-ledger update; final captures.
- Out of scope: changing adjustment math, report contents/formats, PDF plot engine, project/traverse persistence formats, or non-Qt behavior.

## 3. Architectural boundary check

- Reuse the existing `AdjustmentWindow`, `LsaWeights`, `.gstrav` IO, report builder/exporters, and `AdjustmentPlanView`.
- Qt owns interaction and presentation. No new dependency, persisted field, or one-use abstraction is planned.

## 4. Plan

- [x] Read repository governance, issue #915, accepted requirements, and parity ledger.
- [x] Inventory beta 0.7.4 and current Qt settings/report/plot workflows, including cancel, validation, failure, undo, and read-only semantics.
- [x] Verify the plan against the accepted requirements, architecture boundaries, and available Qt services; identify affected screenshot surfaces/states.
- [x] Present integrated visual options for the major affected surfaces and apply the user's selected direction (Option B, 2026-10-19).
- [x] Implement the Qt settings pane, report/plot split, and supported report export actions.
- [x] Add focused Qt coverage for settings Apply/Discard, validation, `.gstrav` persistence, report ordering, supported formats, and export failure.
- [x] Update the 0.7.4 parity ledger with routes, coverage, remaining gaps, and captures.
- [x] Build and run focused verification; capture affected Qt surfaces and states under `build/artifacts/`.
- [x] Show final screenshots inline and receive user approval (2026-10-19).

## 5. Plan verification

- Requirements cover the requested behavior: REQ-422 specifies the instrument/project option set and `.gstrav` persistence; REQ-423 defines report ordering plus TXT/PDF/RTF parity; REQ-418 governs residual reporting. Issue #915's "plot" is the adjustment's station plot, not CAD layout plotting. No solver, report format, or plot-engine change is needed.
- The ImGui reference has a Project Settings popup and report tab with three export formats. The Qt adjustment window had a modal settings dialog and report output/export action, but its settings dialog omitted adjustment type and maximum iterations, and no focused Qt workflow verified Cancel, validation, or `.gstrav` persistence. The Qt window had a station plan preview but no adjusted plot beside the report. Report export existed, but no focused Qt workflow verified report ordering/content or all three output formats.
- Reuse `AdjustmentWindow`, shared `LsaWeights`/`TraverseIo`, `TraverseReport` exporters and `AdjustmentPlanView`. Keep changes in Qt and focused coverage; do not alter shared domain behavior or persistence shape.
- Major affected surfaces/states: (1) adjustment settings, normal/editing and Cancel/validation; (2) adjustment report and station plot, successful result and export cancel/failure. Final screenshots must cover changed UI states.
- Verification risks: "project settings" means the adjustment project's existing `.gstrav` settings per REQ-422 §5; preserve this storage contract. Report exporters already exist and must keep identical content across formats. Export failure must remain visible and leave the current report and adjustment state unchanged.
- Plan result: APPROVE, bounded to the Qt workflows and evidence above. Visual direction is Option B.

## 6. User decision

- 2026-10-19 — User selected **Option B: persistent settings pane + split report/plot**.

## 7. Verification and evidence

- `build.bat qt`: PASS.
- Focused hidden-desktop Qt workflow run: PASS, 3 tests (settings unit labels; settings Apply/Discard/validation/`.gstrav` persistence; report ordering/TXT-RTF-PDF export/station plot/failure preservation), 0 failures.
- `ctest --preset ninja-release -R "^qt[.]editor$"`: unable to run because CTest reports `No tests were found` in `build/.cmake/release`.
- Final captures: `build/artifacts/Release/qt-915-settings-pane.png`, `qt-915-settings-validation-error.png`, `qt-915-settings-applied.png`, and `qt-915-report-and-plot.png`.
- Full `qt.editor` suite result remains unavailable in this checkout; final screenshot approval remains pending.

## 6. Completion report

- Result: PASS for the focused Qt settings, report-export, and station-plot workflows.
- Build: `build.bat qt` passed.
- Tests: the three focused Qt workflows passed (5 QtTest totals including init/cleanup, 0 failures). The full `qt.editor` CTest preset was attempted but reported no tests found in `build/.cmake/release`.
- UI evidence: all four final captures were shown inline and approved by the user on 2026-10-19.
- Documentation: the 0.7.4 parity ledger now records the Qt routes, focused evidence, and screenshots.
- Assumptions and remaining technical debt: issue #915's plot means the existing adjusted-station plan view. This is covered by REQ-418/422/423; no SPEC change was needed. The full CTest target should be run when a test-registered CMake tree is available.
- PR target: `beta`.
