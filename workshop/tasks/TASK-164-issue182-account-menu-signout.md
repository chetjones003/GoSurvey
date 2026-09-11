# TASK-164 — Sign Out on Start screen + menu-bar account dropdown

- Type:    feature
- Status:  self-verify
- Opened:  2026-09-01
- Owner:   chetjones003

## 1. Authority
- Goal:         product usability of REQ-091 sign-in
- Requirements: REQ-091 (accepted) — amended 2026-09-01 (D-2026-09-01-a) for this work
- Constraints:  no new identity/auth mechanism; GUI-only
- Acceptance (added 2026-09-01 to REQ-091):
  - while signed in, a Sign Out control is available on the Start-screen "Connect" card
    and from a dropdown on the far-right menu-bar email; each performs the same sign-out
    as Settings ▸ Account;
  - the far-right menu-bar email opens a dropdown offering Account Details and Sign Out;
    nothing is shown there while signed out;
  - Account Details opens a small read-only window showing the signed-in email and a
    "more coming soon" note — no tier, no editable fields.
- Owning subsystem: UI (`src/ui/`, `src/app/main.cpp` wiring)

## 2. Scope
- In scope: Start-screen Sign Out button; menu-bar email → menu (Account Details + Sign
  Out); `DrawAccountDetailsWindow`; one new state flag `showAccountDetailsWindow`.
- Out of scope: license tier display, editable account fields, any Auth0 write path.
- Smallest change: reuse `cmd.authSignOutRequested` (already handled in main.cpp:609);
  add one bool for the window.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public-API or
  data-format change / new algorithm?
    - [x] No — proceed. (New surface "Account Details" recorded as a REQ-091 amendment
          via D-2026-09-01-a before coding, per CLAUDE.md §5.)

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | What does "Account Details" show, and is it editable? | 2026-09-01 | Email + "coming soon" only; read-only. Recorded as REQ-091 amendment. |

## 6. Plan
- `CadCommands.hpp`: add `bool showAccountDetailsWindow`.
- `CadUi.cpp` DrawMainMenuBar: far-right email → `ImGui::BeginMenu(email)` with
  Account Details (sets flag) + Sign Out (sets `authSignOutRequested`).
- `CadUi_StartScreen.cpp` DrawConnectColumn: grow signed-in card 108→156, add a
  "Sign Out" StyledButton setting `authSignOutRequested`.
- `CadUi_StartScreen.cpp`: `DrawAccountDetailsWindow(cmd)` — read-only window.
- `CadUi.hpp`: declare it. `main.cpp`: call after `DrawSettingsPanel`.
- Test approach: GUI-only; manual verification (per project norm, matches REQ-091/308).

## 8. Implementation log
- 2026-09-01 — SPEC GAP raised on Account Details scope; user chose minimal (email +
  "coming soon"). Recorded D-2026-09-01-a, amended REQ-091. Implemented all four sites.

## 9. Self-verification
- [ ] build-project        — pending
- [x] architecture-review  — PASS (no Workshop architectural decision; amendment recorded first)
- [x] code-review          — PASS (reuses existing flag; one new bool)
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a
- [x] testing              — manual only (GUI); no unit surface

## 11. Outcome
- Requirements satisfied: REQ-091 (amended acceptance met)
- Tests added: none (GUI-only, manual)
- Docs updated: spec/requirements.md (REQ-091), spec/project.md (D-2026-09-01-a)
