# TASK-1039 — Synchronize Qt command-line and dynamic-cursor prompt input (GitHub issue #1039)

- Type:    bug
- Status:  self-verify
- Opened:  2026-10-22
- Owner:   Workshop

## 1. Authority
- Goal:         Qt drafting input parity
- Requirements: REQ-428 (accepted), REQ-024 (accepted input semantics), REQ-304 (accepted prompt text)
- Constraints:  REQ-201 (honest input failures), REQ-300 (no unjustified dependency), CON-06 (smallest change)
- Acceptance:
  - While a Qt command is waiting for a point, distance, angle, or other prompt input, typing in either the command line or the dynamic-cursor field updates one shared draft and the other surface reflects it.
  - Preserve coordinate X/Y or distance/angle field groups, typed locks, Tab navigation, supported relative/bearing-distance syntax, Enter submission, and viewport-click placement.
  - Enter in either entry surface submits the active draft exactly once and advances the command to its next prompt.
  - Cancellation and command completion clear the shared draft; no stale value carries into a later prompt or command.
  - With no active command prompt, the command line still accepts command names and options as today.
  - Prompt text on both surfaces comes from the same active command state.
- Owning subsystem: Qt UI input routing (`src/qt/`); command/domain semantics remain in existing shared command and dynamic-input services.

## 2. Scope
- In scope: Qt command-line/dynamic-input draft synchronization, submit/cancel/reset lifecycle, regression coverage, parity ledger and specification records.
- Out of scope: changing shared command parsing, changing non-Qt frontends, new drafting modes or syntax.
- Smallest change: route active-prompt edits from either Qt entry widget through the existing `dyninput::Group`, render the same draft in both widgets, and submit once via the existing prompt submission path.

## 3. Architectural boundary check
- New abstraction, layer, dependency, ownership change, global state, public API/data format, or unspecified algorithm?
  - [x] No — reuse the existing Qt widgets, `dyninput::Group`, command submit route, and prompt state.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Which public test seam should pin the synchronization behavior? | 2026-10-22 | User selected the Qt editor integration seam: type in either field, press Enter, then inspect the next prompt and both values. |

## 5. Assumptions
- None.

## 6. Plan
- Approach: Amend accepted REQ-428 to cover shared draft behavior for Qt and record the user-authorized decision from issue #1039; then add Qt editor regression coverage and implement the smallest UI-layer synchronization.
- Files/functions to touch: `spec/requirements.md` (REQ-428), `spec/project.md` (decision log), `src/qt/EditorWindow.cpp/.hpp`, `tests/QtEditorTests.cpp`, `docs/qt-074-parity-ledger.md`, this task log.
- Test approach: happy path = edit either surface during a multi-step point prompt and observe identical draft plus one prompt advance on Enter; failure/lifecycle = cancel/complete and verify later prompts do not inherit the draft, while inactive command entry remains unchanged.
- Steps:
  - [x] Record the accepted requirement amendment and task plan.
  - [x] Verify plan against the specification, architecture, and issue acceptance criteria.
  - [x] Write one Qt integration regression test and implement its vertical slice.
  - [x] Cover the opposite entry direction and clear/reset lifecycle, then update parity evidence.
  - [x] Run the Windows build and relevant Qt/full tests; self-review and record results.

## 7. Workflow-specific notes
- Bug root cause: `m_Command` edits only refreshed autocomplete; dynamic-field edits changed `m_DynamicGroup` but only mirrored into `m_Command` for non-point prompts. Separately, command-line Return first submitted through `returnPressed`, then bubbled to the application event filter and submitted a blank Enter, restarting LINE's first-point prompt.
- Regression test: use `GoSurveyQtTests` at the public editor seam; verify each test fails before the fix.

## 8. Implementation log
- 2026-10-22 — Retrieved GitHub issue #1039 and identified the separate Qt input state paths in `EditorWindow.cpp`.
- 2026-10-22 — Added a Qt-scoped requirement amendment plan under accepted REQ-428; REQ-024 and REQ-304 continue to define input semantics and prompt sourcing.
- 2026-10-22 — Verification plan review: APPROVE. The owning subsystem, public Qt test seam, behavior and reset acceptance map to REQ-428's Qt parity scope and #1039; no architectural boundary is crossed. User had time to respond to the proposed test seam; proceeding with that default.
- 2026-10-22 — Added the Qt integration test first. Red evidence: command-line `12,34` left the dynamic X field at its live cursor value (`0.0000`). After draft synchronization was added, the test exposed a second submit: the first Enter placed the LINE start point, then the bubbled blank Enter restarted LINE. Consuming command-line Return at the KeyPress event (after ShortcutOverride) fixed the duplicate while leaving completer selection available for non-exact suggestions.
- 2026-10-22 — Green evidence: `issue1039CommandLineAndDynamicFieldsShareOneDraft`, `issue903DraftingModesAndCursorFields`, `customTitleBarAndFloatingCommandLineWork`, and `tabsAppearInitiallyAndTypingRoutesFromEditorChrome` each pass on the hidden desktop.
- 2026-10-22 — Added capture points for the synchronized point and scalar draft states and documented the focused workflow in `docs/qt-074-parity-ledger.md`.
- 2026-10-22 — Final validation: `build.bat` succeeded; focused issue #1039 and adjacent Qt routing tests passed; `ctest --preset ninja-release` passed all 191 tests; `git diff --check` was clean. Standards and spec reviews reported no findings.
- 2026-10-22 — Captured the three shared-draft states for the required Qt visual review under `build/artifacts/Release/issue1039/`.

## 9. Self-verification
- [x] build-project
- [x] architecture-review
- [x] code-review
- [ ] dependency-audit — n/a (no dependency change)
- [ ] performance-review — n/a (UI event path only)
- [x] testing

## 10. Verification result
- Plan verdict: APPROVE — accepted REQ-428 owns Qt drafting input; the issue's expected behavior fills the specific shared-draft gap; implementation remains inside Qt UI and reuses existing dynamic-input and command services.
- Final verdict: PASS — build succeeded, all 191 Release tests passed, and both code reviews had no findings. Awaiting user approval of the Qt visualizer before final closeout.

## 11. Outcome
- Requirements satisfied: shared prompt draft, mirrored field state, one Enter submission, reset lifecycle, and inactive command entry remain covered by the integration regression.
- Tests added: `issue1039CommandLineAndDynamicFieldsShareOneDraft`.
- Docs updated: accepted REQ-428 amendment, project decision, Qt parity ledger, and this task log.
- Done: implementation and validation complete; visual approval pending per repository UI workflow.

- 2026-10-23 — User confirmed the command line should show the matching polar form. Red evidence: command line showed @28.284271,28.284271 while dynamic fields showed 40 and 45. Added DraftingInputDisplayText() to format the visible draft as @40<45; submission still uses the existing canonical conversion. The Qt integration assertion failed before the formatter and passed after it; polar-draft-shared.png captures the state.
- 2026-10-23 — Final follow-up Release CTest passed all 191 tests. Updated visual evidence is under uild/artifacts/qt-editor/manual/shard-1/artifacts/issue1039/. Awaiting visual approval of the refreshed combined visualizer.
