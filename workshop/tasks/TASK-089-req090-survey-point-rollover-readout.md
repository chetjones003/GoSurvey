# TASK-089 — Survey point rollover readout

- Type:    feature
- Status:  plan
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
| Q1 | **Should the readout respect layer visibility and REQ-084 (d) isolation — and does survey point *picking* respect them today?** `PickSurveyPointIndex(cmd.surveyPoints, …)` takes only the points array: it consults neither `drawingLayerTable` nor `hiddenEntityIds`, so an invisible point may already be hoverable and selectable. REQ-090 states no acceptance condition about this on purpose. | open | **Resolve before implementing.** If picking already ignores visibility, that is a pre-existing defect in survey point picking, not in this readout — file it as its own bug and build the readout on whatever the pick returns, rather than adding a visibility filter here that the surrounding hover highlight does not apply. A readout that disagreed with the highlight beside it would be the worse outcome. |
| Q2 | Does the panel keep REQ-089's "Tin Surface" heading style — i.e. a heading naming the object kind, here "Survey Point"? | open | Recommend **yes**: it is what makes a two-kind readout self-explanatory, and it costs one string. |

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
  - [ ] resolve Q1 and Q2
  - [ ] `settled` + tests (test-first, as TASK-088 did)
  - [ ] the readout branch and the point rows
  - [ ] build, run tests
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

## 9. Self-verification
- [ ] build-project
- [ ] architecture-review
- [ ] code-review
- [ ] dependency-audit
- [ ] performance-review  — expected n-a: no new per-frame work beyond a comparison
- [ ] testing

## 10. Verification result
- Submitted:
