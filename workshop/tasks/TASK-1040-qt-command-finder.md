# TASK-1040 — Make the Qt command finder fuzzy and selectable

- Type: feature
- Status: self-verify
- Opened: 2026-10-22
- Owner: Workshop

## 1. Authority
- Goal: Qt drafting input parity
- Requirements: REQ-040 (accepted), REQ-400 (accepted)
- Constraints: CON-03, CON-06, REQ-300, REQ-201
- Acceptance:
  - REQ-040: The command-name autocomplete popup ranks fuzzy command matches, displays each result's command icon and name in a rounded, scrollable list, and supports keyboard and mouse selection. Enter or clicking a result runs the selected command. `l` ranks `LINE` first by its registered alias and `lin` ranks `LINE` first by name. Dynamic-cursor input and clickable prompt hints continue to work.
  - REQ-400: Exact primary names and aliases rank ahead of prefixes, and prefixes rank ahead of subsequence matches. Enter launches the currently highlighted result, clicking launches the clicked result, and the popup remains scrollable. Exact typed-command resolution remains fresh; multi-token lines remain unchanged.
- Owning subsystem: Qt UI owns popup presentation and selection; the existing command matcher owns command ranking. No command execution semantics outside the existing ranked selection change.

## 2. Scope
- In scope: fuzzy ranked Qt command suggestions, command icons, rounded popup styling, keyboard Enter activation, mouse click/scroll activation, and integration coverage.
- Out of scope: dynamic-cursor prompt changes, aliases/registry changes, new icon assets, command behavior changes, and other frontends.
- Smallest change: expose the existing subsequence scoring through `FuzzyCommandSuggestions` after exact and prefix ranking, then show ranked rows in a focused `QListView` popup with icons and explicit keyboard/mouse activation.

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec did not specify?
  - [x] No — reuse the existing command registry, `FuzzySubsequenceScore`, `FuzzyCommandSuggestions`, and existing command `QAction` icons. The popup is a concrete Qt view owned by `EditorWindow`; it adds no abstraction or layer. The accepted decision specifies exact/alias, prefix, then subsequence ranking.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | What should selecting a fuzzy result do? | 2026-10-22 | Enter runs the highlighted command; mouse clicking and scrolling the list work. User confirmed the assembled feature scope on 2026-10-22. |

## 5. Assumptions
- None.

## 6. Plan
- Approach: Retain exact primary/alias ranking, add name subsequence-ranked suggestions using the existing scorer, populate a styled Qt list popup with ranked icon-bearing rows, and route keyboard/mouse activation through `EditorWindow::Submit`.
- Files/functions to touch: `src/commands/CadCommands.cpp` (`FuzzyCommandSuggestions`), `src/qt/EditorWindow.cpp` (popup, styling and icons), `src/qt/EditorInput.cpp` (Enter/Escape routing), `src/qt/EditorWindow.hpp` (owned widget pointers), `tests/QtEditorTests.cpp`, `docs/qt-074-parity-ledger.md`, `spec/project.md`, `spec/requirements.md`, this task log.
- Test approach: happy path = `l` and `lin` rank LINE first, Enter launches the selected command, clicking a result launches it; failure/edge = empty input closes suggestions, no matches do not reuse a stale result, active dynamic prompt continues to bypass the command popup, and scrolling changes the visible results.
- Steps:
  - [x] Record user-confirmed requirements and acceptance in the accepted spec decision.
  - [x] Verify the plan against REQ-040, REQ-400, architecture, and issue acceptance.
  - [x] Add public Qt integration tests and capture the popup state.
  - [x] Implement the ranked icon-bearing popup and launch routing.
  - [x] Build, run focused and full tests, review, and update the ledger/task outcome.

## 7. Workflow-specific notes
- Feature pre-flight: answered; user chose fuzzy-ranked suggestions, Enter launches highlighted result, and mouse click/scroll operate the popup. Tests-first through the public Qt editor.

## 8. Implementation log
- 2026-10-22 — User requested rounded popup corners, command icons, working fuzzy matching, Enter activation, and mouse click/scroll selection; the assembled scope was confirmed.
- 2026-10-22 — Verification plan verdict: APPROVE. REQ-040 owns the Qt autocomplete surface; REQ-400 owns matching and exact-vs-suggestion resolution. The plan reuses the command registry's existing subsequence scorer and Qt list popup, with Qt retaining popup/icon presentation ownership. Risks are stale selection, accidental Enter propagation, empty results, and interference with active-prompt drafts; each has an integration assertion. No boundary crossing or dependency change.
- 2026-10-22 — Implementation choice updated after the first Qt regression run: QCompleter rewrote the line edit with its highlighted completion during continued typing, so typing `lne` became `LINEne` and a fuzzy query could not finish. Replaced it with a concrete `QListView` popup while retaining the same ranked model and Qt UI ownership; this stays within the approved subsystem and introduces no reusable abstraction. Enter propagation was routed around the active suggestion popup, and Escape now dismisses that popup before it reaches command cancellation.
- 2026-10-22 — Focused Qt integration is green for fuzzy/prefix ranking, Up/Down highlight selection, Enter, click, scroll, no-match unchanged submission, empty dismissal, active prompt isolation, and the existing #1039 shared draft. The first test-first run failed on the absent `lne` popup as expected.
- 2026-10-22 — Release build succeeded. The full CTest run passed 190/191; the sole parity-ledger failure was its status-prefix rule, corrected to `Verified (visual approval pending)`, and `qt.parity-ledger` then passed. The refreshed focused #1040 integration test passed 3/3 and captured the combined editor/popup state. `git diff --check` is clean.
- 2026-10-23 — Follow-up reproduction for Enter reported by the user: the original regression test sent Enter to the line edit and passed, but sending it to the visible Qt popup failed with empty command history. Root cause: the top-level `Qt::Popup` is outside the `EditorWindow` ancestry check, so its keyboard-grab events bypassed the editor's Enter handler. Updated event routing for the popup and descendants; the regression now sends Up, Down, and Enter through the popup and passes. Adjacent #1039/shared input and typing-routing tests pass (5/5 combined), and the parity ledger check passes.

## 9. Self-verification
- [x] build-project
- [x] architecture-review
- [x] code-review
- [ ] dependency-audit — n/a (no dependency change)
- [ ] performance-review — n/a (bounded command registry scan; no hot loop added)
- [x] testing

## 10. Verification result
- Plan verdict: APPROVE — see implementation log.
- Final verdict: PASS for implementation and automated verification; user is testing the rebuilt app.

## 11. Outcome
- Requirements satisfied: fuzzy ranking, icons, rounded scrollable results, keyboard/mouse selection, and activation are covered at the Qt editor seam.
- Tests added: `issue1040QtCommandFinderFuzzyKeyboardAndMouseSelection`.
- Docs updated: accepted REQ-040/REQ-400 scope, project decision, parity ledger, and this task log.
- Done: implementation and automated verification complete; updated app launched for user testing.

- 2026-10-23 — User reproduced continued typing being truncated to 	, with RECT highlighted after entering 	raverse. Red evidence: Qt Popup keyboard grab stopped delivery to the line edit after its first character. Replaced the popup flag with a nonactivating tool window and added a regression typing through the real popup path; it failed before the change and now preserves 	raverse with TRAVERSE selected. Outside-click dismissal is also covered. Full Release CTest passed all 191 tests; refreshed evidence is under uild/artifacts/qt-editor/manual/shard-2/artifacts/issue1040/.
