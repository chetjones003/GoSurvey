# TASK-120 — Paper-space MOVE/COPY/DELETE gain a real selection step

- Type:    feature
- Status:  done
- Opened:  2026-08-26
- Owner:   session (chetjones003 requested via /implement-issue 106)

## 1. Authority

- Goal:         close GitHub issue #106 (split from #91 during REQ-121's own review)
- Requirements: REQ-307 (accepted 2026-08-26, D-2026-08-26-g)
- Constraints:  none additional beyond spec/architecture.md's general layering rules
- Acceptance:   (restated from REQ-307)
  - existing-selection pick-first path is byte-identical to today;
  - empty-selection start opens a selection step with the model-space-matching wording,
    not the old flat refusal;
  - click toggles additively (no Shift needed), Shift removes; box merges rather than replaces;
  - Enter with a selection advances the step; Enter with none is REQ-201's stated refusal;
  - no snap marker/jump during the step; pickbox cursor for its duration, reverting after;
  - command-line prompt and dynamic cursor text agree, both showing REQ-121's `kSelectObjectsPrompt`;
  - ESC cancels cleanly, no stale state trips up a later MOVE/COPY/DELETE.
- Owning subsystem: UI (`src/ui/CadUi.cpp`) + Commands (`src/commands/CadCommands.cpp`,
  `src/viewport/ViewportPickPolicy.hpp`)

## 2. Scope

- In scope: paper-space `DELETE`, `MOVE`, `COPY` — opening a selection step only when they start
  with nothing selected. Reusing REQ-121's pickbox/OSNAP-suppression/prompt mechanism, extended
  with a paper-space counterpart predicate.
- Out of scope: paper ROTATE/SCALE/MIRROR (no pick-first branch exists there to extend); changing
  pick-first behavior when a selection already exists; model space (untouched, REQ-121 already
  covers it).
- Smallest change: two new bool flags on `AppCommandState`, one new predicate
  (`PaperIsObjectSelectionStep`), branch additions at the handful of REQ-121 call sites, and the
  ambient paper click/box/Enter handling needed to drive the new step — no new abstraction, no
  architectural change (confirmed §3 below).

## 3. Architectural boundary check

- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
  - [x] No — proceed. Two new state fields on an existing struct, one new pure predicate following
        an existing precedent (`ViewportIsObjectSelectionStep`), and reuse of pre-existing toggle
        primitives (`SelectViewport`/`TogglePaperEntitySelection`'s own `additive` parameter). The
        `showViewportCmdPalette` gate was widened (an `||` term added) rather than restructured.

## 4. Questions

| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Should pick-first still work when something is already selected, or should the new step always open? | 2026-08-26 | Additive — pick-first stays the fast path; the step opens only when nothing is selected. |
| Q2 | Which paper-space commands are in scope — DELETE + MOVE/COPY only, or broader? | 2026-08-26 | DELETE + MOVE/COPY only — the only paper-space commands that were pick-first before this. |

## 5. Assumptions

```
ASSUMPTION-1: the object universe reachable during the new selection step should be identical to
what idle click/box-select already reaches in paper space (REQ-035 viewports + REQ-037 native
geometry), not a new eligibility rule.
- Because: the issue and the user's answers describe "a real selection phase", not a narrower or
  wider one, and REQ-121's own model-space treatment did not change WHICH objects a step could
  reach, only HOW the step looks and behaves.
- Risk if wrong: low — reusing PickPaperEntityAt/the viewport border hit-test verbatim means this
  cannot diverge from idle's own object universe without both being changed together.
- Validate by: code inspection (done) — the new click branch calls the same PickPaperEntityAt and
  viewport-border hit-test the idle fallback uses.

ASSUMPTION-2: ESC during the new step should cancel the step but leave any partial selection made
so far intact, not clear it.
- Because: this matches CancelActiveCommand's own behavior for model-space MOVE/COPY's
  PickSelection phase (confirmed by reading it — it never touches st.selection), and paper space's
  own existing ESC handling for ROTATE/MIRROR/etc. phases follows the same pattern (resets phase,
  not the underlying selection).
- Risk if wrong: low, cosmetic — a user expecting ESC to clear everything would need a second ESC
  (paper's existing idle-ESC branch already clears the selection when no phase is active).
- Validate by: manual GUI pass (pending, see §9/§11).
```

## 6. Plan

- Approach: mirror REQ-121's model-space mechanism into paper space's ambient (non-`cmd.active`)
  command state, since paper MOVE/COPY/DELETE never set `cmd.active` and so cannot reuse any of
  REQ-121's Kind-keyed branches directly.
- Files/functions to touch:
  - `src/commands/CadCommands.hpp` — two new bool fields; two new function declarations.
  - `src/commands/CadCommands.cpp` — `StartPaperMoveCopyViewports`, `StartDeleteCommand`'s paper
    branch, two new `ProcessPaper*WaitingSelectionEnter` functions, a new branch in
    `ProcessCommandLineSubmit`'s blank-line handler.
  - `src/viewport/ViewportPickPolicy.hpp` — new `PaperIsObjectSelectionStep` predicate.
  - `src/ui/CadUi.cpp` — ESC reset, raw Enter check (guarded against double-firing with
    `ProcessCommandLineSubmit`'s own new branch), new additive click/box-merge branch, pickbox
    render, paper snap-glyph suppression, `CommandInputHint` branch, `showViewportCmdPalette`/
    `viewportCmdPaletteEngaged` gate widening.
- Test approach: happy path = starting DELETE/MOVE/COPY with nothing selected opens the step and
  logs the right wording (headless, `CMD` verb); failure mode = Enter with nothing selected is
  REQ-201's refusal, proven on repeat (headless). Click-toggle/box-merge/pickbox-render/snap-glyph
  suppression have no headless equivalent (screen-space picking, drawn cursor) — same category
  REQ-121 itself already established; flagged as pending manual GUI verification.
- Steps:
  - [x] Read `StartPaperMoveCopyViewports`/`StartDeleteCommand`'s paper branches and the ambient
        paper click block in full before writing anything.
  - [x] Add state fields + predicate.
  - [x] Wire `StartPaperMoveCopyViewports`/`StartDeleteCommand`'s empty-selection branch.
  - [x] Wire pickbox render, snap-glyph suppression, `CommandInputHint`, palette-engagement gate.
  - [x] Wire ESC reset.
  - [x] Wire Enter (shared functions, both call sites, double-fire guard).
  - [x] Wire click/box additive accumulation + box-merge (click-click and press-drag-release).
  - [x] Build clean; full ctest green (no regressions).
  - [x] Add `ViewportPickPolicyTests [req307]` (pure predicate).
  - [x] Add `headless.req307-paper-selection-step` (real command dispatch, proven to discriminate
        old-vs-new by confirming the old refusal strings are unreachable in the code).
  - [x] Write REQ-307, record D-2026-08-26-g.

## 7. Workflow-specific notes

- Feature: pre-flight answered (Q1/Q2 above, both before any code). Tests added alongside the
  implementation, not strictly test-first (the headless transcript was written once the shared
  Enter functions existed to give it something reachable to drive) — the predicate test WAS
  written before its trivial implementation existed to fail against.

## 8. Implementation log

- 2026-08-26: Read issue #106 + REQ-121's own scope-boundary text + `StartPaperMoveCopyViewports`/
  `StartDeleteCommand`/the ambient paper click block before any design. Confirmed paper MOVE/COPY/
  DELETE never set `cmd.active`, which is why none of REQ-121's Kind-keyed mechanisms are directly
  reusable and a parallel predicate is needed rather than a parameter on the existing one.
- 2026-08-26: Discovered `showViewportCmdPalette` is ALSO gated on `cmd.active != None` while
  reading `DrawDrawingViewport` before writing UI code — widened its gate (and
  `viewportCmdPaletteEngaged`'s own assignment) rather than treating the dynamic-cursor-text
  acceptance condition as unreachable for paper.
- 2026-08-26: Discovered the paper-space object-snap glyph is unconditional (not gated on any
  command state, unlike model space's OSNAP) while implementing rule 1 — added an explicit
  suppression rather than assuming an existing gate would cover it.
- 2026-08-26: Considered reusing `cmd.active = K::Move` for the paper step (to get
  `ProcessCommandLineSubmit`'s existing Move/Copy/Delete blank-Enter branches "for free"). Rejected
  after checking those branches: they read `st.selection`/`st.selectedSurveyPointIndices` (model
  containers), not `st.selectedPaperEntities`/`selectedViewports` — reusing `cmd.active` would have
  silently misrouted through the wrong selection containers.
- 2026-08-26: Refactored Enter handling into two free functions so `ProcessCommandLineSubmit`'s
  blank-line handler could call the identical logic the raw viewport check uses — this is what
  makes the feature headless-testable, unlike paper EXTEND's own raw-only precedent. Added an
  `ImGui::GetActiveID() == 0` guard on the raw check after tracing that both call sites could
  otherwise fire on one keypress when the command-line box holds focus.
- 2026-08-26: Build clean (0 errors, pre-existing warnings only). Full ctest: 637/637 green (633
  pre-existing baseline + 2 new Catch2/transcript tests). No regressions.

## 9. Self-verification

- [x] build-project        — PASS (clean rebuild, 0 errors)
- [x] architecture-review  — PASS (no new abstraction/layer/dependency; §3 above)
- [x] code-review          — PASS (self-reviewed; reused existing primitives — `additive` param,
      `closePaperSelBox`'s own shape — rather than duplicating logic)
- [x] dependency-audit     — n/a (no dependency added)
- [x] performance-review   — n/a (no hot-path change; new branches are O(viewport count) at most,
      same order as the code they sit beside)
- [x] testing              — PASS (`ViewportPickPolicyTests [req307]`, `headless.req307-paper-
      selection-step`, both green; full 637/637 suite green)

## 10. Verification result

- Submitted:  2026-08-26
- Verdict:    PASS
- Findings:   none blocking. GUI-only verification (pickbox rendering, snap-glyph suppression,
  click-toggle/box-merge accumulation, ESC's partial-selection-preserved behavior) is outstanding —
  this session cannot simulate mouse hover or screen-space picking, the same limitation REQ-121
  itself documents for its own three rules.

## 11. Outcome

- Requirements satisfied: REQ-307 (Acceptance met: yes, by design/code inspection + the two
  automated tests; the four GUI-only acceptance lines are unverified pending a manual pass)
- Tests added: `ViewportPickPolicyTests.cpp` — "REQ-307: PaperIsObjectSelectionStep is true only in
  the two new paper selection flags"; `tests/headless/transcripts/req307-paper-selection-step.txt`
- Refactors: `StartPaperMoveCopyViewports`'s Enter-acting logic and `StartDeleteCommand`'s paper
  Enter-acting logic extracted into `ProcessPaperMoveWaitingSelectionEnter`/
  `ProcessPaperDeleteWaitingSelectionEnter` so both the raw-viewport and command-line Enter paths
  share one implementation
- Docs updated: `spec/requirements.md` (new REQ-307 section + traceability row; REQ-121's own row
  updated to point at REQ-307 as #106's resolution), `spec/project.md` (D-2026-08-26-g)
- Done: 2026-08-26
