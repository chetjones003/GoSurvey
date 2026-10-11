# TASK-276 — One Enter is one submit; a flange welds its pipe-end port to the pipe

- Type:    bug
- Status:  self-verify (build green; ready to submit)
- Opened:  2026-09-24
- Owner:   Workshop

## 1. Authority

- Goal:         GOAL-01 (a CAD program a surveyor can actually draw in)
- Requirements:
  - **REQ-024** (command line / prompt behaviour) — accepted. Amended by D-2026-09-24-b and
    D-2026-09-24-c, both of which state the rule this task restores: *a bare Enter reaches the
    active command, and one Enter is one submit.*
  - **REQ-345** (piping runs and fittings) — accepted. Increments B5/B7 own `PickElbowPorts` and
    the `PIPEFIT` splice; the 2026-09-17 follow-ups ("Pipe end" connection port,
    connection-point-driven candidate filtering, multi-port auto-detection + exact-match
    priority — TASK-268/TASK-269) state that a port's configured mode target, never definition
    order and never an `isDefault` fallback, decides which port mates with what.
  - **REQ-350 (f)** — accepted. The palette places through "the existing `PIPEFIT` path (B7),
    unchanged", so a defect in that path is a defect in REQ-350's placement too.
  - **REQ-201** — refuse or resolve by name; never place geometry on a guess.
  - **REQ-161** — the Developer Shell is where GUI-side behaviour is proven.
- Constraints:  CON-01 (Windows/MSVC build unchanged), CON-03 (no new dependency).
- Acceptance (restated):
  - A prompt that advertises an Enter default takes that default **once** per keypress, and a
    command started by typing its name is still running afterwards.
  - A fitting spliced into a pipe run is oriented by its **connection points**, so the port
    configured for a pipe end is the port that meets the pipe.
- Owning subsystem: `src/app` + `src/ui` (input routing) and `src/commands` (fitting
  orientation). No renderer, IO or file-format change.

## 2. Scope

- In scope:
  1. One Enter keypress must reach `ProcessCommandLineSubmit` exactly once.
  2. `PickElbowPorts` must let an explicitly pipe-end-tagged port outrank role and definition
     order when choosing which port is welded to the pipe.
- Out of scope (named, not forgotten):
  - **The end-of-run case.** Splicing is the only placement model `PIPEFIT` has: a pick at the
    very end of a run still cuts the run in two and leaves a short downstream piece beyond the
    fitting. A flange is really an END fitting, and "place a fitting ON an end rather than splice
    through it" is its own increment with its own product rules (which end, what happens to the
    run's length, whether the far port must stay free).
  - Reducer/branch size rules — REQ-350 (c)'s existing named deferral.
  - The engagement-length budget's behaviour at a run end (`availB == 0`), unchanged here.
- Smallest change: a frame stamp on the UI's own submissions plus one extra condition in the raw
  poll; and one ranking pass ahead of the existing role/definition-order resolution.

## 3. Architectural boundary check

- New abstraction / layer / dependency / ownership change / global state / public-API or
  data-format change?
  - [x] **No — proceed.**
- Notes: the frame stamp is file-static state inside `src/ui/CadUi.cpp` behind one accessor, the
  same shape `CadUiIsCommandInputActive()` already has; it is per-frame, not persisted, and
  crosses no layer boundary (`src/app` already includes `CadUi.hpp`). `PickElbowPorts` gains no
  new concept: `CadBlockConnectionHasExactMode` already exists in `cadblock.hpp` and was built
  for exactly this discrimination (TASK-269).

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | none — both defects are implementation failures against accepted requirements, not gaps | — | — |

## 5. Assumptions

```
ASSUMPTION-1: the pipe-end rule fires only when EXACTLY ONE of a two-port fitting's ports
              carries an exact PipeEnd mode.
- Because:       an elbow, a tee's through pair and an inline valve all have a pipe end on both
                 sides, and their Inlet/Outlet authoring is what orients them.
- Risk if wrong: a through-run fitting authored with a pipe end on one side only would be
                 oriented by that port instead of by its roles.
- Validate by:   the third regression test ("A fitting with a pipe end on BOTH sides keeps its
                 Inlet/Outlet resolution") plus the 7 existing `[autofit]` elbow cases and the 6
                 `[branch]` tee cases, all green.
```

## 6. Plan

- Approach:
  - **(1) Enter.** Every UI-side submission goes through one wrapper in `CadUi.cpp` that stamps
    `ImGui::GetFrameCount()`; `main.cpp`'s raw poll asks `CadUiCommandLineSubmittedThisFrame()`
    before firing. Routing every call site through the wrapper — rather than tagging only the two
    Enter-driven ones — is deliberate: this codebase's recurring trap is a hand-wired list that a
    new call site is missing from, which is exactly how D-2026-09-24-c's own defect shipped.
  - **(2) Ports.** In `PickElbowPorts`, rank by `CadBlockConnectionHasExactMode(c, PipeEnd)`
    first; fall through to the existing Inlet/Outlet and definition-order resolution otherwise.
- Files/functions to touch:
  - `src/ui/CadUi.cpp` — `UiSubmitCommandLine`, `CadUiCommandLineSubmittedThisFrame`, all
    in-file submit call sites.
  - `src/ui/CadUi.hpp` — declare the accessor.
  - `src/app/main.cpp` — the raw Enter poll's gate (and drop the now-unused `ioEnter`).
  - `src/commands/CadCommands.cpp` — `PickElbowPorts`.
  - `tests/CadPipeRunCommandTests.cpp`, `src/devshell/DevShellTests.cpp` — regression tests.
- Test approach:
  - happy path: typing a command name and pressing Enter leaves the command running; answering
    PIPERUN's size prompt lands on the wall-thickness prompt; a flange's weld neck lands on the
    pipe and its gasket face one flange length downstream.
  - failure mode: a fitting with a pipe end on both sides must be unaffected; the engagement
    cutback must be read from the weld neck, not the gasket face.
  - Both tests must be shown to FAIL against the unpatched code.
- Steps:
  - [x] reproduce and locate both root causes
  - [x] implement
  - [x] regression tests, proven fails-before
  - [x] full suite
  - [x] clean Release build
  - [x] spec notes + decision record

## 7. Workflow-specific notes (Bug)

**Root cause 1 — one Enter, two submits.**

`main.cpp`'s raw Enter poll exists for the prompts whose command line is hidden. D-2026-09-24-c
re-gated it on `!ImGui::IsAnyItemActive()`. That gate is read AFTER `DrawCommandLinePanel` has
run in the same frame — and an `ImGuiInputTextFlags_EnterReturnsTrue` field clears its own active
ID as the last thing it does, with `ReleaseSubmittedCommandInput()` clearing it again immediately
after the submit. So on the one frame that matters — the frame a field just took the Enter — the
gate reads "nothing is active" and the poll submits a second time. By then
`ProcessCommandLineSubmit` has already emptied `cmdBuf`, so the second submission arrives as a
**bare Enter**.

Most commands hid it: a blank Enter at `LINE`'s first-point prompt matches no branch and is
discarded. It is visible exactly where a blank Enter means something at the FIRST prompt —
`ORBIT`/`PAN` (Enter exits) and `PIPERUN` (Enter takes the remembered size, the schedule-40 wall,
or finishes the run). Hence "typing ORBIT then ENTER just cancels the command", and PIPERUN
walking two prompts per keypress until it reported itself cancelled.

Regression test fails-before: yes — with the new condition neutralised (the poll left live),
`d-2026-09-24-f-one-enter-one-submit` fails at the first check, `s_cmd->active == Kind::Orbit`.
Not unit-testable: the defect lives entirely in which widget consumed the keypress on which
frame.

**Root cause 2 — the flange goes in backwards.**

`PickElbowPorts` resolves a two-port fitting as "the Inlet/Outlet pair if roles are tagged,
otherwise definition order", and `TrySplicePipeFitNamed` welds whichever port it returns as
`near` onto the pipe. The bundled `2in_WELD_NECK_FLANGE` has two ports that are **not**
interchangeable — `gasketFace` (mode target `flange-face`) and `weldNeckFace` (mode target
`pipe-end`) — but gives **both** the `Inlet` role and defines the gasket face **first**. Both of
`PickElbowPorts`' tests therefore chose the gasket face, and the flange was placed end-for-end:
its gasket face welded to the pipe and its weld neck pointing away, which is what the user's
screenshot shows.

This is the same defect TASK-269 fixed for `INSERT`'s connector snap, on the same part, after the
same report — the splice path simply never got the rule. `CadBlockConnectionHasExactMode` (which
deliberately ignores the `isDefault` fallback, because an only-mode port is flagged default by
the authoring UI as a matter of habit rather than intent) is reused verbatim.

Regression test fails-before: yes — with the ranking disabled, `[flangeport]` reports
`gasket.x == 10.0` where 10.2 is required, and the 1.5 ft cutback is read off the wrong port.

## 8. Implementation log

- 2026-09-24 — located fault 1 by reading the frame order in `main.cpp` against
  `DrawCommandLinePanel`'s own "on the Enter frame the input is already inactive" comment;
  confirmed in the Developer Shell.
- 2026-09-24 — located fault 2 by dumping the bundled flange's ADR-044 trailer out of
  `resources/blocks/fittings/2in_WELD_NECK_FLANGE.dwg`: two `inlet`-role ports, gasket face
  first, distinguished only by their mode target.
- 2026-09-24 — both fixes implemented; both regression tests shown to fail against the unpatched
  code and pass against the patched one.
- 2026-09-24 — dropped the unused `ioEnter` left behind by D-2026-09-24-c (MSVC C4189).

## 9. Self-verification

- [x] build-project        — PASS. `./dev/build release` clean (no warnings, no errors), and the
      Developer Shell `GoSurvey.exe` (RelWithDebInfo, `dev/build-devshell.bat`) links clean too.
      The one warning this diff touched — `C4189 'ioEnter' initialized but not referenced`, left
      behind by D-2026-09-24-c — is gone.
- [x] architecture-review  — PASS. No new layer, dependency, ownership or global state; no `gl*`
      outside the renderer; the frame stamp is per-frame UI state behind one accessor.
- [x] code-review          — PASS. Both changes are additive conditions ahead of existing logic;
      no behaviour removed. The `PickElbowPorts` rule is deliberately narrow (exactly one port
      tagged) so every existing caller is provably unaffected.
- [x] dependency-audit     — n/a, no dependency change.
- [x] performance-review   — n/a. One integer compare per frame; one `modes` scan per two-port
      fitting at splice time, not per frame.
- [x] testing              — PASS.
  - `GoSurveySnapTests` **434/434** (up from 431 — 3 new `[flangeport]` cases).
  - `GoSurveyTests` **1218/1219** — the one failure is TASK-272 §10.6's pre-existing cone-apex
    crack.
  - `ctest` **1806/1814** — the same **8 pre-existing** failures named in TASK-275 (7 headless
    transcripts + the cone-apex crack), unchanged in identity and count.
  - Developer Shell: `d-2026-09-24-f-one-enter-one-submit` **Success**,
    `req024-blank-enter-default` **Success**. (`command-line-line` fails when run standalone — it
    never calls `OpenFreshDrawing`, so there is no command bar on the Start tab; verified
    identical on the unpatched tree.)

## 10. Verification result

- Submitted:  —
- Verdict:    self-verification PASS — ready for Verification
- Findings:   —

## 11. Outcome

- Requirements satisfied: REQ-024 (Acceptance met: yes), REQ-345 / REQ-350 (f) (yes)
- Tests added: `[issue486][pipefit][flangeport]` x3 (`tests/CadPipeRunCommandTests.cpp`);
  Developer Shell `d-2026-09-24-f-one-enter-one-submit` (`src/devshell/DevShellTests.cpp`)
- Refactors:   every `ProcessCommandLineSubmit` call in `src/ui/CadUi.cpp` now goes through
  `UiSubmitCommandLine`, so the frame stamp cannot be forgotten at a new call site
- Docs updated: `spec/project.md` (D-2026-09-24-f), `spec/requirements.md` (REQ-345 follow-up
  note), this task
- Done:       —
