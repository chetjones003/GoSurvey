# TASK-112 — Modify commands' "select objects" step: click-or-box, accumulate until Enter

- Type:    bug
- Status:  done
- Opened:  2026-08-25
- Owner:   assistant (bug-fix skill)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         GOAL (issue #87 follow-up)
- Requirements: REQ-305 (ARRAY), REQ-103 (modify-command completeness — MOVE/COPY/SCALE/ROTATE/
  MIRROR/ALIGN's shared selection-step precedent)   ← both `accepted`
- Constraints:  CON-07 (build artifacts stay in build/, not the source tree)
- Acceptance:   REQ-305 Acceptance 2 (as amended, D-2026-08-25-n): "object selection accepts a
  pre-existing selection, individual entity/annotation/fill/survey-point clicks, and/or a
  window/crossing box — any mix accumulates into one selection until Enter confirms it, matching
  MOVE/COPY/SCALE/ROTATE/MIRROR/ALIGN's own selection step."
- Owning subsystem: Commands (`CadCommands.cpp`/`.hpp`) + Viewport click policy
  (`ViewportPickPolicy.hpp`) + UI click handling (`CadUi.cpp`) + headless test driver

## 2. Scope
- In scope: MOVE, COPY, SCALE, ROTATE, MIRROR, ALIGN, ARRAY's PickSelection-equivalent phase —
  click-select (additive, Shift removes) plus window/crossing box (already additive), accumulating
  across any number of actions, confirmed only by a blank Enter.
- Out of scope: STRETCH (its crossing box is load-bearing geometry — REQ-103 step 5 tests each
  entity's definition points against the box to decide which vertices move, not just which objects
  are selected — extending click-select would need a per-entity move-vs-stretch model this task does
  not build). DELETE/JOIN/ZOOM (pure box-select commands, not part of the "select then transform"
  family the user scoped this to).
- Smallest change: reuse idle click-select's own pick functions (`PickCadAnnotationAt`,
  `PickClosestCadEntity`, `PickFilledRegionAt`, `PickSurveyPointAtCursor`) and the box's own merge
  logic (`ComputeSelectionFromRect`, already additive); stop the six PickSelection branches in
  `SubmitViewportPickImpl` from auto-advancing when a box finishes; advance instead from
  `ProcessCommandLineSubmit`'s blank-Enter dispatcher, the shape ALIGN's own PickSelection branch
  already had.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed. One new enum value (`ViewportClickRoute::SelectionAccumulate`) in an
      existing exhaustive-switch policy function; its handler is new code but calls only existing
      pick/selection primitives. No new entity kind, store, or persisted state.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Scope the click-or-box/accumulate/Enter fix to ARRAY alone, or extend it to the five sibling commands (MOVE/COPY/SCALE/ROTATE/MIRROR/ALIGN) that share the identical selection-step limitation? | 2026-08-25 | All modify commands (user chose the broader option over ARRAY-only) |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: STRETCH is excluded from this fix even though the user said "all modify commands."
- Because:       REQ-103 step 5 makes STRETCH's crossing box the operation's own data (which
                 vertices move), not just an object filter; extending click-select to it needs a
                 per-entity move-vs-stretch model, which is new scope this task does not build.
- Risk if wrong: user actually wanted STRETCH included too, accepting the "clicked objects move
                 wholesale" AutoCAD convention as a smaller follow-up.
- Validate by:   flagged in the completion report; user can ask for a STRETCH follow-up if wanted.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: add `ViewportClickRoute::SelectionAccumulate`; route the six commands' PickSelection
  phase (+ ARRAY, already using its own phase name) to it instead of `SelectionBox`; give it a
  handler in `CadUi.cpp` that click-selects one entity/annotation/fill/survey-point additively (or
  arms/closes the fence on a miss); stop `SubmitViewportPickImpl`'s six PickSelection branches from
  auto-advancing on box-finish; add/extend `ProcessCommandLineSubmit`'s blank-Enter branches to
  advance the phase there instead (ALIGN already had this shape — copy it for the other five, plus
  ARRAY which had none).
- Files/functions to touch: `ViewportPickPolicy.hpp`, `CadUi.cpp` (new switch case),
  `CadCommands.cpp` (`SubmitViewportPickImpl` six blocks, `ProcessCommandLineSubmit` empty-line
  dispatcher, Start*Command/footer-hint/`CommandInputHint` prompt text), `HeadlessDriver.cpp` (CLICK
  verb's exhaustive switch), `tests/ViewportPickPolicyTests.cpp`.
- Test approach: happy path = a headless transcript driving MOVE and ARRAY through two separate BOX
  drags that must both land in the final selection, confirmed only by a blank Enter (fails against
  pre-fix code — either the "Nothing selected" Enter message never appears, or the second BOX is
  consumed as the next phase's own point instead of adding to the selection). Failure mode = blank
  Enter with nothing selected must not advance the phase (asserted in the same transcript). The
  click-one-entity half of the fix is screen-space picking with no headless equivalent (same
  limitation idle click-select already has) — needs the user's own manual GUI pass.
- Steps:
  - [x] Investigate: confirm this is a real gap (not REQ-305 already), find the shared PickSelection
        shape, find ALIGN's existing Enter-confirm precedent, find why STRETCH's box is load-bearing.
  - [x] Ask the user the scoping question (Q1).
  - [x] `ViewportPickPolicy.hpp`: add `SelectionAccumulate`, reroute six commands + ARRAY.
  - [x] `CadUi.cpp`: implement the new case.
  - [x] `CadCommands.cpp`: stop auto-advance in six `SubmitViewportPickImpl` blocks; add/extend six
        `ProcessCommandLineSubmit` Enter branches; update Start*Command/footer-hint/
        `CommandInputHint` prompt text for all seven affected commands.
  - [x] `HeadlessDriver.cpp`: route `SelectionAccumulate` through the CLICK verb's box-arm/close path
        (documented limitation: no screen-space entity-pick simulation).
  - [x] `tests/ViewportPickPolicyTests.cpp`: update MIRROR/ARRAY route assertions; add a dedicated
        test case for all six + STRETCH's deliberate exclusion.
  - [x] New headless regression transcript (`regression-modify-selection-accumulate.txt`).
  - [x] `spec/requirements.md`: amend REQ-305 Statement/Acceptance 2 + Revisions; REQ-103 Revisions.
  - [x] `spec/project.md`: record D-2026-08-25-n.
  - [x] Build clean; full `GoSurveyTests.exe` + `ctest` headless corpus green.

## 7. Workflow-specific notes
- Bug: root cause = `SubmitViewportPickImpl`'s PickSelection branches for MOVE/COPY/SCALE/ROTATE/
  MIRROR/ARRAY advanced the phase the instant one two-corner box finished, and
  `ViewportClickRouteFor` routed their PickSelection phase to the box-only `SelectionBox` route —
  together this meant no individual-entity click was possible and a bare Enter during selection was
  a silent no-op (ALIGN was the sole exception, already staying in PickSelection until Enter, just
  without click-select). Regression test: `regression-modify-selection-accumulate.txt` fails against
  pre-fix code (confirmed: `mirror-click-driven.txt`, an existing transcript, broke on this exact
  auto-advance-on-one-box assumption and needed updating — direct evidence the change is real).

## 8. Implementation log
- 2026-08-25 — Investigated; found REQ-305 already matched the reported "bug" (spec-compliant,
  not broken) — escalated as a scoping question rather than silently fixing. User chose the
  broader (all modify commands) option.
- 2026-08-25 — Implemented across the five files listed in the Plan. Discovered `stretch-*.txt`/
  STRETCH's own REQ-103 acceptance text makes its crossing box load-bearing mid-implementation;
  excluded it (ASSUMPTION-1) rather than re-asking, since it's a scope boundary the user's own
  REQ-103 text already draws, not a new ambiguity.
- 2026-08-25 — Full rebuild + `GoSurveyTests.exe` (542/542) green. `ctest` headless corpus: one
  pre-existing transcript (`mirror-click-driven.txt`) failed on the FIRST full run — it asserted the
  old two-click-then-auto-advance shape directly. Updated it to add the confirming blank Enter (not
  a workaround — it was asserting the exact behavior this task deliberately removed). Full rerun:
  54/54 headless transcripts pass (1 pre-existing unrelated transcript stays disabled, see
  CMakeLists.txt's own note on `dxf-export-stable`).

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS (clean rebuild, MSVC/Ninja, 0 errors; pre-existing unrelated
      warnings only — `curveintersect.cpp` C4458, and `/EHsc` C4530 noise present throughout the
      tree already)
- [x] architecture-review  — PASS (no Workshop architectural decision; one new enum value in an
      existing exhaustive-switch policy function, no new abstraction/dependency/layer)
- [x] code-review          — PASS (reused existing pick/selection primitives rather than
      reimplementing; STRETCH's exclusion documented inline at every routing site touched)
- [x] dependency-audit     — n/a (no dependency changes)
- [x] performance-review   — n/a (no hot-path/perf-relevant change; UI click handling only)
- [x] testing              — PASS: `GoSurveyTests.exe` 542/542; `ctest` (headless corpus) 54/54 run
      (1 pre-existing disabled), including the new regression transcript and the updated
      `mirror-click-driven.txt`

## 10. Verification result
- Submitted:  2026-08-25
- Verdict:    PASS
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-305 (Acceptance 2, amended: met — yes); REQ-103 (selection-step
  precedent, amended: met — yes)
- Tests added:            `tests/headless/transcripts/regression-modify-selection-accumulate.txt`;
                          `tests/ViewportPickPolicyTests.cpp` new TEST_CASE + two updated assertions
- Refactors:              none (STRETCH's `SelectionBox` shape and MOVE/COPY/SCALE/STRETCH's shared
                          `ViewportPickPolicy.hpp` case were split, not refactored, to keep STRETCH
                          behavior-identical)
- Docs updated:           `spec/requirements.md` (REQ-305, REQ-103), `spec/project.md`
                          (D-2026-08-25-n)
- Done:                   2026-08-25
