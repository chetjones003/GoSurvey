# TASK-1105 — Restyle crash and startup report windows (issue #1004)

- Type: feature
- Status: in review
- Opened: 2026-10-25
- Owner: Codex

## 1. Authority
- Goal: REQ-425 purpose — explain crashes/startup failures and provide a clear reporting path.
- Requirements: REQ-425 (accepted), amended by D-2026-10-25-e.
- Constraints: preserve REQ-425 §2–§5 behavior; use existing Qt Light/Dark palettes and QtAccent tokens; no new dependency or data flow.
- Acceptance:
  - The crash report window has a modern branded, theme-aware hierarchy: problem heading, next-step explanation, bounded read-only full-report pane, primary **Send This Report**, secondary **Close**.
  - The same visual system is used for crash and startup failure reports and follows the active Qt Light or Dark appearance.
  - Existing behavior remains: Send copies the report, opens the GitHub new-issue URL, reports outcomes, and leaves the window open; Close closes without further sending.
  - Qt tests cover the structure/behavior and capture crash and startup windows in Light and Dark appearances. The screenshots are presented for user approval before the PR is updated.
- Owning subsystem: Qt UI (`src/qt/FailureReport.*`) with tests in `tests/QtEditorTests.cpp`; platform report semantics remain in `src/platform/FailureReportUi.*`.

## 2. Scope
- In scope: refresh the report dialog's visual hierarchy and theme surfaces; extend Qt tests and screenshot capture; update REQ-425, decision history, and this task log.
- Out of scope: changing crash/startup reporting, report text, network behavior, GitHub issue behavior, settings/privacy disclosure, or admin dashboard.
- Smallest change: restyle the existing dialog and controls using existing Qt accent/theme infrastructure; retain its current report and callbacks.

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
  - [x] No — D-2026-10-25-e authorizes the visual hierarchy; this stays within the existing Qt dialog and appearance APIs.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Which direction should guide the restyle? | 2026-10-25 | Modern branded, theme-aware; preserve report/actions and improve hierarchy in Light and Dark. |

## 5. Assumptions
- None; the user selected the visual direction and the accepted REQ-425 behavior bounds the dialog.

## 6. Plan
- Approach: refine the existing branded dialog with a hierarchy and surfaces that use the current Qt appearance/accent palette; keep one dialog implementation for both failure types.
- Files/functions to touch: `src/qt/FailureReport.cpp`; `tests/QtEditorTests.cpp` screenshot and structural coverage; `spec/requirements.md`, `spec/project.md`; this task log.
- Test approach: existing send/close behavior test remains green; add checks for read-only report, hierarchy, and active Light/Dark styling; screenshot crash and startup states in both appearances through the hidden-desktop Qt test.
- Steps:
  - [x] Record D-2026-10-25-e and amend accepted REQ-425.
  - [x] Complete architectural/risk/test-plan verification.
  - [x] Restyle the dialog and add hierarchy assertions plus crash/startup screenshots for Light and Dark.
  - [x] Build and run Qt tests; present screenshots in the T3 visualizer and wait for user approval.
  - [x] After approval, review product documentation.
  - [x] Commit the approved changes and update the existing issue #1004 PR.

## 7. Workflow-specific notes
- Feature: user selected direction; prior accepted navy-banner design is extended with theme-aware hierarchy. Visual approval remains required before PR updates.

## 8. Implementation log
- 2026-10-25 — Verification approves the bounded Qt-only plan; no implementation started before this verdict.
- 2026-10-25 — Restyled the shared report window with a theme-aware branded header, next-steps panel, bounded report pane, and clearly ordered actions. Added structural assertions and four visual states. The first full Qt shard run passed (6/6). After a contrast adjustment, rebuilt successfully and reran shard 5 (1/1); it contains the report-window behavior and screenshot tests. `git diff --check` passed. The documented `^qt[.]editor$` CTest filter matches no names in this checkout because Qt tests are sharded; the current shard name filter is `^qt[.]editor[.]shard-5$`.
- 2026-10-25 — User approved the four-state T3 visualizer. Pre-PR product documentation review: `README.md` reviewed; no update needed. `PRIVACY.md` reviewed; no update needed because this slice changes appearance only and all data handling stays the same. `resources/wiki/Troubleshooting.md` reviewed; no update needed because the existing startup-report guidance and action names remain accurate. Public GoSurvey GitHub Wiki `Troubleshooting` page reviewed; no update needed for the same reason. The website homepage was inaccessible through the web reader, so the linked public manual identified by the repository instructions was reviewed directly.
- 2026-10-25 — Committed as `0dd994b5` and pushed to `feat/crash-report-build-id`; updated PR #1101 summary/evidence and recorded the four-source documentation review. The PR remains open for review.

## 9. Self-verification
- [x] build-project — Release build passed.
- [x] architecture-review — presentation-only Qt dialog change; no boundary or dependency change.
- [x] code-review — action behavior preserved; no blocking findings in self-review.
- [ ] dependency-audit — n/a
- [ ] performance-review — n/a
- [x] testing — all six Qt shards passed before the final contrast-only adjustment; shard 5 passed after it and refreshed the four screenshots.

## 10. Verification result
- Submitted: 2026-10-25
- Verdict: APPROVE
- Findings: none. The change is presentation-only and preserves existing behavior and architecture.

## 11. Outcome
- Requirements satisfied: REQ-425 report-window hierarchy and theme behavior; existing report/action behavior preserved.
- Tests added: report hierarchy assertions and Light/Dark crash/startup screenshot coverage.
- Docs updated: REQ-425 and D-2026-10-25-e; product documentation review recorded above, no product-doc updates needed.
- Done: screenshots approved, product documentation reviewed, commit pushed, and PR #1101 updated.
