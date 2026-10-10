# TASK-110 — Issue #86: paper-space CIRCLE/ARC/ELLIPSE commit into the MODEL store

- Type:    bug
- Status:  done — fixed, tested, and verified against the unfixed binary
- Opened:  2026-08-25
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-039** (paper-space objects have full model-space parity), Statement (e)
                ("draw commands LINE, TEXT, MTEXT, CIRCLE, ARC, ELLIPSE, POLYLINE create into the
                active layout's paper store") and Acceptance (5)/(6) — both already `accepted`.
- Constraints:  REQ-301 (no new abstraction) — the fix must reuse the branch shape
                `CommitRectangle`/`CommitPolylineDraft` (TASK-107) already established.
- Acceptance:   CIRCLE, ARC, and ELLIPSE drawn while a paper layout is active commit to that
                layout's `paperCircles`/`paperArcs`/`paperEllipses` stores; the model
                `userCirclesCxCyZR`/`userArcs`/`userEllipses` stores are unchanged; switching back
                to Model space does not surface the geometry there.
- Owning subsystem: Commands (`CommitCircle`, `CommitArcThreePoints`, `FinishEllipseFromRatio` own
                the respective commits).

## 2. Bug report (issue #86, filed by nrjohnson2604 2026-08-25)

| # | Observed | Expected |
|---|----------|----------|
| 1 | `CIRCLE` drawn with a paper Layout tab active (status bar reads PAPER) appears on the paper canvas at draw time, then reappears in **model space** the moment the user switches to the Model tab | the circle stays paper geometry, like LINE/TEXT/RECT/POLYLINE already do |
| 2 | Same for `ARC` (three-point) and `ELLIPSE` (centre/major-axis/ratio) | both stay paper geometry |
| 3 | Control case: `POLYLINE`/`RECT` in the same layout stay correctly in paper space (TASK-107 / REQ-053) | CIRCLE/ARC/ELLIPSE should match |

Reproducer: `tests/headless/transcripts/regression-86-paper-circle-arc-ellipse-routing.txt` (new).

## 3. Root cause (evidence, not hypothesis)

`src/commands/CadCommands.cpp`:
- `CommitCircle` (:4543) — unconditionally wrote `st.userCirclesCxCyZR`/`st.userCircleAttrs`.
- `CommitArcThreePoints` (:7584) — unconditionally wrote `st.userArcs`/`st.userArcAttrs`.
- `FinishEllipseFromRatio` (:20412) — unconditionally wrote `st.userEllipses`/`st.userEllAttrs`.

None had an `ActivePaperGeometryTarget(st)` branch anywhere in its body — confirmed by inspection
against the two functions that already do this correctly, `CommitRectangle` (:15220) and
`CommitPolylineDraft` (:20444, fixed for issue #84 by TASK-107). All four functions build closely
related storage shapes and are commit-time decision points for their respective draw commands; the
absence of the branch in these three, versus its presence in the other two, is the entire defect.

`PaperLayout` (`src/commands/PaperSpace.hpp:159`) already carries `paperCircles`/`paperCircleAttrs`,
`paperArcs`/`paperArcAttrs`, and `paperEllipses`/`paperEllAttrs` — added under REQ-038/ADR-013 "full
primitive store... mirrors the model arrays exactly so clipboard paste can route model↔paper", and
already read by `PaperSpace.hpp`'s bounds/hit-test helpers for selection and box-select (REQ-039
Acceptance (1), already covered by `PaperSpaceTests`). So this is confirmed a **routing fix**, not a
data-model addition — the storage existed and was already exercised by selection code; only the
create-time commit functions never wrote to it.

## 4. Architectural boundary check (workflow.md §4)

- New abstraction / layer / dependency / ownership change / global / public API / data-format
  change?
    - [x] **No — proceed.** Three `if (PaperLayout* L = ActivePaperGeometryTarget(st))` branches,
      the exact shape `CommitRectangle`/`CommitPolylineDraft` already use. `CadArc`/`CadEllipse` are
      reused as-is (their `z` field is documented "Always 0 in paper space (ADR-025 (g))" — no
      change needed there, just don't call `CadCommitElevation` on the paper path).

## 5. Assumptions

None beyond what's stated in §3 — the paper stores already existed and were already typed
identically to the model stores, so no ambiguity surfaced.

## 6. Plan

- `src/commands/CadCommands.cpp` — give `CommitCircle`, `CommitArcThreePoints`, and
  `FinishEllipseFromRatio` each the `ActivePaperGeometryTarget` branch, writing to the layout's
  paper store (paper branch) vs. the existing model store (else branch), matching
  `CommitPolylineDraft`'s structure exactly. `arc.z`/`ell.z` stay at their zero-initialized default
  on the paper path instead of taking `CadCommitElevation(st)`.
- `tests/headless/HeadlessDriver.cpp` — add `EXPECT PAPERCIRCLES <n>` / `PAPERARCS <n>` /
  `PAPERELLIPSES <n>`, summed across `run.st.paperLayouts`, alongside the existing
  `PAPERPOLYLINES` TASK-107 added.
- New transcript `regression-86-paper-circle-arc-ellipse-routing.txt`: switch to a new paper
  layout, draw one CIRCLE (typed centre/radius), one ARC (three `CLICK`s — ARC has no typed-X,Y
  command-line path, confirmed by reading `ProcessCommandLineSubmit`, which has no `K::Arc` branch),
  and one ELLIPSE (`CLICK` centre + major-axis point, typed ratio); assert `CIRCLES`/`ARCS`/
  `ELLIPSES` (model) stay 0 and `PAPERCIRCLES`/`PAPERARCS`/`PAPERELLIPSES` go to 1 after each, and
  that switching back to Model doesn't change either set.

## 7. Steps
- [x] sibling audit (§3) — confirmed `PaperLayout` already has the three paper stores, typed
  identically to the model stores
- [x] regression transcript written, proven to fail against the unfixed binary
- [x] the fix
- [x] full suite

## 8. Implementation log

- 2026-08-25 — read `CommitCircle`, `CommitArcThreePoints`, `FinishEllipseFromRatio`, their working
  siblings `CommitRectangle`/`CommitPolylineDraft`, and `PaperLayout`'s field list end to end before
  touching anything. Confirmed the paper circle/arc/ellipse stores already exist (REQ-038/ADR-013)
  and are already read by `PaperSpace.hpp`'s selection/bounds helpers — this is a routing fix, not a
  data-model addition, settling the issue's own open question.
- 2026-08-25 — wrote `regression-86-paper-circle-arc-ellipse-routing.txt` and the driver's three
  `EXPECT PAPER*` additions. Confirmed ARC has no typed-coordinate command-line path (no `K::Arc`
  branch in `ProcessCommandLineSubmit`) by reading the function; used `CLICK` for its three points
  instead, matching every existing ARC transcript.
- 2026-08-25 — built `gosurvey_headless` against the unfixed code (`git stash` on
  `CadCommands.cpp` only) and ran the new transcript: **fails** — `EXPECT CIRCLES 0` at step 8,
  "got 1" (geometry landed in the model store, matching the issue's prediction).
- 2026-08-25 — restored the fix, rebuilt. New transcript **passes** (33 steps). First run without
  an `ESC` between the CIRCLE commit and the next `CMD ARC` failed for an unrelated reason (CIRCLE
  stays active for repeat-drawing after a commit, same as the GUI) — added the `ESC`, not a defect.
- 2026-08-25 — re-ran `cmake` configure (the transcript glob and two stale test names from prior
  renames needed a fresh configure to pick up) and the full suite via `ctest`: **594/594** active
  tests pass, 1 pre-existing `Disabled` (`headless.dxf-export-stable`, held open for issue #63,
  unrelated). `GoSurvey` (full GUI) and `GoSurveyTests` also rebuilt clean in the same pass — no new
  warnings from any changed file.

## 9. Self-verification

- [x] build-project       — PASS. `gosurvey_headless`, `GoSurveyTests`, and `GoSurvey` all clean;
      no new warnings introduced by either changed file.
- [x] architecture-review — PASS. Three branches reusing `ActivePaperGeometryTarget`, the exact
      pattern `CommitRectangle`/`CommitPolylineDraft` already establish. No new abstraction, layer,
      dependency, global, public API, or data-format change; `CadArc`/`CadEllipse` reused as-is.
- [x] code-review         — PASS. Fixes the root cause (the missing branch) in all three sites, not
      a symptom; mirrors the sibling pattern exactly rather than inventing a new shape.
- [x] dependency-audit    — PASS. None added.
- [x] performance-review  — n/a. Same commit, one extra branch already paid for by
      `ActivePaperGeometryTarget`'s existing use elsewhere in the same function family.
- [x] testing             — PASS. New transcript proven red-before/green-after; full suite green
      (541 unit + 53 headless incl. the new one, 1 pre-existing disabled).

## 10. Verification result

- Submitted: 2026-08-25
- Verdict:   **PASS**
- Findings:  none outstanding.

## 11. Outcome

COMPLETION REPORT — TASK-110 — 2026-08-25
- Requirements satisfied:  REQ-039 (Acceptance (5)/(6) met for CIRCLE/ARC/ELLIPSE: yes)
- Summary:                 `CommitCircle`, `CommitArcThreePoints`, and `FinishEllipseFromRatio`
                           had no paper-space branch and always wrote the model stores. Gave each
                           the same `ActivePaperGeometryTarget` branch `CommitRectangle`/
                           `CommitPolylineDraft` already have, so each commits to the active
                           layout's `paperCircles`/`paperArcs`/`paperEllipses` store instead. This
                           closes out the last three of REQ-039 Statement (e)'s seven draw commands
                           — LINE/TEXT/MTEXT/RECT/POLYLINE were already correct.
- Tests:                   `headless.regression-86-paper-circle-arc-ellipse-routing` (new), proven
                           to fail against the unfixed binary. Full suite: 541/541 unit,
                           53/53 headless (+1 pre-existing disabled for unrelated issue #63).
- Verification verdict:    PASS (findings resolved: none)
- Assumptions:             none
- Architectural decisions: none made by Workshop.
- Dependencies:            none added
- Technical debt noted:    the issue's documentation problem — REQ-039's traceability row (line
                           3923 of `spec/requirements.md`) claims "draw/modify parity" was verified
                           by manual test, when four of seven draw commands (now fixed: POLYLINE by
                           TASK-107, CIRCLE/ARC/ELLIPSE here) had no code path at all when that row
                           was written — is **not** corrected here. The issue explicitly asks that
                           this be called out rather than folded into the code fix, since a spec/
                           traceability change needs its own recorded decision (CLAUDE.md's
                           escalation rule). Filed as a follow-up: the traceability row should be
                           re-verified by manual test now that all seven commands have a code path,
                           and the row updated (or reasserted) by a recorded decision in
                           `spec/project.md`, not silently.
- Build:                   reproducible, clean on target platform (MSVC, ninja, vcvars64)
- Docs updated:            this log; `tests/headless/HeadlessDriver.cpp` inline comments
- Done:                    2026-08-25
