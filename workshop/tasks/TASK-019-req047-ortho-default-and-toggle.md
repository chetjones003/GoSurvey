# TASK-019 — REQ-047: ORTHO off by default + reliable F8 toggle (LINE stuck orthogonal)

- Type:    bug
- Status:  self-verify (awaiting user manual verification)
- Opened:  2026-07-13
- Owner:   chetjones003

## 1. Authority
- Goal:         Drawing/draft-command correctness (AutoCAD parity)
- Requirements: REQ-047 (accepted 2026-07-13 — recorded as the SPEC GAP resolution BEFORE the fix)
- Constraints:  root cause identified with evidence; smallest correct fix; no broken functionality;
                CLAUDE.md rules; no new dependency; geometry/coordinates unchanged (REQ-101).
- Acceptance (verbatim, REQ-047): see spec/requirements.md — fresh drawing ORTHO off (free-angle LINE);
  F8/status toggles ORTHO incl. while the command bar is focused; ON constrains to H/V from the anchor;
  object snap overrides ORTHO.
- Owning subsystem: UI (default + key/status toggles) / Commands (pure constraint helper).

## 2. Root cause (with evidence)
- CONFIRMED: ORTHO defaulted ON. `src/app/main.cpp:184` `bool orthoEnabled = true;`, pushed to
  `cmd.orthoMode` every frame (`:224`). Every draw path correctly gates on the flag
  (`CadCommands.cpp` commit `if (st.orthoMode)`; paper `CadUi.cpp:6750`; preview `:8789`) — so the
  ONLY reason lines were stuck orthogonal is the flag starting/staying true. AutoCAD defaults it off.
- CONTRIBUTING (was hypothesis, now fixed): F8 was gated behind `!ioFrame.WantTextInput`
  (`main.cpp:217`); the floating command bar takes keyboard focus when the user types
  (`CadUi.cpp:5659,5777`), so F8 could be swallowed while the bar was focused. The status-bar ORTHO
  button worked (direct toggle), but the documented F8 shortcut did not reliably.

## 3. Spec status — SPEC GAP resolved
- No requirement governed ORTHO. Recorded REQ-047 (+ decision-log entry) defining the intended
  behavior (default off; reliable F8/status toggle; snap wins) BEFORE writing the fix.

## 4. Fix plan → implementation
- `src/app/main.cpp` — (1) `orthoEnabled = false` default (REQ-047 default off); (2) moved F3/F8 mode
  toggles OUT of the `!WantTextInput` gate so they fire even while the command bar is focused (they are
  not text characters — no typing interference). Removed the now-unused `ioFrame` local.
- `src/commands/OrthoConstrain.hpp` (new) — pure `OrthoConstrainPoint(anchorX, anchorY, *x, *y, ortho)`;
  no-op when ortho is false (the disable-able invariant), snap-to-nearer-axis when true. Header-only so
  the commit + preview paths share one unit-tested implementation (NumFormat/AngleFormat precedent).
- `src/commands/CadCommands.cpp` — `ApplyOrthoConstrainFromAnchor` and the commit-side ortho
  (`SubmitViewportPickImpl`) now delegate to `OrthoConstrainPoint` (dedup; same behavior).

## 5. Validation tests
- `tests/OrthoConstrainTests.cpp` (new, `[ortho]`, added to GoSurveyTests): OFF = no-op at any angle
  (the invariant that was violated); ON snaps to the nearer H/V axis; exact-diagonal ties resolve
  horizontal. Note: a classic "fails-before" unit test is not feasible here — the defect was a UI
  default value and a keybinding focus gate (both main-loop state, excluded from the pure-compute test
  target). The helper test guards the constraint semantics going forward; the default + F8 behavior is
  manual per the project's UI-REQ convention (REQ-024/040/045 precedent).

## 6. Regression risks
- Draw commands that rely on ORTHO still constrain correctly when it is ON (delegation preserves the
  exact math — covered by the ON test cases + full suite green).
- F3/F8 firing during text input: safe — they are non-character mode keys; typing a command is
  unaffected (verified: full build, no input-routing change beyond the two toggles).

## 7. Self-verification
- [x] build-project        — PASS (clean Ninja build; both exes link; pre-existing MSVC CRT warning only)
- [x] architecture-review  — PASS (SPEC GAP recorded as REQ-047 before code; pure helper matches the
      established tested-header pattern; no new global/dependency; no architectural decision by Workshop)
- [x] code-review          — PASS (smallest correct fix at the root cause; ortho math deduped to one
      tested helper; readable)
- [x] dependency-audit     — n-a
- [x] performance-review    — n-a (identical per-point math, now one inline function)
- [x] testing              — PASS (58/58 ctest incl. 2 new `[ortho]` cases); default/F8 = manual
- [ ] user manual verification — PENDING

## 8. Verification result
- Verdict: PASS pending user manual verification.
- Findings: none blocking. Note: two CadUi paper-space inline ortho sites (6750, 8789) were left as-is
  (identical math, correct); deduping them to the helper is optional cleanup, not required for REQ-047.

## 9. Outcome
- Requirements satisfied: REQ-047 (Acceptance: pending manual verification of default + F8 toggle).
- Tests added:            tests/OrthoConstrainTests.cpp (2 cases / 8 assertions).
- Refactors:              ortho constraint math → commands/OrthoConstrain.hpp (shared, tested).
- Docs updated:           spec/requirements.md (REQ-047 + traceability), spec/project.md (decision log).
- Done:                   pending user manual verification.
