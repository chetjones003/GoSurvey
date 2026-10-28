# TASK-1110 — Refine Qt drawing workspace chrome for both appearances (issue #1041)

- Type:    feature
- Status:  self-verify
- Opened:  2026-10-28
- Owner:   Workshop

## 1. Authority
- Goal:         Qt migration / appearance parity; no GOAL-NN is assigned in `spec/project.md`.
- Requirements: REQ-081 (shell separation/elevation), REQ-436 (Qt appearance and palette).
- Constraints:  Preserve the Qt accent palette, viewport content, usable viewport area, commands, docking, resizing, navigation, and responsive layout.
- Acceptance:   Issue #1041: separate the ribbon and workspace panels at the named boundaries; use subtle rounded and raised surfaces; give permanent ribbon tabs modest rounding and distinct hover/selected states; keep Dark and Light readable; preserve existing interactions/layout; capture both appearances for review.
- Owning subsystem: Qt UI (`src/qt/QtAppearance.cpp`, ribbon and dock widget setup in `src/qt/EditorRibbon.cpp` / `src/qt/EditorWindow.cpp`).

## 2. Scope
- In scope: Amend REQ-081 and REQ-436 under a recorded decision to cover only the #1041 Qt drawing-workspace ribbon, its permanent tabs, and the Properties/viewport/Toolspace boundaries in both appearances. Implement that styling and add focused visual/state coverage.
- Out of scope: ImGui chrome, Start page, dialogs, PDF windows, drawing entities, viewport content, accent-color direction (#1031), and behavior changes.
- Smallest change: Extend existing Qt appearance rules and existing Qt UI coverage; no new styling framework or widget abstraction.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / global state / public API / data format / algorithm: **No.** Existing Qt appearance ownership and widgets suffice.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Is the issue's request to amend REQ-081 and style only the listed Qt workspace surfaces in both themes sufficiently specific to resolve its recorded SPEC GAP? | 2026-10-28 | Resolved from evidence: yes. Issue #1041 explicitly requests the narrow scope, both themes, retained palettes, and acceptance checks. User directed the agent to resolve gaps from code/docs first and ask only when evidence does not settle them. Autodesk documentation confirms ribbon/tab/panel roles but makes no styling prescription. Scope and behavior remain bounded by the issue; record D-2026-10-28-a before implementation. |

## 5. Assumptions
None. “Subtle/modest” remains a visual acceptance judged by the issue's required Dark/Light screenshots; do not invent pixel/radius thresholds.

## 6. Plan
- Approach: Record D-2026-10-28-a and amend REQ-081/REQ-436 with a narrow Qt workspace exception. Keep palette values and viewport untouched. Adjust only the existing Qt appearance rules for ribbon boundary, permanent ribbon tabs, dock panel edges, and workspace seams. Add or extend Qt tests for style/state coverage and retain screenshot evidence for Dark and Light review.
- Files/functions to touch: `spec/project.md`, `spec/requirements.md`, `workshop/tasks/TASK-1110-issue1041-qt-workspace-chrome.md`, `verification/reviews/TASK-1110-plan-review.md`, `src/qt/QtAppearance.cpp`, `src/qt/EditorRibbon.cpp`, and `tests/QtEditorTests.cpp`. Update the Qt parity ledger if it tracks this appearance work.
- Test approach: Happy path = both appearances show separated, rounded, subtly raised ribbon/panel surfaces and distinct tab states; failure/regression = switching Light → Dark → Light leaves no stale styles and existing ribbon/dock interaction tests continue to pass. Screenshot both themes at a representative workspace layout.
- Steps:
- [x] Record and apply the narrow specification decision; complete verification review before implementation.
- [x] Implement the appearance changes and focused tests.
- [x] Build, run relevant Qt tests, capture both themes, inspect the docs, and complete code/spec verification.

## 7. Workflow-specific notes
- Feature request is supplied by GitHub issue #1041 and completed the repository feature-request fields; scope, problem, goal, requirements, and observable criteria are present in the issue.
- Tests-first: Add/adjust test coverage before the implementation change where the existing Qt tests expose the needed styling/state seams.

## 8. Implementation log
- 2026-10-28 — Opened in a dedicated worktree from `origin/beta` after user direction to investigate and resolve blockers using code/spec/documentation evidence.
- 2026-10-28 — Inspected REQ-081, REQ-436, existing Qt style rules and Autodesk ribbon/workspace documentation. Findings and evidence are recorded in Q1 and the proposed decision; no code changed.
- 2026-10-28 — Verification approved the bounded plan. Recorded D-2026-10-28-a, amended REQ-081/REQ-436 narrowly for the #1041 Qt workspace surfaces, and removed the `blocked` issue label in favor of `in progress`.
- 2026-10-28 — Implemented workspace-only Qt separators, ribbon boundaries, rounded tab faces and dock panel edges in both appearance styles; enabled hover tracking on the permanent ribbon tab bar. Added a Qt editor test that checks ribbon tab navigation and dock visibility after theme switching/resizing, and captures Home/Insert selected states in Light and Dark.
- 2026-10-28 — `build.bat` passed after the final production-code changes. The first CTest filter matched no tests because Qt cases are shard-suffixed; corrected to all six `qt.editor.shard-*` tests. Final rerun passed 6/6 (389.80 s); `ctest --preset ninja-release -R "^build[.]output-layout$"` passed 1/1. `git diff --check` passed.
- 2026-10-28 — Pre-PR product documentation review: `README.md`, `PRIVACY.md`, `resources/wiki/`, and the public manual wiki linked from gosurveycad.com were reviewed. No updates are needed: the change does not alter commands, panel placement, help entry points, data handling, or workflows. The public manual homepage indexes a User Interface page whose source was absent in the cloned wiki; the in-app `resources/wiki/User-Interface.md` was reviewed and its functional descriptions remain accurate.
- 2026-10-28 — Captured Light and Dark workspace screenshots with Home and Insert selected for visual review. Visual approval is pending before PR creation, as required for visible Qt UI changes.
- 2026-10-28 — A follow-up that floated Properties and Toolspace on the drawing was rejected. Restored the earlier workspace chrome: ribbon and dock edges in place, no canvas gutter and no overlay cards.
- 2026-10-28 — The user rejected the dark-color experiments. Restored the dark workspace to the first #1041 chrome: beveled separator, original drawing surface, and original panel colors.
- 2026-10-28 — The user approved that restored workspace. Re-checked `README.md`, `PRIVACY.md`, `resources/wiki/`, and the earlier public-manual review: still no product-doc update. The change does not alter commands, panel placement, help, data handling, or workflows.

## 9. Self-verification
- [x] build-project — Release build passed after final production-code changes; the subsequent edit changed only test capture timing.
- [x] architecture-review — PASS; existing Qt appearance/widget owners only, no new abstraction or dependency.
- [x] code-review — PASS; reviewed the final diff for Qt-only selector scope, palette/viewport preservation, and focused state/interaction coverage.
- [x] dependency-audit — n/a, no manifests changed.
- [x] performance-review — n/a, no hot path or measurable performance behavior changed.
- [x] testing — final six Qt editor shards passed (6/6); output-layout test passed (1/1).

## 10. Verification result
- Submitted:  2026-10-28
- Verdict:    Plan APPROVE; implementation self-verification PASS. User approved the restored screenshots on 2026-10-28.
- Findings:   `verification/reviews/TASK-1110-plan-review.md`.

## 11. Outcome
- Requirements satisfied: yes. The user approved the Dark and Light workspace screenshots.
- Tests added:            Qt test for appearance switching, tab navigation, dock visibility, resize behavior, and four visual captures.
- Refactors:              none planned
- Docs updated:           REQ-081 and REQ-436 amended; product documentation review recorded, no product docs needed edits.
- Done:                   implementation, verification, and visual approval are complete. Pull request is the next step.

