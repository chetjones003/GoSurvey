# TASK-089 — Survey point rollover readout

- Type:    feature
- Status:  implemented — pending user visual check
- Opened:  2026-08-23
- Owner:   Workshop

## 1. Authority
- Goal:         GOAL-01 (a working surveyor's CAD)
- Requirements: REQ-090 (accepted 2026-08-23, D-2026-08-23-b). Reads REQ-089 (the dwell, the
                suppression rules and the panel it reuses), REQ-084 (d) (isolation), REQ-101.
- Constraints:  CON-07
- Acceptance:   verbatim from REQ-090:
  - resting on a point marker for the dwell period shows the readout; moving the cursor hides it
    immediately and re-arms the dwell;
  - northing and easting equal what the Properties panel shows for the same point, in a drawing
    whose `worldDocumentOrigin` is NON-ZERO;
  - elevation equals the point's stored elevation at `surveyPointDisplayPrecision`;
  - the number shown is `SurveyPoint::id`;
  - resting on a point that also lies inside a surface shows the point's readout and only that;
  - no readout appears while a command is active, while a gesture is in progress, or in paper space.
- Owning subsystem: UI. Unlike TASK-088 there is **no Commands-layer work at all** — see §2.

## 2. Scope
- In scope:     a level (non-one-shot) dwell signal, the point-vs-surface precedence, the five-row
                readout, and its tests.
- Out of scope: a general per-entity rollover (still declined, D-2026-08-23-a (4)); any change to
                how survey points are picked or drawn — including the layer-visibility gap in Q1,
                which is a separate defect if it is one.
- Smallest change: this is mostly *subtraction* relative to TASK-088. The hit test already exists
                and already runs every frame (`PickSurveyPointAtCursor` →
                `cmd.viewportHoverSurveyPointIndex`, `CadUi.cpp:8981`), so there is no query to run
                once, nothing to latch, and no new state beyond what the dwell already carries.
                Format the five strings inline each frame and draw them.

### Why this one does not latch, and TASK-088 did
REQ-089 latches formatted text because its query walks every triangle of every visible surface and
must be kept off the per-frame path (REQ-100 profile (c)). None of that applies here: the pick runs
every frame regardless, for hover feedback that already ships, and formatting five strings is
nothing. Not latching is also *safer* — `viewportHoverSurveyPointIndex` is an **index**, and
`surveyPoints` compacts on erase, so latching it across frames would be architecture §11.9's
blocking finding. Reading it in the same frame it was computed sidesteps §11.9 entirely, exactly as
`SurfaceCoverage::surfaceIndex` does inside its own call.

**Resist the symmetry.** Copying TASK-088's latch "for consistency" would import a §11.9 hazard to
solve a performance problem this feature does not have.

## 3. Architectural boundary check
- [x] **No — proceed.**
  - new global mutable state (§11.3): **no** — no new state at all; the dwell timer already exists
    on `AppCommandState` (`surfaceHoverDwell`), and this shares it.
  - upward dependency (§11.1): **no** — UI-only.
  - index across an object boundary (§11.9): **no**, and deliberately so — see §2 above.
  - new abstraction (§11.4): **no**. `HoverDwellTick` gains one field (§6 step 1); a field on an
    existing plain result struct is not an abstraction, and it has two present-day readers the day
    it lands.
  - dependency / data format / public API: **none**.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | **Should the readout respect layer visibility and REQ-084 (d) isolation — and does survey point *picking* respect them today?** `PickSurveyPointIndex(cmd.surveyPoints, …)` takes only the points array: it consults neither `drawingLayerTable` nor `hiddenEntityIds`, so an invisible point may already be hoverable and selectable. REQ-090 states no acceptance condition about this on purpose. | 2026-08-23 | **Confirmed by user: use the existing pick as-is.** Verified `PickSurveyPointIndex`/`PickSurveyPointAtCursor` (`CadUi.cpp:4312`, `:4371`) consult neither `drawingLayerTable` nor `hiddenEntityIds` — same gap the hover highlight already has. Readout builds on whatever `viewportHoverSurveyPointIndex` returns; the pre-existing visibility gap in picking is a separate bug, out of scope here. |
| Q2 | Does the panel keep REQ-089's "Tin Surface" heading style — i.e. a heading naming the object kind, here "Survey Point"? | 2026-08-23 | **Confirmed by user: yes.** Panel shows a "Survey Point" heading above the five rows, matching `DrawSurfaceRolloverReadout`'s "Tin Surface" heading. |

## 5. Assumptions

ASSUMPTION-1: A point marker and a surface are the only two things that can be under the cursor for
              this panel, so precedence is a two-way rule.
- Because:       REQ-090 decides point-over-surface; no third readout exists.
- Risk if wrong: a later readout kind would need the rule restated as an ordering rather than an
                 if/else.
- Validate by:   code review — keep the precedence as a single visible `if/else` at one site, so a
                 third kind is a compile-time-obvious edit rather than a search.

## 6. Plan
- Approach: give the dwell a **level** signal beside its existing edge signal, then branch one
  readout on what is under the cursor.
- Files/functions to touch:
  - `src/util/hoverdwell.hpp` — add `HoverDwellTick::settled` (true on every frame the cursor has
    rested at least `dwellSeconds`, independent of `armed`). `elapsed` stays exactly as it is: the
    surface query still needs a one-shot, and this must not quietly become a level signal.
  - `tests/HoverDwellTests.cpp` — `settled` is level where `elapsed` is an edge; `settled` is false
    before the threshold and through a move; the two agree on the frame `elapsed` fires.
  - `src/ui/CadUi.cpp` — at the REQ-089 dwell block (~`CadUi.cpp:9117`): when
    `viewportHoverSurveyPointIndex >= 0` and `settled`, format and show the point readout and skip
    the surface path; otherwise the existing surface path is unchanged.
    `DrawSurfaceRolloverReadout` generalises to take the rows to draw, or gains a sibling — whichever
    keeps the "one tooltip, one `BeginTooltip`" shape, since two `BeginTooltip` calls in a frame is
    the failure mode to avoid.
  - Coordinates: `CadCoord::WorldXFromLocal` / `WorldYFromLocal` with
    `cmd.surveyPointDisplayPrecision`, elevation raw — the exact pattern at `CadUi.cpp:4606-4617`.
- Test approach:
  - happy path = `settled` is true on every frame at and after the threshold;
  - failure modes = `settled` is false before it and immediately after a move; `elapsed` still fires
    exactly once per rest with `settled` present (i.e. adding the level signal did not turn the
    one-shot into a per-frame query — the REQ-089 condition this could silently break).
  - The world-coordinate condition cannot be reached by the unit target (no `CadCommands.cpp`, no
    window). Cover it by **reading the same drawing twice**: assert the readout's formatting helper
    against `WorldXFromLocal` on a state-plane-magnitude origin if the helper can be made pure;
    otherwise state plainly that it is verified by eye against the Properties panel, and say so in
    the completion report rather than implying coverage.
- Steps:
  - [x] resolve Q1 and Q2
  - [x] `settled` + tests (test-first, as TASK-088 did)
  - [x] the readout branch and the point rows
  - [x] build, run tests
  - [ ] visual check — **see the note below before spending time on it**

## 7. Workflow-specific notes
- Feature: pre-flight is Q1/Q2; the dwell change is written test-first.
- **Do not repeat TASK-088's visual-verification attempt.** FINDING-1 there established that this
  session type cannot produce a hovered frame at all: synthetic mouse input never reaches ImGui,
  because the GLFW backend only feeds a mouse position while the window reports `GLFW_FOCUSED`.
  The cheap control is to hover a ribbon button first — if it does not highlight, stop and hand the
  visual check to the user rather than iterating. TASK-088 spent a substantial part of a session
  discovering this; the whole value of writing it down is not paying for it twice.

## 8. Implementation log
- 2026-08-23 opened from the user's request, immediately after TASK-088 shipped. REQ-090 accepted
  and recorded (D-2026-08-23-b). Status `plan` — not started.
- 2026-08-23 Q1/Q2 confirmed by user (both per recommendation; see §4).
- 2026-08-23 implemented:
  - `src/util/hoverdwell.hpp`: `HoverDwellTick` gains `settled` (level — true on every frame the rest
    has reached `dwellSeconds`, computed independently of `armed`). `elapsed` unchanged.
  - `tests/HoverDwellTests.cpp`: three new cases — `settled` is level vs. `elapsed`'s edge; `settled`
    is false before the threshold and immediately after a move; the two agree on the firing frame.
    Written before the CadUi.cpp change (test-first, per TASK-088's precedent).
  - `src/ui/CadUi.cpp`: `DrawSurveyPointRolloverReadout(cmd, ix)` — a sibling to
    `DrawSurfaceRolloverReadout`, formatting the five rows (Number/Layer/Northing/Easting/Elevation)
    fresh every call (no latch — see its doc comment for why that's correct here, not a shortcut).
    At the REQ-089 dwell block: `BuildSurfaceHoverRows` still runs unconditionally on `tick.elapsed`
    (see FINDING-1, §10, for why it must not be skipped there); `onPoint`, read fresh every frame from
    `viewportHoverSurveyPointIndex`, decides only which readout is DRAWN — the point readout on a
    settled point-hover frame, the (already-latched) surface readout otherwise. No new
    `AppCommandState` field — reuses `surfaceHoverDwell` and the pre-existing
    `viewportHoverSurveyPointIndex`, per §3.
- 2026-08-23 build: `GoSurvey` and `GoSurveyTests` both clean (MSVC 14.50, vcvars64 x64). Full suite:
  214453 assertions / 503 cases, all green (`[hover]` subset: 91 assertions / 11 cases).
- 2026-08-23 code-review (medium) run against the diff — found FINDING-1 (below), fixed. See §10.
- 2026-08-23 visual check: not attempted in this session. TASK-088's FINDING-1 established synthetic
  mouse input cannot produce a hovered ImGui frame here (the GLFW backend needs `GLFW_FOCUSED`, which
  this session type cannot obtain); repeating that attempt would only re-spend the time already paid
  down. Handed to the user per §7.

## 9. Self-verification
- [x] build-project — `GoSurvey` + `GoSurveyTests`, MSVC 14.50 x64, clean
- [x] architecture-review — no new abstraction, no new state, no upward dependency, no §11.9 index
      latch (see §3; unchanged by the FINDING-1 fix)
- [x] code-review — medium, forked agent; one finding, fixed (§10)
- [x] dependency-audit — n/a, no dependency added
- [x] performance-review — n/a as expected: the one-shot triangle walk is unconditional either way
      (FINDING-1's fix made it MORE unconditional, not less); the point readout's five-string format
      runs only on settled frames, same order of cost as the surface readout's tooltip draw
- [x] testing — `HoverDwellTests` extended (3 cases), full suite green (214453 assertions / 503 cases)

## 10. Verification result
- Submitted: 2026-08-23, code-review (medium, forked agent) against the working diff.
- FINDING-1 (correctness, CONFIRMED, fixed): gating `BuildSurfaceHoverRows` on `tick.elapsed && !onPoint`
  meant that if a survey point was under the cursor on the exact frame the dwell elapsed, but drifted
  outside the point's pick aperture on a later frame by less than `kSurfaceRolloverMoveTolPx` (so the
  dwell was never reset), the readout went blank for the rest of that rest: the point branch stopped
  matching (`onPoint` false) and the surface branch had nothing latched (`elapsed` cannot fire twice).
  Fixed by always running `BuildSurfaceHoverRows` on `tick.elapsed` unconditionally, and deciding
  point-vs-surface precedence only at draw time from a freshly-read `onPoint` each frame
  (`CadUi.cpp` ~9172-9199). Re-verified: build clean, full suite green (214453 assertions / 503 cases).
- Verdict: **PASS** (after fix).

## 11. Completion report

```
COMPLETION REPORT — TASK-089 — 2026-08-23
- Requirements satisfied:  REQ-090 (Acceptance met: yes, by code inspection — see below for the one
                            condition not exercisable by the unit target)
- Summary:                 Survey point rollover readout. Resting on a point marker shows Number,
                            Layer, Northing, Easting, Elevation beside the cursor; a point takes
                            precedence over an overlapping surface's REQ-089 readout.
- Tests:                   HoverDwellTests — 3 new cases for HoverDwellTick::settled (level vs.
                            elapsed's edge; false before threshold and immediately after a move; the
                            two agree on the firing frame). Full suite: 214453 assertions / 503 cases,
                            green. The world-coordinate acceptance condition (northing/easting equal
                            the Properties panel's on a non-zero worldDocumentOrigin) is not
                            exercisable by the unit target — no CadCommands.cpp/CadCoordinateFrame.cpp
                            link there (plan §6) — and is NOT independently covered by an automated
                            test; DrawSurveyPointRolloverReadout uses the identical call
                            (WorldXFromLocal/WorldYFromLocal + FormatLinear at
                            surveyPointDisplayPrecision) as the Properties panel does at CadUi.cpp
                            ~4483-4499, which is the strongest guarantee available short of a GUI test.
                            This is stated plainly rather than implied as covered, per plan §6.
- Verification verdict:    PASS (findings resolved: FINDING-1 — surface rows could go permanently
                            unbuilt for a rest if a point briefly aliased the elapsed frame; fixed by
                            building them unconditionally on elapsed and deciding precedence only at
                            draw time)
- Assumptions:              ASSUMPTION-1 (point-vs-surface is a two-way precedence, §5) — not
                            invalidated; the fix does not add a third readout kind.
- Architectural decisions: none made by Workshop (escalated: none)
- Dependencies:             none added
- Technical debt noted:     none new. Pre-existing gap surfaced but NOT fixed here, by design (Q1):
                            PickSurveyPointIndex/PickSurveyPointAtCursor (CadUi.cpp:4312, :4371)
                            consult neither drawingLayerTable nor hiddenEntityIds, so an invisible or
                            isolated-out survey point is still hoverable/pickable — and now also
                            still produces this readout. This is a defect in survey point picking, not
                            in the readout, per the user's confirmed Q1 answer; it needs its own bug
                            report, not a fix folded into TASK-089.
- Build:                    reproducible; GoSurvey.exe and GoSurveyTests.exe both link clean, no new
                            warnings on touched lines
- Docs updated:             workshop/tasks/TASK-089-req090-survey-point-rollover-readout.md (this
                            file) — Q1/Q2 answers, implementation log, verification result

Outstanding: visual confirmation in a running GUI session — this session cannot produce a hovered
ImGui frame (TASK-088 FINDING-1: synthetic mouse input never reaches a GLFW_FOCUSED window). Please
verify by eye: hover a survey point's cross marker for ~0.5s, confirm the "Survey Point" tooltip shows
Number/Layer/Northing/Easting/Elevation matching the Properties panel for the same point (ideally on a
drawing with a non-zero worldDocumentOrigin), that it disappears on cursor movement, and that hovering
a point sitting inside a surface shows only the point's readout.
```
