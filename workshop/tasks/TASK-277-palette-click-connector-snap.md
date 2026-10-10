# TASK-277 — The palette's one click must SNAP the part, not just drop it

- Type:    bug
- Status:  self-verify (build green; ready to submit)
- Opened:  2026-09-25
- Owner:   Workshop
- Follows: TASK-276 (same user report thread; that one fixed which PORT a splice welds, this one
  fixes the case where no splice happens at all)

## 1. Authority

- Goal:         GOAL-01
- Requirements:
  - **REQ-350 (f)** — accepted, and stated verbatim: *"A click **off any run** places the part as an
    ordinary block INSERT at that point, **with connection-port snapping (REQ-107)** — which is what
    makes the palette usable for a nozzle on a vessel or a flange staged beside the line, neither of
    which is a run splice."* The connection-port snapping half was never wired up.
  - **REQ-107** — accepted. Block INSERT, connection ports, `CadBlockSnapInsertToConnection`.
  - **REQ-345** — accepted. The 2026-09-17 follow-ups (TASK-268/TASK-269) own the
    `CadConnectionModeTarget` matching this reuses unchanged.
  - **REQ-201** — refuse or resolve by name; never leave a click with no effect and no reason.
- Constraints:  CON-01, CON-03.
- Acceptance (restated): clicking a palette-armed part onto a connection port or pipe end places it
  snapped to that port — position and orientation from the port, not from the raw click.
- Owning subsystem: `src/commands` (CadBlocks). No renderer, IO or file-format change.

## 2. Scope

- In scope:
  1. The palette's armed click tries a connection-port snap before falling back to a free placement.
  2. A palette click whose splice is REFUSED still places the part, instead of consuming the click.
- Out of scope (named):
  - The 2 ft connector tolerance (`kSnap`) — REQ-107's existing figure, unchanged here.
  - Compatibility-tag enforcement (`class150` vs `class300`) at snap time. `CadBlockConnectionMode`
    carries the tag and nothing checks it; that is a real product rule of its own, not a corner of
    this fix.
  - Everything TASK-276 §2 already deferred, including "place a fitting ON a run's end".
- Smallest change: one optional-mode flag on the existing snap entry point, and the three-outcome
  ordering in the one place that handles a palette-armed click.

## 3. Architectural boundary check

- New abstraction / layer / dependency / ownership change / global state / public-API or
  data-format change?
  - [x] **No — proceed.**
- Notes: `SubmitInsertBlockConnectorPick` gains a defaulted `bool optional` parameter — a widening of
  an existing signature with every existing caller unchanged, not a new API. No new placement rule:
  the palette reaches the splice and the connector snap that already exist, which is what REQ-350 (f)
  says it is for.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Should a palette click whose SPLICE is refused place the part anyway, or end with the refusal? | 2026-09-25 | Decided in-Workshop as a REQ-201 reading, recorded as **D-2026-09-25-a**: place it. A one-port part can never splice, so refusing consumed the click and placed nothing at all — an outcome the user cannot act on. The refusal is still logged by name; the part still lands where it was clicked. |

## 5. Assumptions

```
ASSUMPTION-1: snapping is attempted for EVERY palette-armed off-run click, not only when the user
              asks for it.
- Because:       REQ-350 (f) gives the palette one click and says where it lands decides the kind;
                 there is no second gesture in which to opt in.
- Risk if wrong: a part the user wanted staged 1 ft from a flange face snaps onto it instead.
- Validate by:   the "nothing in reach" test, and the log line naming what happened either way, so
                 an unwanted snap is visible and one undo away.
```

## 6. Plan

- Approach: in `SubmitInsertBlockPick`'s `WaitInsertPoint` branch, a palette-armed click resolves in
  three ordered outcomes — splice, snap, free place — each strictly more specific than the next.
- Files/functions to touch:
  - `src/commands/CadBlocks.cpp` — `CadPipePaletteArmPart`, `SubmitInsertBlockPick`,
    `SubmitInsertBlockConnectorPick`.
  - `src/commands/CadBlocks.hpp` — the widened signature.
  - `tests/CadBlockImportTests.cpp` — regression tests.
- Test approach:
  - happy path: a blind flange clicked on a placed flange's gasket face lands ON that face, facing
    into it.
  - failure mode: nothing within 2 ft still places, and says why it is unoriented; a part that
    cannot splice still lands where it was clicked.
  - All three must FAIL against the unpatched code.
- Steps:
  - [x] reproduce and locate the root cause
  - [x] implement
  - [x] regression tests, proven fails-before
  - [x] full suite
  - [x] clean Release build
  - [x] spec notes + decision record

## 7. Workflow-specific notes (Bug)

**Root cause.** `CadPipePaletteArmPart` sets up a one-click INSERT and its own comment says *"its
orientation comes from the port it snaps to"* — but it never enables any snap, and
`SubmitInsertBlockPick` only had two outcomes: splice if the click lands on a pipe run, otherwise
place free. A **blind flange bolts onto another flange's FACE**, never into the middle of a pipe, so
its click never reaches the splice; it fell straight through to a free placement and was dropped with
the identity rotation it was authored with. That is the reported "not aligning correctly".

Two aggravating details, both visible in the user's BEDIT screenshot:
- The bundled `2IN_BLIND_FLANGE` has **one** connection point (`GASKET_FACE`, single mode targeting
  *Flange face*, flagged Default). `PickElbowPorts` needs two, so the splice can never take it — and
  before this fix a click that DID land on a run was consumed with nothing placed.
- Its mode targets a flange face, and the weld-neck flange already in the drawing exposes exactly
  that (`CadBlockClassifyPortTarget` reads a `Flange`-typed part's port as `FlangeFace`). So the
  metadata to orient it correctly was complete and present; nothing consulted it.

The snap itself needed no new code: `SubmitInsertBlockConnectorPick` already does the whole job,
including TASK-269's exact-match-before-default two-pass search — which matters here, because the
pipe run's end is also within reach and the blind flange's port accepts `PipeEnd` only through its
`isDefault` fallback. The exact `FlangeFace` pairing therefore wins over the nearer pipe end, which
is the correct answer and the reason that two-pass design exists.

It only needed to become *askable*: an `optional` flag so "nothing nearby" returns false without
logging a refusal, letting the palette take its documented fallback and report its own outcome.

Regression tests fail-before: yes, all three —
- the blind flange lands at the raw click point with no `snapped to` in the log;
- the "nothing in reach" case never reports why it is unoriented;
- the un-spliceable part places **nothing at all** (`PlacedPortOf` returns false), which is the
  dead end D-2026-09-25-a closes.

## 8. Implementation log

- 2026-09-25 — user reported the blind flange landing unaligned, with a BEDIT screenshot showing its
  single `GASKET_FACE` port and its *Flange face* mode.
- 2026-09-25 — traced to `CadPipePaletteArmPart` never enabling a snap; confirmed the one-port part
  can also dead-end a run click.
- 2026-09-25 — implemented the three-outcome ordering; cleared `insertBlockConnectorName` and the
  stale dialog rotations on arm, so every port is a candidate and no leftover dialog state pins the
  search to one.
- 2026-09-25 — a `'\0'` literal written through a shell heredoc arrived as a real NUL byte
  (`error C2137: empty character constant`), the same escape-collapsing trap TASK-275 recorded.
  Repaired by writing the byte directly and re-checked the file for NULs and mixed line endings.

## 9. Self-verification

- [x] build-project        — PASS. `./dev/build release` clean. The seven `C4834` warnings in
      `CadBlockImportTests.cpp` are at lines 1804–2249, well above this diff's insertion point, and
      are pre-existing.
- [x] architecture-review  — PASS. No new layer, dependency, ownership or global state; a defaulted
      parameter on an existing entry point, and ordering in the one function that already owned this
      decision.
- [x] code-review          — PASS. No behaviour removed: every existing caller of
      `SubmitInsertBlockConnectorPick` keeps `optional = false` and its named refusals.
- [x] dependency-audit     — n/a.
- [x] performance-review   — n/a. One extra nearest-port scan per placement click, not per frame.
- [x] testing              — PASS.
  - `GoSurveySnapTests` **437/437** (up from 434 — 3 new `[connectorsnap]` cases).
  - `GoSurveyTests` **1218/1219** — TASK-272 §10.6's pre-existing cone-apex crack.
  - `ctest` **1809/1817** — the same **8 pre-existing** failures named in TASK-275/276.
- [ ] GUI confirmation     — the mouse half is the user's to confirm: arm `2IN_BLIND_FLANGE` and
      click the weld-neck flange's face. The unit tests drive the real entry points
      (`CadPipePaletteArmPart` + `SubmitInsertBlockPick`), so everything below the click is covered.

## 10. Verification result

- Submitted:  —
- Verdict:    self-verification PASS — ready for Verification
- Findings:   —

## 11. Outcome

- Requirements satisfied: REQ-350 (f) (Acceptance met: yes), REQ-107 / REQ-201 (yes)
- Tests added: `[issue486][req350][palette][connectorsnap]` x3 (`tests/CadBlockImportTests.cpp`)
- Refactors:   none
- Docs updated: `spec/project.md` (D-2026-09-25-a), `spec/requirements.md` (REQ-350 note), this task
- Done:       —
