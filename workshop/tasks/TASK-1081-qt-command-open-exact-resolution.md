# TASK-1081 — Exact OPEN submission in the Qt command bar

- Type: bug
- Status: needs review
- Opened: 2026-10-24
- Owner: Workshop

## 1. Authority
- Goal: Qt drafting input parity
- Requirements: REQ-040 (accepted), REQ-400 (accepted)
- Constraints: CON-03, CON-06, REQ-300, REQ-201
- Acceptance:
  - Typing the exact registered command `OPEN` in the Qt command bar and pressing Enter opens the drawing picker (`Drawing (*.dwg)`), even when `OPENPACK` is highlighted from a stale or earlier query.
  - Non-exact single-token input continues to submit the currently selected fuzzy suggestion; multi-token input and active drafting prompts retain their current behavior.
  - The regression is exercised through the real command-bar widget.
- Owning subsystem: Qt command input resolution; command registry/matching remains in Commands.

## 2. Scope
- In scope: Resolve exact typed command names and aliases before consulting the selected suggestion on Enter; Qt integration coverage; this task log.
- Out of scope: Changing fuzzy ranking, command aliases, dialogs, other frontends, or the accepted specification.
- Smallest change: At the Qt submit seam, use the existing command-resolution helper for exact bare tokens and preserve selected-row activation for non-exact queries.

## 3. Architectural boundary check
- Does this need a new abstraction, layer, dependency, ownership change, global state, public API/data-format change, or unspecified algorithm?
  - No. REQ-400 already defines the resolution precedence. Keep the fix in Qt input routing and reuse existing command matching.

## 4. Questions and assumptions
- None. Ticket #1081 and REQ-400 define the expected behavior.

## 5. Plan and verification
- Files/functions: `src/qt/EditorInput.cpp` Enter routing; `tests/QtEditorTests.cpp`; this task log.
- Test: use the real command widget to type `OPEN`, set the visible suggestion selection to `OPENPACK`, press Enter, and assert the drawing-open dialog was launched. Also retain existing exact alias, selected fuzzy result, and active-prompt coverage.
- Architectural check: accepted REQ-400 assigns exact-vs-suggestion resolution to the command-line UI; no cross-layer change.
- Plan verdict: APPROVE. This directly repairs the specified precedence at the Qt submit boundary and does not broaden command semantics.

## 6. Implementation log
- 2026-10-24 — Ticket #1081 reports `OPEN` Enter activating `OPENPACK`. Confirmed `REQ-400` acceptance already requires exact names and aliases to win; current Qt Enter handling submits the highlighted row directly. Issue status moved to `in progress`.
- 2026-10-24 — The command registry contains `OPENPACK` but no `OPEN`; `OPEN` is routed directly by the Qt window. Extended ranked suggestions to mark exact primary/alias matches, and made Enter prefer those matches, the UI-routed `OPEN` action, or the original multi-token line before falling back to the selected suggestion.
- 2026-10-24 — Regression red/green: the real Qt command bar test failed before the fix because it opened `Open Packed Project`; it now verifies `Open drawing` with the DWG filter. The same test verifies a selected `MKLINE` cannot override exact `LINE`. The existing finder test verifies exact alias vs prefix metadata.
- 2026-10-24 — `build.bat` succeeded. Focused hidden-desktop regression passed (3/3 QtTest cases). Full `ctest --preset ninja-release` passed 191/191, including all six Qt editor shards, deployment, and parity ledger. `git diff --check` passed.

## 7. Verification result
- Plan verdict: APPROVE — existing REQ-400 specifies exact-match precedence; the task is a UI-resolution defect with no spec change or architecture boundary change.
- Implementation verdict: PASS — focused and full automated verification passed. Code review pending.

## 8. Outcome
- Requirements satisfied: REQ-040 and REQ-400 command-line behavior for exact command names and aliases, including the UI-routed `OPEN` action.
- Tests added: `issue1081QtExactOpenWinsOverOpenPackSuggestion`; exact-match flags verified in `issue1040QtCommandFinderFuzzyKeyboardAndMouseSelection`.
- Documentation updated: task log only; product documentation is unaffected.
- Build: Release build succeeded. Full suite passed 191/191.
- Assumptions / technical debt: none.
