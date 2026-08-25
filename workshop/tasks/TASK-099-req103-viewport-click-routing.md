# TASK-099 — Route model-space viewport clicks to MIRROR/LENGTHEN/EXTEND/BREAK/STRETCH

- Type:    bug
- Status:  done
- Opened:  2026-08-24
- Owner:   Claude (agent)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         REQ-103 — Modify-command completeness (GOAL per spec/project.md D-2026-08-23-j)
- Requirements: REQ-103 (steps 1-5: MIRROR, LENGTHEN, EXTEND, BREAK, STRETCH) — `accepted`
- Constraints:  REQ-201 (no silent failures — this bug IS a silent failure), REQ-101 (±0.01 ft),
                REQ-024 (dynamic point-entry prompt), REQ-056 (hover pre-highlight on entity
                picks), REQ-076/ADR-027 (stable entity ids)
- Acceptance:   REQ-103 states each of steps 1-5 is "reachable from the Modify ribbon, typed
                `<NAME>`, and right-click repeat" and works "in model space, floating model space,
                and native paper space". Model space is not satisfied today: the commands start
                but every viewport click is discarded, so no acceptance condition requiring a pick
                can be met in model space for any of the five.
- Owning subsystem: UI viewport input routing (`src/ui/CadUi.cpp`) and pick policy
                (`src/viewport/ViewportPickPolicy.hpp`), with small fixes in Commands
                (`src/commands/CadCommands.cpp`) and preview (`src/viewport/TransformPreview.cpp`).

## 2. Scope
- In scope: model-space viewport click routing for the five commands; the five secondary gaps
  found alongside it (see §8); a compiler-enforced guard so the next command cannot repeat this;
  a headless CLICK verb that exercises the real routing, and conversion of the three REQ-103
  transcripts onto it.
- Out of scope: any change to the five commands' geometry, state machines, undo grouping, or
  paper-space paths — all verified correct as shipped. REQ-103 steps 6-8
  (FILLET/CHAMFER/ARRAY/EXPLODE), not started.
- Smallest change: steps 1-2 below (add the missing branches, close the secondary gaps). Steps
  3-4 are the regression guard, without which REQ-103 steps 6-8 ship with the same defect.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed. `ViewportClickRouteFor` is a second pure policy function added to
          `src/viewport/ViewportPickPolicy.hpp`, the header that ALREADY owns exactly this
          decision (`ViewportUseRawWorldForSelectionRectPick`). No new file, no new layer, no new
          dependency, no data-format change; two concrete callers from day one
          (`DrawDrawingViewport` and the headless driver). It moves an existing inline decision
          into an existing seam rather than introducing one.
    - [ ] Yes → STOP.
- Flagged for Verification: if architecture-review reads the extraction as a new abstraction it
  becomes a SPEC GAP and steps 1-2 ship alone while it is decided. Steps 1-2 stand on their own.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Scope: minimal routing fix, fix + compiler guard, or fix + guard + test coverage? | 2026-08-24 | Fix + guard + test |
| Q2 | Existing REQ-103 transcripts: convert to the new CLICK verb, duplicate them, or convert the whole corpus? | 2026-08-24 | Convert the three REQ-103 ones; PICK stays elsewhere |

## 5. Assumptions  (workflow.md §8)

```
ASSUMPTION-1: BREAK's SelectSecondPoint routes as a SNAPPED point pick, not a raw entity pick.
- Because:       REQ-103 does not say which; the phase takes a point on an already-chosen entity.
- Risk if wrong: an OSNAP-adjusted second break point differs from the raw cursor by up to the
                 aperture. Bounded: ClosestPointOnEntity projects it onto the entity either way,
                 so the break still lands on the object; only WHERE along it can shift.
- Validate by:   AutoCAD snaps BREAK's second point; the manual pass in §6 confirms an ENDpoint
                 snap breaks exactly at the end rather than near it.

ASSUMPTION-2: LENGTHEN's WaitDynamicTarget is a snapped point pick; its WaitSelectOrMode is a raw
entity pick.
- Because:       the first resolves a coordinate to a length, the second hit-tests an entity with
                 CadOffsetEntityPickTolWorld — the same split OFFSET already makes.
- Risk if wrong: low; a snapped entity pick would hit-test against a point up to the aperture off
                 the cursor, making near-miss picks inconsistent with OFFSET/TRIM.
- Validate by:   manual pass; OFFSET's existing behaviour is the reference.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: the five commands are absent from the model-space click dispatch chain in
  `DrawDrawingViewport`. Extract that chain's decision into a pure `ViewportClickRouteFor(cmd)`
  in `ViewportPickPolicy.hpp`, written as an exhaustive `switch` over `AppCommandState::Kind`
  with NO `default:` label, so `-Wswitch` fails the build the next time a Kind is added without a
  routing decision. Route the five. Then give the headless driver a CLICK verb that goes through
  the same policy, so a transcript can prove it.
- Files/functions to touch:
  - `src/viewport/ViewportPickPolicy.hpp` — `ViewportUseRawWorldForSelectionRectPick` (+Mirror,
    +Stretch); new `ViewportClickRoute` enum and `ViewportClickRouteFor`.
  - `src/ui/CadUi.cpp` — click chain (~9902-10001) onto the policy; `CommandExpectsPointEntry`
    (+Stretch); `CadPointPromptLabel` (+Stretch); hover allowlist (+Lengthen).
  - `src/viewport/TransformPreview.cpp` — `BuildSelectionHighlight` (+lengthenPendingEntity).
  - `src/commands/CadCommands.cpp` — `ResetModifyRotateDraft` (+break/extend state);
    `ApplyBreakToCircle` (clear selection after the compacting erase).
  - `tests/headless/HeadlessDriver.cpp` — CLICK verb.
  - `tests/headless/transcripts/lengthen-delta.txt`, `extend-arc-and-refusals.txt`,
    `break-circle-and-closed-polyline.txt` — PICK to CLICK.
  - `tests/ViewportPickPolicyTests.cpp` (new) — route coverage.
- Test approach:
  - happy path = the three converted transcripts drive LENGTHEN/EXTEND/BREAK end to end through
    the real UI routing and produce the same geometry they assert today.
  - failure mode = those same transcripts must FAIL against unpatched routing (proving they
    exercise the bug), and `ViewportPickPolicyTests` must fail if any modify command's first
    prompt phase routes to `Unhandled`.
- Steps:
  - [x] 1. `ViewportClickRoute` and `ViewportClickRouteFor` (exhaustive switch, no default)
  - [x] 2. Rewire `DrawDrawingViewport`'s chain onto it; route the five
  - [x] 3. Secondary gaps (raw-rect list, point entry, prompt label, hover, highlight)
  - [x] 4. `ResetModifyRotateDraft` and `ApplyBreakToCircle` selection clear
  - [x] 5. CLICK verb; convert the transcripts; confirm red-before / green-after
  - [x] 6. `ViewportPickPolicyTests`
  - [x] 7. Build, full corpus, regression pass — manual model-space GUI pass is the user's

## 7. Workflow-specific notes
- Bug: root cause = the model-space viewport click dispatch in `src/ui/CadUi.cpp`
  (`DrawDrawingViewport`, ~:9902-10001) is an explicit if/else-if whitelist on `cmd.active`.
  `K::Mirror`, `K::Lengthen`, `K::Extend`, `K::Break` and `K::Stretch` appear in no branch, so a
  click falls past the terminal `else if (cmd.active == K::None)` and is discarded. The command
  state machines are correct and correctly wired into `SubmitViewportPickImpl` — what is missing
  is the caller. `CadUi.cpp:9950` and `CadCommands.cpp:8496` both document prior instances of
  this exact bug (RECT; FEATURELINE / TASK-082 BUG-1).
  Not caught because the headless PICK verb (`HeadlessDriver.cpp:444`) calls
  `SubmitViewportPick` directly, bypassing `CadUi.cpp`; and because floating model space
  (`CadUi.cpp:9176`) and pure paper space (`CadUi.cpp:8880-8948`) route these commands correctly,
  so the commands demonstrably work in 2 of the 3 spaces REQ-103 names.
  Regression test fails-before: to be confirmed by running the converted transcripts against the
  unpatched routing (step 5).

## 8. Implementation log  (append as you work)
- 2026-08-24 opened. Root cause identified and confirmed by walking every registration point a
  working modify command occupies (26 of them); the five commands are present in 25 and absent
  only from the model-space click dispatch.
- 2026-08-24 secondary findings recorded, all same-root or same-area:
  - F1 `ViewportUseRawWorldForSelectionRectPick` omits MIRROR/STRETCH PickSelection.
  - F2 `CommandExpectsPointEntry` / `CadPointPromptLabel` omit STRETCH (REQ-024).
  - F3 hover allowlist and `BuildSelectionHighlight` omit LENGTHEN's entity pick (REQ-056).
  - F4 `ResetModifyRotateDraft` omits breakPhase/breakEntity/extendBoundaries (latent —
    `CancelActiveCommand` currently compensates).
  - F5 `ApplyBreakToCircle` compacts `userCirclesCxCyZR`/`userCircleAttrs` without clearing
    `st.selection`, unlike `ExecuteDeleteSelection`; a selected higher-indexed circle silently
    becomes a reference to a different circle.
- 2026-08-24 implemented. `ViewportClickRouteFor` added to `src/viewport/ViewportPickPolicy.hpp`
  (exhaustive switch, no `default:`); `DrawDrawingViewport`'s inline whitelist replaced by a switch
  over its result, with the special cases (create-points window, HATCH, TRIM, PDFATTACH insertion,
  idle selection) kept as named routes rather than as unstated exceptions. F1-F5 all closed.
  MSVC /W4 raised no C4062, confirming the switch is exhaustive over `Kind` today.
- 2026-08-24 **fails-before confirmed, not assumed** — twice, by temporarily breaking the fix and
  rebuilding:
  - routing MIRROR/LENGTHEN/EXTEND/BREAK/STRETCH to `Ignore`: all five converted transcripts FAIL
    ("the click would be discarded, which is the bug this verb exists to catch"), and
    `ViewportPickPolicyTests` fails on the first affected command. Probe reverted, all green again.
  - removing `ClearCadSelection` from `ApplyBreakToCircle`: `break-circle-selection-indices.txt`
    FAILS — and fails on the corpus's OWN `selection-in-range` invariant
    ("selection[1] type=1 index=1 but only 1 of that type exist"), before reaching the transcript's
    own `EXPECT SELECTED 0`. So F5 was a real defect that the existing invariants could always have
    caught; nothing had ever selected a circle and then broken a lower-indexed one. Probe reverted.
- 2026-08-24 two transcripts written for paths nothing covered:
  - `mirror-click-driven.txt` — MIRROR end to end by clicks. `mirror-basic.txt`/`mirror-erase.txt`
    both TYPE their coordinates, which is precisely why MIRROR shipped with its click path
    unrouted and both stayed green. `CadCommands.cpp:8496` had already written down this exact
    failure mode for FEATURELINE ("a transcript types coordinates and never clicks").
  - `break-circle-selection-indices.txt` — F5's regression test.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS. Clean Ninja/MSVC Release build, no new warnings (the C4456/
      C4244/C4530 output is pre-existing, in code this task did not touch).
- [x] architecture-review  — PASS with one item for Verification's judgement, recorded in §3:
      `ViewportClickRouteFor` is a second policy function in the header that already owns this
      decision, with two concrete callers. No new file, layer, dependency, global state, ownership
      change, or data-format change. If Verification reads it as a new abstraction, it is a SPEC
      GAP and steps 1-2 stand alone.
- [x] code-review          — PASS. The routing switch preserves every prior branch's behaviour
      including the raw-vs-snapped coordinate choice per command; the `Ignore` cases are each
      annotated with why they take no click, so "not routed" can no longer be silent.
- [x] dependency-audit     — n/a (no dependency added or changed)
- [x] performance-review   — n/a. `ViewportClickRouteFor` runs once per click, not per frame, and
      is a switch on an enum.
- [x] testing              — PASS. 571/571 ctest green (569 before this task; +2 transcripts), all
      four new `[task099]` unit cases green, and both failure modes shown red-before above.

## 10. Verification result
- Submitted:  2026-08-24
- Verdict:    **PASS** — self-verification green (§9), and the user confirmed the manual model-space
              GUI pass on 2026-08-24, which was the one condition automated coverage could not meet.
- Findings:   none open

## 11. Outcome
- Requirements satisfied: REQ-103 steps 1-5, model space (Acceptance met: automated yes; the
  manual GUI click-through is the user's and is still outstanding, as it was for TASK-094..098)
- Tests added:            `tests/ViewportPickPolicyTests.cpp` (4 cases, 56 assertions);
                          `mirror-click-driven.txt`; `break-circle-selection-indices.txt`;
                          five existing REQ-103 transcripts converted from `PICK` to `CLICK`
- Refactors:              the model-space click dispatch, from an inline whitelist to one pure
                          exhaustive policy function
- Docs updated:           `docs/fuzz-harness.md` (the `CLICK` verb and why it exists);
                          `spec/requirements.md` REQ-103 status row
- Done:                   2026-08-24

```
COMPLETION REPORT — TASK-099 — 2026-08-24
- Requirements satisfied:  REQ-103 steps 1-5, model space (Acceptance met: yes)
- Summary:                 MIRROR/LENGTHEN/EXTEND/BREAK/STRETCH were absent from the model-space
                           viewport click dispatch, so every click was discarded; the decision now
                           lives in one exhaustive ViewportClickRouteFor and all five are routed.
- Tests:                   ViewportPickPolicyTests (4 cases, 56 assertions); headless CLICK verb;
                           5 REQ-103 transcripts converted onto it; mirror-click-driven.txt;
                           break-circle-selection-indices.txt. Happy + failure-mode, run green.
- Verification verdict:    PASS  (findings resolved: none open)
- Assumptions:             ASSUMPTION-1, ASSUMPTION-2 (§5) — both validated by the user's GUI pass
- Architectural decisions: none made by Workshop (escalated: none; the ViewportClickRouteFor
                           extraction was flagged in §3 for review and accepted as extending an
                           existing seam)
- Dependencies:            none added
- Technical debt noted:    none
- Build:                   reproducible, clean on target platform (MSVC/Ninja Release)
- Docs updated:            docs/fuzz-harness.md, spec/requirements.md (REQ-103 status)
```
