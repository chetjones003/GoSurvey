# TASK-115 — Object selection is a visibly distinct mode (REQ-121)

- Type:    feature
- Status:  plan — no code written. Opened with REQ-121 in the same spec PR, on PR #89's precedent.
- Opened:  2026-08-26
- Owner:   Nathan Johnson

Upstream issue: chetjones003/GoSurvey#91.

## 1. Authority
- Requirements: **REQ-121** — accepted 2026-08-26 by **D-2026-08-26-a**, in the same change that
  opens this task. Before that acceptance there was **no** authority for this work: `grep` over
  `spec/requirements.md` found no accepted requirement mentioning a pickbox cursor or OSNAP
  suppression, which under CLAUDE.md step 1 makes it a Specification task, not a Workshop one.
- Also honoured: REQ-304 (the dynamic cursor text and the command line say the same thing —
  rule (3) is REQ-304's rule applied to one more prompt); REQ-305 (accumulate-until-Enter,
  explicitly untouched); REQ-103 step 5 (STRETCH's crossing box stays load-bearing).
- Acceptance: REQ-121's seven conditions, restated there.
- Owning subsystem: `UI` for the cursor and marker suppression, `Commands` for the shared prompt
  string and the selection-step predicate. The raw-vs-snapped pick paths in `Viewport` already
  exist and are mostly correct — see §2.

## 2. Scope

Recon corrected the issue's premise before any of this was scoped, and the correction shrinks the
work: **the hit-test half is already right; the visual half does not exist.**

| | today | REQ-121 |
|---|---|---|
| pick hit-tests against | raw unsnapped cursor, for most steps | raw, for **every** step |
| cursor drawn at | snapped — it jumps | raw — it tracks the mouse |
| snap markers | drawn | not drawn |
| prompt wording | different in every command | one shared string |

- In scope:
  1. **One predicate** — "is an object-selection step active?" — exhaustive over the phases, with
     no `default:`, on `ViewportClickRouteFor`'s precedent. All three rules consult it. This is the
     load-bearing piece: it is what stops a command being half-included a fourth time.
  2. **Marker + cursor-adjustment suppression** while it is true.
  3. **Pickbox cursor** while it is true, reusing the crosshair config's existing
     `pickbox half-size in px` rather than adding a tunable.
  4. **One shared prompt string**, rendered in the command line and the dynamic cursor text.
  5. **The ALIGN gap** — `K::Align`'s `PickSelection` routes to `SelectionAccumulate`
     (`ViewportPickPolicy.hpp:126`) but is absent from `ViewportUseRawWorldForSelectionRectPick`
     (`:9`), so its box corners are snapped while all six siblings are raw. Found while drafting
     REQ-121; fixed here as part of rule (1)'s completeness, not as a separate task.
- Out of scope:
  - which objects each command can select, and the accumulation logic (REQ-305, already done);
  - STRETCH's crossing-box semantics (REQ-103 step 5) — it gets the treatment, its box does not
    change meaning;
  - snapping outside selection steps, which is untouched.

## 3. Architectural boundary check  (fill BEFORE planning)
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [ ] No.
    - [x] **One question to settle before code, and it is small but real.** Rule (1) introduces a
          predicate consulted by both `UI` (cursor, markers) and `Commands` (prompt). Where it
          lives decides whether this is reuse or a new seam:
          - `ViewportPickPolicy.hpp` already holds exactly this kind of pure phase predicate
            (`ViewportUseRawWorldForSelectionRectPick`), is already included by both sides, and is
            already unit-tested by `ViewportPickPolicyTests`. Putting it there is **reuse of an
            established pattern, not a new abstraction** — and it puts the new predicate beside the
            one whose incompleteness caused the ALIGN gap, which is where a reader will look.
          - The alternative — a field on `AppCommandState` — would be new mutable state
            duplicating what the phase enums already say, and could disagree with them.
          Recommendation: the former. Recorded here rather than assumed, because "where does the
          shared predicate live" is the one decision in this task that a reviewer could reasonably
          answer differently.

## 4. Questions  (ask before guessing)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Exact prompt wording. REQ-121 fixes that there is **one** string; it does not fix what it says. #91 suggests "Select objects, ENTER to continue". | — | — |
| Q2 | Does idle selection (no command running) get the pickbox too? REQ-121 calls idle an object-selection step, so by the letter yes — but the idle crosshair is what the user sees most of the time, so this is the most visible change in the task and worth confirming rather than inferring. | — | — |
| Q3 | Do the entity-picking loops of TRIM/EXTEND/FILLET/CHAMFER/BREAK/LENGTHEN get the pickbox as well, or only the `PickSelection` phases? They pick objects, so REQ-121 lists them — but TRIM owns its own pick entry point (`SubmitTrimViewportPick`) and may need its own handling. | — | — |

## 5. Assumptions
```
(none recorded yet — Q1-Q3 must be answered rather than assumed. Q2 in particular changes what the
user sees during normal idle use, which is not a detail to settle by guessing.)
```

## 6. Plan  (write BEFORE any code)
- Steps:
  - [ ] 1. Answer Q1–Q3.
  - [ ] 2. The predicate, in `ViewportPickPolicy.hpp`, exhaustive with no `default:`; extend
           `ViewportPickPolicyTests` to assert every selection phase answers true. **The ALIGN gap
           is the red-before test here** — it fails on the existing code and passes after.
  - [ ] 3. Fix `ViewportUseRawWorldForSelectionRectPick` to include ALIGN (rule 1 completeness).
  - [ ] 4. Suppress snap markers and cursor adjustment while the predicate is true.
  - [ ] 5. Pickbox cursor while it is true; revert on phase advance and on cancel.
  - [ ] 6. One shared prompt string through the command line and the dynamic cursor text.
  - [ ] 7. Audit every command in REQ-121's list; confirm none is left behind.
- Test approach: **split, deliberately.** The predicate and the ALIGN fix are pure and go to
  `ViewportPickPolicyTests` plus a headless transcript — that is where an off-by-one command would
  hide, and it is fully automatable. The cursor shape and marker suppression are **rendered GUI**,
  which this project's anti-requirements exclude from automation, so they are a manual pass.
  Prompt text is assertable headlessly via `EXPECT LOG`.

## 7. Workflow-specific notes
- Feature: the pre-flight is Q1–Q3 plus §3's placement decision. Do not start at step 2.
- This task file ships **with** REQ-121 in one spec PR and contains no code, mirroring PR #89
  (`spec: accept REQ-119 and open TASK-111`). Chet's merge is the acceptance.

## 8. Implementation log
- 2026-08-26 Opened. Recon before drafting REQ-121 established that the issue's "several commands
  already do this internally" is half right in a way worth writing down: hit-testing is already
  raw, cursor drawing and markers are not, and the two disagreeing on screen is the actual defect.
  The ALIGN gap was found in the same read.

## 9. Self-verification
- [ ] build-project / architecture-review / code-review / dependency-audit / performance-review /
      testing — none run; no code yet.

## 10. Verification result
- Not submitted.

## 11. Outcome
- Not delivered.
