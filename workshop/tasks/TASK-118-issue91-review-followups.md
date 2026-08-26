# TASK-118 — REQ-121 review follow-ups: the OSNAP override seam, and a prompt that was not true (GitHub issue #91)

- Type:    fix (two defects), plus one stated scope boundary
- Status:  done
- Opened:  2026-08-26
- Owner:   Nathan Johnson

## 1. Authority
- Requirements: **REQ-121** — accepted 2026-08-26 (D-2026-08-26-a), amended 2026-08-26 by
  **D-2026-08-26-d**, which is this task's authority.
- Also honoured: REQ-305 / D-2026-08-25-l (the accumulate-until-Enter shape DELETE and JOIN adopt
  here, unchanged), REQ-103 step 5 (STRETCH's box stays load-bearing), REQ-201 (both new refusals
  state their reason), REQ-045 (untouched).
- Acceptance: REQ-121's acceptance list, plus the two conditions D-2026-08-26-d added.
- Owning subsystem: `UI` (the snap-override gates), `Viewport` (the DELETE/JOIN route), `Commands`
  (their selection step and its Enter).

## 2. Scope
- In scope: chetjones003's three review findings on PR #102, and nothing else.
- Out of scope:
  - **paper space** — a stated scope boundary on REQ-121 now, and GitHub issue #106. Fixing it means
    giving paper-space modify commands a real selection phase (§7), which is a behaviour change
    outside this requirement and was the user's call, taken 2026-08-26;
  - re-litigating REQ-121's idle exclusion, settled 2026-08-26 and load-bearing for §7's finding.
- Smallest change: two boolean gates, one route, one Enter branch.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No.** The route change moves two `case` labels between existing enumerators. The Enter
          handling joins an existing `else if` chain beside MOVE/COPY. The two OSNAP gates are
          `&& !ViewportIsObjectSelectionStep(cmd)` on predicates that already existed. Nothing new
          is introduced; one enumerator (`SelectionBox`) simply ends up with two users instead of
          four, and both remaining users are asserted so it cannot quietly empty out.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | DELETE/JOIN: fix the prompt, or fix the behaviour? | 2026-08-26 | **Fix the behaviour.** A second box-only prompt would make rule (3) "one of two prompts", to preserve a shape nobody had chosen — DELETE/JOIN were never *excluded* from D-2026-08-25-l, merely never included. User's decision. |
| Q2 | Paper space: fix here, or state the boundary? | 2026-08-26 | **State it and file it.** The gap is two deliberate decisions meeting (paper modify commands are pick-first; idle selection is excluded), so closing it is a paper-space behaviour change, not a REQ-121 fix. User's decision. |
| Q3 | Finding 1 (pickbox not rendering) does not reproduce — ask the reviewer, or proceed? | 2026-08-26 | **Proceed**, with the evidence posted so he can see exactly what was tested (§13). |

## 5. Assumptions
```
ASSUMPTION-1: DELETE's box should keep excluding survey points.
- Because:       the old two-corner box passed inclSurvey=false, and `finishBox` independently
                 computes false for DELETE/JOIN — so routing through it preserves the behaviour
                 rather than changing it. Survey points still reach DELETE the way they always
                 did, through `selectedSurveyPointIndices`, which the new Enter branch handles
                 first for StartDeleteCommand's own stated reason (the linked label).
- Risk if wrong: a user who boxes over survey points during DELETE expects them erased and they
                 are not — the same expectation gap that exists today, neither widened nor closed.
- Validate by:   read both paths and confirmed the flag is false either way. Deliberately NOT
                 changed here: making the box take survey points is a behaviour change with no
                 finding behind it.
```

## 6. Plan
- `src/viewport/ViewportPickPolicy.hpp` — `K::Delete` / `K::Join` → `SelectionAccumulate`; ZOOM
  keeps `SelectionBox` with its reason restated (STRETCH is now its only object-selecting user).
- `src/commands/CadCommands.cpp` — `SubmitViewportPickImpl`: the DELETE/JOIN box merges via the
  existing `finishBox()` instead of executing. `ProcessCommandLineSubmit`: Enter acts, in the
  `line.empty()` chain beside MOVE/COPY (§8 — this is where the first attempt was wrong). Both
  opening messages reworded to match the behaviour they now have.
- `src/ui/CadUi.cpp` — gate `allowSnapCycle`. (Originally also gated the `pendingOneShotSnapValid`
  consumption directly; superseded during rebase — see §14.)
- Tests: one `ViewportPickPolicyTests` case for the route; one headless transcript for the
  behaviour; GUI for the two rules no test can reach.

- Steps:
  - [x] 1. Reproduce all three findings before changing anything (§8).
  - [x] 2. Route + box-merge + Enter.
  - [x] 3. Both OSNAP gates.
  - [x] 4. Transcript, proven red on `beta`.
  - [x] 5. GUI pass, A/B against a control.

## 7. Workflow-specific notes
- **Finding 1 did not reproduce, and that had to be established rather than assumed.** On the merged
  tree, MOVE / DELETE / TRIM / STRETCH / ALIGN / OFFSET all draw the pickbox in model space, with
  LINE as a crosshair control and idle correctly excluded (§13). Reporting "works for me" is worth
  nothing without the matrix, which is why the matrix is the evidence posted to the issue.
- **What the hunt found instead is the useful part.** In PAPER space every object-selection step
  shows the crosshair, no pickbox and no prompt — and the cause is not a bug in the predicate.
  `StartDeleteCommand`, `StartMoveCommand` and their siblings are **pick-first** in paper space:
  they act on an existing paper selection or answer *"select paper object(s) or viewport(s) first"*
  and return **without ever setting `st.active`**. There is no selection *step* for the rules to
  apply to; the selection is made idle, which REQ-121 excludes by decision. Two deliberate choices
  meeting. Stated on the requirement and filed rather than silently fixed.
- **The bug the reviewer described and the bug that exists are different bugs.** Worth recording:
  a reviewer's diagnosis ("the predicate is evaluating false when it shouldn't") was correct about
  the symptom and wrong about the cause, and the only way to tell was to drive the real window in
  every space rather than re-read the draw site.

## 8. Implementation log
- 2026-08-26 Both real findings reproduced by reading before any edit: `allowSnapCycle` checks only
  `cmd.active != None`, and the `pendingOneShotSnapValid` consumption is the `if` branch whose
  `else` holds the REQ-121 gate — so a forced snap bypassed the rule entirely, exactly as reported.
  (Against `09db6e5`, this task's original base — see §14 for what changed by the time this rebased
  onto `beta`.)
- 2026-08-26 **The Enter handling was put in the wrong place first.** It went where the old
  "finish window-select" message was, which only ever ran for typed TEXT: the `line.empty()` block
  higher up consumes a bare Enter and always returns, so the new branch was unreachable. Caught by
  the transcript failing on the "Nothing selected" assertion, not by the build. The old location
  now carries a corrected message for the typed-text case, which was equally stale.
- 2026-08-26 `boxSelCam2` / `boxSelCam3` — aliases that existed only for the two blocks this change
  merged into one — removed, or /W4 would have flagged them.

## 14. Rebase addendum — 2026-08-26

Rebased onto `beta` (`425afa7`) after chetjones003's re-review of this PR (#107) found it was
authored against `09db6e5` and had drifted: `beta` gained issue #103's fix in between, which
**replaced the exact mechanism §8's implementation log describes gating** — the one-shot
`pendingOneShotSnapValid`/`pendingOneShotSnapX/Y/Kind` value became a persistent per-kind lock,
`objectSnapKindOverrideValid`/`objectSnapKindOverrideKind`, consumed every frame as an `onlyKind`
filter to `CadSnap::FindBest` inside `snapViewportActive`'s own `!ViewportIsObjectSelectionStep`
gate.

**What this means for finding (1):** the symptom this task reproduced and fixed against `09db6e5`
— the crosshair jumping and a marker drawing mid-selection when a forced snap was consumed — **no
longer reproduces on `beta`**, because #103's restructuring incidentally moved the consumption
site behind a gate that already excludes selection steps. Confirmed by reading `425afa7`'s diff
directly rather than assumed: the only remaining gap is that `allowSnapCycle` (the menu opener)
had no selection-step check of its own, so the "Snap once — choose type" menu could still be
*opened* mid-selection and arm the lock off a selection-step pixel — invisible during that step
(already gated at consumption) but then silently spent on the *next* ordinary point-entry snap
instead, a smaller, later surprise rather than the on-the-spot one originally reported. That is the
one gate this rebase re-applies (`allowSnapCycle`, `CadUi.cpp` — unchanged from the original patch,
since that predicate and its call site were untouched by #103). The direct
`pendingOneShotSnapValid` gate this task's original implementation log (§8) describes adding no
longer has anything to gate — that whole `if`/`else` branch was removed by #103 and replaced with
the unconditional block its consumption now lives in.

D-2026-08-26-d (`spec/project.md`) and REQ-121's traceability row (`spec/requirements.md`) rewritten
to state this rather than repeat the now-inaccurate "both seams gated" framing. DELETE/JOIN
(finding 2) and the paper-space scope boundary (finding 3 / DEBT-2) are unaffected by the rebase —
neither file touched by #103.

Test count in §11 corrected from the original 615/615 (stale — predates #104's REQ-306 and #105's
REQ-122, both merged to `beta` before this rebase) to the rebased tree's actual result: full clean
build (`build.bat`, zero errors, no new `/W4 /permissive-` warnings) then `ctest` from `build/`,
**633/634 passing, 1 reported failed**. The one failure (`FindBest respects the snap tolerance —
nothing outside it is ever returned`, a CadSnapTests case from issue #103, untouched by this task)
is not a regression: `ctest`'s discovery mechanism passes the Catch2 test name as an exact-match
filter, and the em-dash in that name gets mangled by the Windows console encoding CTest uses,
so the filter matches nothing ("No tests ran") rather than exercising the test. Confirmed by
running the same test directly against the built exe (`GoSurveySnapTests.exe "FindBest respects
the snap tolerance*"`, wildcard sidesteps the em-dash) on **both** this rebased tree and unmodified
`beta` — passes on both, identically. **634/634 real.**

## 9. Self-verification
- [x] build-project        — PASS (clean; no new warnings under /W4 /permissive-)
- [x] architecture-review  — PASS (§3; no new type, field, or signature)
- [x] code-review          — PASS (the DELETE/JOIN box blocks collapse into one, which is smaller than what it replaces)
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a (two extra predicate calls on click/right-click paths)
- [x] testing              — PASS (§11)

## 10. Verification result
- Submitted:  2026-08-26
- Verdict:    PASS
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-121 as amended (all acceptance conditions, including the two added by
                          D-2026-08-26-d — met); REQ-305/D-2026-08-25-l (DELETE and JOIN now use
                          its shape unchanged — met); REQ-201 (both refusals state their reason —
                          met); REQ-103 step 5 (STRETCH untouched — met)
- Tests added:            `tests/headless/transcripts/req121-delete-join-accumulate.txt` (58 steps)
                          and one `ViewportPickPolicyTests [req121]` case. The transcript was run
                          against unmodified `beta` and fails at the load-bearing assertion —
                          `LINES: expected 3, got 2`, the closing box erasing — then passes after.
                          **634/634 ctest green post-rebase** (§14 — one em-dash filter artifact,
                          not a regression, confirmed passing directly on both trees).
- Refactors:              the two near-identical DELETE and JOIN box blocks in
                          `SubmitViewportPickImpl` collapse into one that calls `finishBox()`
- Docs updated:           `spec/requirements.md` (REQ-121 rules 1 and 3 amended, two acceptance
                          conditions added, the paper-space scope boundary stated, traceability row
                          added), `spec/project.md` (D-2026-08-26-d)
- Done:                   2026-08-26

## 12. Technical debt
```
DEBT-1: REQ-121's three rules remain GUI-verified only.
- What:      the pickbox cursor, the snap-marker suppression and the prompt text are all drawn or
             screen-space, and `GoSurveyTests` deliberately links no command or UI translation
             unit (ADR-002). TASK-115 §12 recorded the two routes that look viable and are not.
- Changed by this task: the BEHAVIOUR half is now automated — the route is a unit test and
             accumulate-until-Enter is a transcript. What is still manual is what the user SEES.
- Remove by: unchanged — nothing short of a UI-automation driver, which the anti-requirements
             rule out deliberately (REQ-203's own entry).
- Follow-up: none. This is the accepted cost of that anti-requirement, not a gap to close.
```
```
DEBT-2: Paper space has none of REQ-121's treatment.
- What:      §7. Paper-space modify commands are pick-first, so no object-selection step exists
             there and the three rules never engage.
- Forced by: closing it is a paper-space behaviour change (giving those commands a real selection
             phase), not a REQ-121 fix.
- Cost:      a user working in a layout gets the old undifferentiated crosshair throughout.
- Remove by: a requirement for paper-space modify commands to have a selection phase.
- Follow-up: FILED as GitHub issue #106 (the user's decision, Q2), and stated on REQ-121 so it
             is visible to whoever reads the requirement rather than only to whoever reads this.
```

## 13. GUI verification — 2026-08-26
Two rounds: the matrix that established finding 1 does not reproduce, and the pass on this task's
own changes. Every result read off a screenshot.

### Round 1 — finding 1, on merged `beta` (09db6e5) with no changes applied
| Where | Command | Cursor |
|---|---|---|
| model | MOVE, DELETE, TRIM, STRETCH, ALIGN, OFFSET | **pickbox**, no arms, with the "Select objects…" prompt beside it |
| model | LINE (control) | full crosshair with its aperture box |
| model | idle, no command | full crosshair — REQ-121's deliberate exclusion |
| model | pick-first: select a line, *then* MOVE | full crosshair, prompt "Base point" — correct, the selection step is skipped |
| paper | MOVE | refused: *"MOVE — select object(s) first."* — never starts |
| paper | DELETE | **starts** (*"select paper object(s) or viewport(s) first"*) yet shows a crosshair and no prompt — §7 |
| floating model space | MOVE | crosshair (same pick-first refusal) |

The last three rows are the finding that replaced the reported one.

### Round 2 — this task's changes
- **DELETE accumulates by click.** Three lines. `DELETE`, then a click ON the bottom line and a
  click ON the middle line: both turn yellow with grips, **all three lines are still present**, the
  cursor is a pickbox and the prompt reads `Select objects, ENTER to continue | ESC cancel` — which
  is now true. Enter logs `Deleted 2 object(s).` and the top line survives. This is the
  click-on-entity half no transcript can reach.
- **JOIN accumulates the same way.** Two legs of an L clicked individually, both highlighted, both
  still present; Enter logs `JOIN — created 1 polyline(s).`
- **The snap-override menu, A/B against a control.** Same gesture, same pixel, twice:
  under `LINE` (a point step) Shift+Right-click opens *"Snap once — choose type"* with
  Endpoint / Midpoint / Center / Survey / Intersection; under `DELETE` (a selection step) **nothing
  opens** — only the pickbox and the prompt. The control is what makes the negative result mean
  something: "no menu appeared" also happens when the gesture never reached the app at all.
