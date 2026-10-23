# TASK-1041 — Give every Qt command finder result a command icon

- Type: feature
- Status: self-verify
- Opened: 2026-10-23
- Owner: Workshop

## 1. Authority
- Goal: Qt drafting input parity
- Requirements: REQ-040 (accepted), REQ-400 (accepted)
- Constraints: CON-03, CON-06, REQ-300
- Acceptance:
  - The idle Qt command finder shows a relevant command-specific icon beside every registered command result, with no generic green command-link fallback.
  - Reuse a relevant existing icon asset when one exists. Generate a matching icon asset for every command that has no suitable associated icon.
  - Preserve existing matching, result ordering, selection, and command execution behavior.
- Owning subsystem: Qt UI command finder and its resources; command registry/matching remains in Commands.

## 2. Scope
- In scope: Audit all 207 primary command names in `kRegistry`, associate semantically relevant existing icons, generate icons for commands without a suitable match, wire each command to its icon, and add UI coverage.
- Out of scope: changing command names, descriptions, aliases, ranking, ribbon layout, or non-Qt frontends.
- Smallest change: Replace the current QAction-only icon collection with a complete Qt command-name-to-icon association; add only the necessary icon assets.

## 3. Architectural boundary check
- New abstraction, layer, dependency, ownership change, global state, public API/data format, or unspecified algorithm?
  - [x] No — this remains Qt presentation and static resources. Existing command registry remains authoritative for names; icon associations do not alter command semantics.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | How broad is icon coverage and how to source art? | 2026-10-23 | User confirmed all registered commands; reuse relevant existing assets first and generate icons for any remaining commands. |

## 5. Assumptions
- None.

## 6. Plan
- Approach: Build a complete icon association for all primary registry names. Use semantically appropriate existing resource icons where available; generate consistent SVG icons in the established 24×24 line-art style for commands without one. Keep the icon lookup in the Qt UI layer.
- Files/functions to touch: `spec/requirements.md`, `spec/project.md`, `src/qt/EditorWindow.cpp`, new `resources/icons/commands/*.svg` as needed, `tests/QtEditorTests.cpp`, `docs/qt-074-parity-ledger.md`, this task log.
- Test approach: happy path = representative known/semantic and generated icons render in fuzzy results; failure mode = every registry result has a non-null non-fallback icon and no missing asset path silently falls back.
- Steps:
  - [x] Inventory the registry and icon resources; record the confirmed coverage scope.
  - [x] Update the accepted requirement and decision record.
  - [x] Verification review approved the plan.
  - [x] Add Qt coverage for full icon coverage and representative command-finder rendering.
  - [x] Build, run the relevant Qt checks, inspect screenshots, and update task/ledger outcome.

## 7. Workflow-specific notes
- Feature pre-flight: title/problem/goal/requirements/acceptance inferred from user's request and confirmed all-command coverage. Tests-first at the Qt editor seam.
- Inventory: 207 primary entries in `kRegistry`; 798 icon resource files (794 PNG, 4 SVG); 63 exact normalized filename matches. The current finder gathers icons only from QAction metadata, causing unmatched entries to use `QStyle::SP_CommandLink`.

## 8. Implementation log
- 2026-10-23 — User approved coverage of all registered commands, reuse-first sourcing, and generated artwork for commands with no suitable match. Registry audit found 207 primary names (not the preliminary estimate of 215), 794 PNG and 4 SVG library assets, and 63 exact normalized filename matches. Verification approved: Qt owns icon presentation, with no new dependency, API, or command behavior.

- 2026-10-23 — Added the command icon lookup: use Qt action art, semantic aliases, exact normalized library names, then one of 14 generated SVG families selected from the command name/description. SVGs use the existing 24×24 line-art palette and are cached per window after first use; the existing icon-library index is built once per process.
- 2026-10-23 — Regression red/green: forcing `SP_CommandLink` caused `issue1041EveryRegisteredCommandHasAnAssociatedFinderIcon` to fail on LINE (`icon.pixmap(...) != generic` false). Restored the resolver; the isolated test passed and confirmed all 207 commands. The same test captures the generated MASSPROP icon and a mixed list.
- 2026-10-23 — Release build passed. Qt shard 3 passed 40/40 including the all-command test; adjacent shards 1, 2 and 4 passed. The focused one-function run passed 3/3 QtTest cases. `git diff --check` was clean.
- 2026-10-23 — Screenshot evidence is in `build/artifacts/qt-editor/manual/shard-99/artifacts/issue1041/`. Awaiting user approval of the updated Qt visualizer.

## 9. Self-verification
- [x] build-project
- [x] architecture-review
- [x] code-review
- [x] dependency-audit — n/a, no dependency change
- [x] performance-review — index built once; result icons cached after first lookup; no hot path added
- [x] testing

## 10. Verification result
- Plan verdict: APPROVE — REQ-040 owns Qt finder presentation; icon association and assets remain in the Qt UI/resource layer. No new dependency, public format, or command semantics. The complete catalog coverage check is deterministic, and the list is capped at 30 results, so lookup cost is bounded. The all-command Qt test resolves all 207 primary names to relevant, non-fallback icons and captures representative popup rows.
- Final verdict: PASS — build and focused Qt verification passed; waiting on the required visual approval before final closeout.

## 11. Outcome
- Requirements satisfied: REQ-040, icon assignment is verified for all 207 registered commands.
- Tests added: `issue1041EveryRegisteredCommandHasAnAssociatedFinderIcon`.
- Docs updated: accepted REQ-040 amendment, decision D-2026-10-23-b, Qt parity ledger, and this task log.
- Done: implementation and automated verification complete; visual approval pending.