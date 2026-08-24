# TASK-100 — LENGTHEN: accept a pick before the sub-mode has a value

- Type:    bug
- Status:  self-verify
- Opened:  2026-08-24
- Owner:   Claude (agent)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         REQ-103 — Modify-command completeness (GOAL per spec/project.md D-2026-08-23-j)
- Requirements: REQ-103 (step 2, LENGTHEN) — `accepted`, acceptance amended by D-2026-08-24-e
- Constraints:  REQ-201 (no silent failures), REQ-101 (±0.01 ft tolerance)
- Acceptance:   the condition added by D-2026-08-24-e, restated: "a pick made before the active
  sub-mode has a value is accepted, not refused: the picked object is latched, its current length
  is reported, and that sub-mode's value prompt opens; the value typed next applies to that object
  immediately rather than only arming the mode. Typing a mode letter at that prompt switches
  sub-mode and keeps the latched object (DYnamic hands it straight to the drag). A refused length
  clears the latch, so a later value can never silently apply to a stale object."
- Owning subsystem: Commands (`src/commands/CadCommands.{hpp,cpp}`) — the LENGTHEN state machine.

## 2. Scope
- In scope: model-space LENGTHEN's entry flow; the paper-space pick's refusal message, which now
  reports the object's current length instead of refusing bare.
- Out of scope: giving paper-space LENGTHEN a real value prompt — the paper path runs with
  `active == None` and has no command state to hold one; that is a documented simplification
  (`paperLengthenPhase`'s comment), not a regression this task introduces. Recorded as debt below.
- Smallest change: one flag on the existing `lengthenPending*` latch (already built for DYnamic,
  which had the same "remember the object between two inputs" problem) plus the two functions that
  open and close the prompt.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed. One new transient bool in `AppCommandState`, the same category as every
          other `lengthenPending*` field beside it. No new entity kind, dependency, or persisted
          state; the arithmetic is the existing `LengthenResolveTargetLength`/
          `ApplyLengthenToEntity`, unchanged.
    - [ ] Yes → STOP.
- The user-facing FLOW change is an acceptance amendment, escalated properly: asked, decided, and
  recorded as D-2026-08-24-e before any code was written.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Is the refused valueless pick the breakage reported, and should the first pick report the current length (AutoCAD parity) or prompt for the value there and then? | 2026-08-24 | Prompt for the value after the pick |

## 5. Assumptions  (workflow.md §8)

```
ASSUMPTION-1: a mode letter typed AT the value prompt keeps the latched object rather than
discarding it.
- Because:       the user's chosen flow settles what a pick does, not what a mode switch mid-prompt
                 does, and the spec is silent.
- Risk if wrong: low, and it fails safe — the worst case is that a user who picks, switches mode,
                 and types a value affects the object they had already picked, which is the object
                 they were looking at.
- Validate by:   discarding it would recreate the very dead end this task removes, one step later
                 ("pick, then realise you wanted Percent" would answer "must be a finite number").
                 Pinned by part 2 of lengthen-pick-first.txt.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: `HandleLengthenViewportPick`'s `!lengthenModeValueSet` branch stops refusing and calls
  a new `LengthenBeginValuePromptForPick`, which latches the entity/near-end/current length, arms
  `lengthenPendingApplyOnValue`, opens the mode's value phase, and logs the current length. The
  three value-parsing branches of `HandleLengthenText` then call `LengthenApplyPendingPick`, which
  applies and disarms. A mode letter typed while armed is routed to `TryLengthenModeToggle` (which
  already sets the right value phase) with the latch left intact.
- Files/functions to touch: `CadCommands.hpp` (`lengthenPendingApplyOnValue`); `CadCommands.cpp`
  (`HandleLengthenViewportPick`, `HandleLengthenText`, `ApplyLengthenToPaperEntity`,
  `ResetModifyRotateDraft`); new transcript.
- Test approach: happy path = pick with nothing set, type a value, geometry changes.
  failure mode = a value that would collapse the entity is refused AND clears the latch, proven by
  a following value being read as a mode letter rather than silently applied.
- Steps:
  - [x] 1. `lengthenPendingApplyOnValue` + the two helpers
  - [x] 2. Wire the three value phases; keep the latch across a mode switch
  - [x] 3. Clear the latch on ESC (`ResetModifyRotateDraft`)
  - [x] 4. Paper-space pick reports current length instead of a bare refusal
  - [x] 5. `lengthen-pick-first.txt`; full regression

## 7. Workflow-specific notes
- Bug: root cause = `HandleLengthenViewportPick` refused any pick while `lengthenModeValueSet` was
  false. The command's own opening prompt ("select object, or [DElta/Percent/Total/DYnamic]")
  invited exactly that pick, so from the Modify ribbon — which offers no way to type a mode — the
  command could not be completed at all. Not a routing bug (TASK-099 fixed that; the click reaches
  the handler and the entity is found), and not a geometry bug: all four sub-modes were verified
  correct on Line, Arc and open Polyline before any change was made.
  Regression test fails-before: `lengthen-pick-first.txt` asserts the log line
  "current length 100.000", which the unpatched code never emits — it emitted the refusal instead.

## 8. Implementation log  (append as you work)
- 2026-08-24 reproduced headlessly before diagnosing: LENGTHEN, then one pick, with nothing typed →
  "LENGTHEN — no DElta value set yet; type DE/P/T first." and no geometry change. Then confirmed
  the opposite — that the machinery underneath is sound — by driving all four sub-modes across
  Line, Arc and open Polyline with a value set; every one applied correctly. So the fault was in
  the entry flow alone, which is what made this an acceptance question rather than a code fix.
- 2026-08-24 escalated rather than guessed: the flow the user wanted was not derivable from
  REQ-103, so it was asked with two concrete options and recorded as D-2026-08-24-e.
- 2026-08-24 implemented and verified. Also fixed the mid-prompt mode switch (part 2), which would
  otherwise have reproduced the same dead end one step further in.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS (no new warnings)
- [x] architecture-review  — PASS (no Workshop architectural decision; the flow change was
      escalated to the spec first, per §3)
- [x] code-review          — PASS. The latch is cleared on every exit: apply, refusal, ESC.
- [x] dependency-audit     — n/a
- [x] performance-review   — n/a (per pick)
- [x] testing              — PASS. `lengthen-pick-first.txt` covers the happy path, the mid-prompt
      mode switch, and the refusal-clears-the-latch failure mode. Full suite green.

## 10. Verification result
- Submitted:  2026-08-24
- Verdict:    pending (self-verification complete)
- Findings:   none open

## 11. Outcome
- Requirements satisfied: REQ-103 step 2, as amended (Acceptance met: yes, model space; paper space
  per the stated simplification)
- Tests added:            `tests/headless/transcripts/lengthen-pick-first.txt`
- Refactors:              none
- Docs updated:           `spec/project.md` (D-2026-08-24-e), `spec/requirements.md` REQ-103
- Technical debt noted:   paper-space LENGTHEN still cannot prompt for a value, because the paper
                          path runs with `active == None`. Removal condition: paper-space commands
                          gaining their own text-prompt surface (the same gap MIRROR's paper path
                          documents). Its pick now at least reports the current length.
- Done:                   2026-08-24 (pending Verification and the user's GUI pass)

## 12. Follow-up — default sub-mode changed to Total (2026-08-24, D-2026-08-24-f)

The user reviewed the pick-first entry and asked for the default sub-mode to be Total rather than
DElta, describing the whole interaction they wanted: "the user selects the line, the readout gives
the length (say 50, the user types 100) the line should now be 100."

Implemented as one line — `lengthenMode`'s initialiser — because D-2026-08-24-e had already built
everything else that flow needs. Verified end to end:

```
LENGTHEN — select object, or [DElta/Percent/Total/DYnamic] <Total>. ESC cancels.
LENGTHEN — current length 50.000. Total — new total length:
LENGTHEN — length 50.000000 -> 100.000000.
```

`lengthen-pick-first.txt` was rewritten around the new default; part 1 now pins exactly the
exchange quoted above. `lengthen-delta.txt` is unaffected — it types `DE` explicitly.

**One asymmetry, kept deliberately and flagged rather than quietly resolved.** The user described
the readout-then-type exchange as *the* interaction, but REQ-103's accepted acceptance requires
that "the active sub-mode persists across repeated picks", and the value it carries persists with
it. So the readout-and-prompt happens on the first pick of a session, and a later pick applies the
armed total straight away without asking. Making Total re-prompt every time would have contradicted
an accepted condition, so it was not done on my own judgement — it is pinned by
`lengthen-pick-first.txt` part 1 (a second 50-long line jumping straight to 100) so the behaviour is
visible in the corpus rather than incidental, and raised with the user for their call. Reversing it
is a small change to `LengthenApplyPendingPick`'s caller if they want every pick to prompt.
