# TASK-919 — Prepare the 0.7.4 Qt parity sign-off packet

- Type: docs and testing
- Status: submitted; issue closed at user request; PR #967 open and unmerged; full-suite verification has known failures
- Opened: 2026-10-19
- GitHub issue: #919, child of #878

## 1. Authority

- Goal: Qt migration to beta 0.7.4 parity and user sign-off; tracker #878 keeps Qt additive and keeps the production default and ImGui executable unchanged.
- Requirements: REQ-091 (startup sign-in choice), REQ-382 (project lock/read-only behavior), REQ-436 (Qt Dark appearance), and the accepted REQs cited by each row in `docs/qt-074-parity-ledger.md`. This packet records evidence against those existing requirements; it adds no product behavior.
- Constraints: issue #919 explicitly prohibits switching production defaults or removing ImGui; preserve 0.7.4 cancel, validation, failure, undo and read-only behavior. Keep captures and logs in `build/artifacts/` and out of commits.
- Issue acceptance: audit the completed ledger; summarize verification evidence and remaining limits; capture final selected-design screenshots; add focused workflow coverage for completed behavior; preserve production defaults and ImGui. Any visible UI changes require 2–3 integrated screenshot options per major surface and the user's selected direction.
- Owning subsystem: documentation and test verification; no production subsystem changes are planned.

## 2. Scope

- In scope: add a sign-off packet that reconciles tracker #878 items #879–#919, documents build/test evidence and known gaps, and links the final selected design captures; bring the existing Qt parity ledger up to date; add a consistency check; preserve focused tests for read-only-save and sign-in-decline persistence behavior.
- Out of scope: changing application behavior, Qt appearance, production default, installer, ImGui availability, or closing GitHub issues.
- Smallest change: use existing workflow tests and screenshots, report unverified claims as limitations, and add only the checks and documentation needed to keep the packet accurate.

## 3. Architectural boundary check

- [x] No new abstraction, layer, dependency, ownership change, global state, public API, or file-format change.

## 4. Questions

| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| — | No unresolved product or architecture ambiguity identified. | — | — |

## 5. Assumptions

ASSUMPTION-1: Earlier user selections for the Qt visual directions remain the approved direction for this sign-off capture.
- Because: the tracker asks to capture final selected designs; the individual UI task records and issue history document selected options.
- Risk if wrong: the packet could present a stale visual direction.
- Validate by: presenting the final capture gallery to the user before treating sign-off as complete.

## 6. Plan

- Approach: compare issue #878's tracker with merged work and the existing ledger; verify test names and evidence; document unresolved verification honestly; capture a compact representative gallery of the selected final UI; add a repeatable ledger/packet check without adding dependencies.
- Files/functions to touch: `docs/qt-074-parity-ledger.md`, `docs/qt-074-signoff-packet.md`, traceability rows in `spec/requirements.md`, `tools/verify-qt-parity-ledger.ps1`, CMake test registration, `tests/QtEditorTests.cpp`, `tools/run-qt-tests-hidden.ps1`, and this task log.
- Test approach: run the new packet consistency check; build Qt and run focused issue 919 tests; run the prescribed full Release suite if practical and record every failure. Screenshot capture uses the hidden Qt desktop and stores PNGs under `build/artifacts/`.
- Steps:
  - [x] Reconcile tracker items, accepted requirements, ledger statuses, existing workflow evidence, and visual approvals.
  - [x] Finish focused regression coverage and consistency automation; remove temporary crash breadcrumbs.
  - [x] Write the packet and update the ledger with verified evidence and explicit limits.
  - [x] Build/run required verification and assemble the selected final-design captures.
  - [x] Present the screenshot gallery for user review.
  - [x] Record the user's visual review and its limited sign-off scope.

## 7. Workflow-specific notes

- Documentation: every status and verification statement must be traceable to an issue, task record, test, build, or captured artifact. Do not upgrade a claim without evidence.
- Testing: new test cases must cover successful behavior and specified failure/read-only behavior; preserve drawing state on refusal.

## 8. Implementation log

- 2026-10-19 — Recovered interrupted work. Existing unstaged changes include two focused issue 919 regression tests, a partial parity-ledger checker, and temporary `DBGMARK` breadcrumbs in the topography screenshot test. The latter will be removed before compilation.
- 2026-10-19 — Removed the temporary breadcrumbs, added #900–#902 and #914 evidence to the ledger, added the sign-off packet and tracker reconciliation, and added a CTest consistency check. Updated REQ-091/REQ-382 traceability with the focused Qt slots.
- 2026-10-19 — Fixed the hidden Qt runner's conversion of unsigned Windows crash codes so an access violation is reported as the actual process failure. This did not fix the underlying intermittent screenshot-test crash.
- 2026-10-19 — `.\build.bat qt` passed. Both #919 focused Qt slots passed independently (3 QtTest totals each including setup/cleanup). CTest `qt.parity-ledger` passed. Full Release CTest **failed: 2683/2685 passed**; `headless.regression-surfacedelete-selection` has an invalid surface selection at step 5, and `qt.editor` segfaulted at `QWidget::grab` in `approvedTopographyStaysInPanelBackgrounds`. See `build/artifacts/ctest-919-final.log`.
- 2026-10-19 — Presented the selected Start, drawing-workspace and point-cloud attachment captures in the T3 visualizer. User approved the captures. This is visual approval for #919 only, not the production migration decision under #878.

## 9. Self-verification

- [x] Build project — Qt Release target built; full suite result is recorded separately below.
- [x] Architecture review — PASS; docs, tests, CMake verification registration and the hidden-runner exit conversion stay within existing layers; no new dependency/state owner/API/data format.
- [x] Code review — PASS for focused changes; both new tests assert state preservation and existing undo/persistence behavior; checker references current tests and tracker rows.
- [x] Dependency audit — n/a; no dependency or manifest changed.
- [x] Performance review — n/a; no production hot path changed.
- [ ] Testing — FAIL for the full suite (two failures); both focused tests and the new CTest consistency check pass.

## 10. Verification result

- Submitted: 2026-10-19
- Verdict: FAIL — full Release suite has two failures. User visual approval is recorded; the suite failures remain disclosed and unresolved.
- Findings: `headless.regression-surfacedelete-selection` fails its step-5 fixture invariant; `approvedTopographyStaysInPanelBackgrounds` intermittently crashes in `QWidget::grab`. The runner reports the crash code correctly after the fix.

## 11. Outcome

- Requirements satisfied: issue #919 acceptance; applicable accepted requirements are cross-referenced in the packet.
- Tests added: `issue919TypedDraftingInAReadOnlyProjectDrawingCannotBeSaved`, `issue919DeclinedSignInThatCannotBeSavedKeepsTheChoiceOpenAndSaysWhy`; CTest `qt.parity-ledger`.
- Refactors: none
- Docs updated: parity ledger, sign-off packet, REQ-091/REQ-382 traceability rows.
- Done: visual review approved; PR #967 opened against `beta`, and issue #919 closed at the user's direction. The PR remains unmerged while checks run.
