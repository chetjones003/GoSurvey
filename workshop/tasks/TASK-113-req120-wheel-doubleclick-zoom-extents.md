# TASK-113 — Middle double-click zooms to extents (REQ-120)

- Type:    feature
- Status:  done
- Opened:  2026-08-25
- Owner:   Nathan Johnson

## 1. Authority
- Requirements: **REQ-120** — accepted 2026-08-25 by **D-2026-08-25-o**.
- Also honoured: REQ-045 (middle-drag pan must keep working, unchanged), REQ-201 (a refusal
  states its reason), REQ-061 (floating model space has its own camera).
- Acceptance: REQ-120's seven conditions.
- Owning subsystem: `UI` (the gesture). The extents computation and the camera write are existing
  `Commands` code; the only Commands change is the space branch REQ-120 specifies.

## 2. Scope
- In scope: the middle double-click gesture; the space branch (model / floating model → model
  extents, paper → sheet rect).
- Out of scope:
  - a second extents sweep over the seven paper stores, so paper geometry outside the sheet is
    not framed (REQ-120 states this);
  - giving the headless harness a synthetic viewport so zoom becomes transcript-testable (DEBT-1);
  - ZOOM PREVIOUS / view history — that is REQ-106, still `proposed`.
- Smallest change: one `IsMouseDoubleClicked` guard, one space branch.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No**. The flag-then-deferred-consumer pattern already exists and already has a
          non-command caller (DXF import). The space test reuses `ActivePaperGeometryTarget`'s own
          rule rather than inventing a second notion of "which space am I in". No new field, no
          new function, no signature change.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Transparent mid-command, or refused like the typed command? | 2026-08-25 | **Transparent.** Matches AutoCAD; needing to reframe mid-command is when the gesture is wanted. Implemented by setting the flag directly, so the typed command keeps its guard. |
| Q2 | Which spaces, and what does paper frame? | 2026-08-25 | **All three**, mirroring middle-drag pan. Paper frames the **sheet**. |

## 5. Assumptions
```
ASSUMPTION-1: Zooming mid-command cannot corrupt the active command.
- Because:       the gesture bypasses StartZoomExtentsCommand's active-command guard.
- Risk if wrong: a reframe mid-LINE loses the placed point, or leaves a draft in a bad state.
- Validate by:   ProcessPendingViewportZoom writes ONLY st.viewportPanX/Y/Zoom (plus the out-params
                 and the GPU cache bump). It touches no command phase and no draft store — read and
                 confirmed. Verified in the GUI: a LINE with one point placed survives the zoom and
                 still expects its next point.
```

## 6. Plan
- `src/ui/CadUi.cpp`, `DrawDrawingViewport` — `hovered && IsMouseDoubleClicked(_Middle)` sets
  `pendingZoomExtents` (and clears `pendingZoomWindow`, which would otherwise win the same frame).
  Placed **above** the orbit/pan drag handling, which is untouched.
- `src/commands/CadCommands.cpp`, `ProcessPendingViewportZoom` — pick the extents source by space.
- Test approach: **GUI, because headless cannot reach this** (§9 DEBT-1). Before/after screenshots
  of the framing; the mid-command survival check; the typed command's refusal still intact.

- Steps:
  - [x] 1. Space branch in `ProcessPendingViewportZoom`.
  - [x] 2. Gesture in `DrawDrawingViewport`.
  - [x] 3. GUI verification.

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1/Q2). Recon before scoping is what kept this small — it found
  the trigger/worker split and the existing non-command caller, so no mechanism had to be built.

## 8. Implementation log
- 2026-08-25 Recon (recorded in D-2026-08-25-o) established this is a reuse, not a new mechanism.
- 2026-08-25 Recon **corrected itself once**: "works in all three spaces" looked like pure reuse
  because middle-drag pan works in all three, but pan is camera math that does not care what is in
  the scene, whereas extents must know. `ComputeRobustWorldExtents` sweeps **model stores only**
  and no paper-extents helper exists anywhere. Framing the **sheet** — via the `sheetWidthIn()` /
  `sheetHeightIn()` accessors that already exist — keeps paper space nearly free and is what
  REQ-120 specifies; a second sweep over the seven paper stores was declined.

## 9. Self-verification
- [x] build-project        — PASS (clean)
- [x] architecture-review  — PASS (no invariant touched; no new type or field)
- [x] code-review          — PASS (two edits, both small; the space branch mirrors an existing rule rather than inventing one)
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a (one `IsMouseDoubleClicked` per frame)
- [x] testing              — GUI only; see DEBT-1 for why nothing else is possible

## 10. Verification result
- Submitted:  2026-08-26
- Verdict:    PASS
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-120 (all seven acceptance conditions — met; the seventh was corrected
                          during the pass rather than merely checked, see §13); REQ-045 (middle-drag
                          pan unchanged — met); REQ-061 (floating model space frames the model — met)
- Tests added:            none, and none are reachable — no zoom behaviour can be covered by the
                          headless harness (DEBT-1). Full suite green on the implementation commit
                          `98edace`: **601/601 ctest**, 1 pre-existing disabled
                          (`headless.dxf-export-stable`, unrelated — GitHub #63). Behaviour verified
                          by the manual GUI pass recorded in §13, and re-tested by the user on
                          2026-08-26 with no findings.
- Refactors:              none
- Docs updated:           `spec/requirements.md` (REQ-120 added; its acceptance text corrected by
                          what the GUI pass found — §13), `spec/project.md` (D-2026-08-25-o)
- Done:                   2026-08-26

## 12. Technical debt
```
DEBT-1: No automated coverage is possible for any zoom behaviour.
- What:      the headless driver models no framebuffer and never calls ProcessPendingViewportZoom,
             which early-returns on fbW <= 0. There is no zoom transcript anywhere in the corpus,
             for this reason — REQ-120 is not the first to hit it.
- Forced by: covering it means giving the harness a synthetic viewport with a framebuffer size, so
             the deferred consumer runs. That is harness work, not feature work, and it would need
             its own decision about what a headless "viewport" is.
- Cost:      every zoom behaviour — this gesture, ZOOMEXTENTS, ZOOM WINDOW — is manual-only.
- Remove by: a REQ-203 harness extension adding a synthetic viewport + a ZOOM verb; then this
             gesture, the typed commands and the space branch all become transcript-testable.
- Follow-up: not filed; raise it if a third zoom requirement lands.
```

## 13. GUI verification — 2026-08-25
Driven against the real window; every result read off a screenshot.

- **Frames the drawing.** Two lines at `0,0–50,50` and `400,300–450,350`, view zoomed in so only
  one was visible. Middle double-click framed both, logging
  `Zoom extents applied — span 450 x 350` — exactly the drawing's extents (0→450, 0→350).
- **Transparent mid-command (ASSUMPTION-1).** With `LINE` active and one point placed at `10,10`,
  the double-click zoomed **and the command survived**: the log still shows the point was set, the
  prompt is still `Next: click; X, Y; @dx,dy; [A]zimuth, [2P];`, and the rubber-band draft is still
  drawn from the placed point. This is the condition with no other coverage, and it holds.
- **Typed route unchanged — and the REQ was corrected here.** Acceptance originally claimed typed
  `ZOOMEXTENTS` refuses mid-command "with its existing message". It does not: while a command is
  active the text is consumed by that command, so it is read as point input and refused with
  `Could not parse point. Use X,Y or X Y; @dx,dy; A / 2P`. `StartZoomExtentsCommand`'s guard is
  never reached on that path. The guarantee that matters — the typed route does not zoom
  mid-command — holds; the REQ text now says what actually happens.
