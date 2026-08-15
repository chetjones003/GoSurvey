# TASK-047 — Survey point defects in 3D: label elevation, hover highlight, selection box, pick space

- Type:    bug
- Status:  implement
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority
- Requirements: **REQ-057** (every entity carries Z), **REQ-058** (orbitable 3D view; markers and
  overlays stay on the geometry they belong to), **REQ-023** (survey point labels)
- Constraints:  REQ-101 (±0.01 ft), REQ-201, architecture §11.4 (no new abstraction)
- Acceptance (the conditions these defects violate):
  - REQ-057: "every entity gains Z" — a survey label is a `CadAnnotation`, which has `insZ`.
  - REQ-058: an overlay must stay on the geometry it belongs to when the view is orbited. The
    TASK-036/037 sweep established this for markers, snap glyphs and per-entity elevation; these
    three sites were missed by it.
- Owning subsystem: Domain/survey (label anchor), UI (hover + selection overlays), Renderer (removal)

## 2. Bug report (as reported, 2026-08-15)
| # | Observed | Expected |
|---|----------|----------|
| 1 | Survey point labels stay at elevation 0 when the view is orbited; they detach from their points. | A label sits at its point's elevation and stays with it under orbit. |
| 2 | Hovering a survey point draws a blue ring around it. | The point's own X marker highlights blue; no ring. |
| 3 | The box-selection rectangle skews when the view is orbited. | It stays screen-facing, looking the same in 3D as in 2D. |

## 3. Root causes (evidence, not hypothesis)

**#1 — `insZ` is never assigned.** `RepositionSurveyLabelMtextForPoint`
(`src/survey/SurveyPoints.cpp`) sets `insX`, `insY`, `boxMinX/Y`, `boxMaxX/Y` — and no `insZ`.
`CadAnnotation::insZ` defaults to `0.f` and is **absolute** (ADR-025 D2), so every survey label is
pinned to the datum. The draw path is not at fault: it already honours `insZ` at 14 sites in
`CadUi.cpp` (10205, 10368-10369, 10568-10569, 10580, 10606-10607, 10617, 10640, …). One missing
assignment, one symptom.

**#2 — the hover overlay draws a ring by construction.** `CadUi.cpp:10731`:
`dlHov->AddCircle(cH, std::max(rPxH, 4.f), IM_COL32(100, 215, 255, 230), 48, 2.25f)`. Nothing is
wrong with it — it is simply not what is wanted. This is a **design change, not a defect**, and is
recorded as such.

**#3 — the drawn box is world geometry; the selection region is screen-space.** The renderer builds
the rectangle from four **world** corners on the XY plane at z ≈ 0.035
(`AppendWorldRectFillTris` / `AppendWorldRectOutline`, `ViewportRenderer.cpp:1753-1778`), so an
orbited camera projects it as a parallelogram lying on the datum.

Meanwhile the hit test (`CadCommands.cpp`, box-select) does this:
```cpp
if (proj) {  // the drag corners arrive in world coords; move them to screen too
  SP(xa, ya, 0.f, &xa, &ya);
  SP(xb, yb, 0.f, &xb, &yb);
}
float mnX = std::min(xa, xb); ...   // axis-aligned SCREEN rectangle
```
— it projects **two** corners and forms an axis-aligned **screen** rectangle. So in an orbited view
the drawn box and the region that actually selects are **different shapes**. This is not cosmetic:
the box misrepresents what it will select. In plan view the two coincide, which is why it was never
visible before REQ-058.

The correct shape already exists in this codebase: the floating-viewport path (`CadUi.cpp:10048`)
projects both corners and draws an axis-aligned ImGui rect — screen-facing by construction. Model
space is the odd one out.

**#4 — the pick test has no Z term at all** (reported 2026-08-15 after the first three landed, with a
screenshot: cursor on one point, a *different* point ~340 px above it highlighted).
`PickSurveyPointIndex` (`CadUi.cpp`) compares the cursor's world position against each point's
easting/northing and nothing else:
```cpp
const double dx = wx - static_cast<double>(pts[i].easting);
const double dy = wy - static_cast<double>(pts[i].northing);   // no elevation term
```
`wx/wy` are the cursor **ray hit on the work plane at Z = 0**, while the marker is **drawn** at the
point's elevation (REQ-057). So the test picks whichever point's *plan* position lies under the
cursor — which, on this 33 ft-relief surface in an orbited view, is a point hundreds of pixels away
from the one visibly under the crosshair. Exact in plan, which is why it survived REQ-058.

This is the same family as #1 and #3: not a wrong Z literal but a **missing dimension**, invisible
in the default view. Three call sites shared it — the hover and both click paths — so hover and
click were at least consistently wrong.

**#5 — a selected survey point had no marker highlight at all** (reported 2026-08-15, after #4 was
confirmed fixed). `BuildSelectionHighlight` (`TransformPreview.cpp:502`) walks `cmd.selection` only.
Survey points are not in it — they live in `cmd.selectedSurveyPointIndices` — so nothing ever
highlighted their marker.

What made this confusing rather than obviously missing: selecting a point *does* light something
up. `SyncSurveyPointLinkedMtextSelection` puts the point's linked **label** into `cmd.selection`, and
the label is a `CadAnnotation`, which `BuildSelectionHighlight` does handle. So the label highlighted
and the point it belongs to did not — which reads as "selection is drawn in the wrong place" rather
than "the marker was never included".

## 4. Regression risks
- **Plan view must look identical.** #3 changes how the box is drawn for every user; plan view is
  the default and the overwhelmingly common case. Colours are carried over from the GL path exactly
  so a plan-view drag is pixel-equivalent.
- **Label geometry must not move in plan.** #1 sets Z only; `insX/insY` and the box are untouched, so
  plan view, picking, and the PDF plot are unaffected.
- Removing the renderer's selection-rect block must not disturb any other draw stage.

## 5. Assumptions
```
ASSUMPTION-1: A survey label's elevation is its POINT's elevation.
- Because:       REQ-023 does not state a label elevation; the label is an attached annotation and
                 the reported expectation is that it stays with the point under orbit.
- Risk if wrong: a user wanting labels on a constant plane would disagree. Low — that would be a
                 label-style option, not a default.
- Validate by:   user's manual check on samples/surface-demo.gs (the CP points are labelled).

ASSUMPTION-2: Window/crossing COLOUR CODING is not introduced to model space by this fix.
- Because:       the floating-viewport path colour-codes (blue window / green crossing) and model
                 space does not. Reusing its exact styling would silently change model-space
                 appearance beyond the reported bug — scope the user did not ask for.
- Risk if wrong: model space keeps a single blue for both modes, which is the status quo.
- Validate by:   raised to the user in the report as an observed inconsistency, their call.
```

## 6. Plan
- `src/survey/SurveyPoints.cpp` — assign `a.insZ = p.elevation` in `RepositionSurveyLabelMtextForPoint`.
- `src/ui/CadUi.cpp` — replace the hover ring with a blue X built from
  `AppendSurveyPointCrossVertices` in the camera basis (the same function that builds the real
  marker, so highlight and marker cannot drift apart); draw the model-space selection box as a
  screen-space ImGui rect from the two projected corners.
- `src/app/main.cpp` — stop passing `selectionFillRect`.
- `src/render/ViewportRenderer.{hpp,cpp}` — remove the selection-rect stage, its parameter, and the
  now-unused `AppendWorldRectFillTris` / `AppendWorldRectOutline`.

## 7. Testing note — stated, not papered over
None of these three is reachable by `GoSurveyTests`: the label anchor lives behind
`AppCommandState` and the other two are ImGui/GL draw stages, neither of which the test target can
link (**DEBT-7**, inherited from TASK-044/045). Writing a test that exercised something adjacent and
called it a regression test would be worse than admitting the gap. Verification is by build plus the
user's manual check against `samples/surface-demo.gs`, whose CP points are labelled precisely so
label behaviour is visible.

## 8. Implementation log
- 2026-08-15 — opened; three root causes identified with evidence before any edit.
- 2026-08-15 — **#1** one line: `a.insZ = p.elevation`. **No migration needed** — `LoadGoSurveyFile`
  and startup both call `RepositionAllSurveyPointLabels`, which runs the same function per point, so
  every existing drawing corrects itself on open.
- 2026-08-15 — **#2** the highlight is built by `AppendSurveyPointCrossVertices` in the camera basis
  — the same function that builds the drawn marker. Re-deriving the X shape locally is how a
  highlight ends up a different size or angle from the thing it highlights; this cannot drift.
- 2026-08-15 — **#3** drawn screen-aligned in `CadUi` from the two projected drag corners at Z = 0,
  matching the hit test's own `SP(xa, ya, 0.f, …)` exactly, so the box now *is* the region that
  selects. Colours carried over verbatim from the removed GL stage, so plan view is unchanged.
  Removed as dead: the renderer's selection-rect stage, its `selectionFillRect` parameter, the
  `kLwFence` constant, and `AppendWorldRectFillTris` / `AppendWorldRectOutline` (43 lines) — they had
  no other caller.
- 2026-08-15 — **#4** `PickSurveyPointIndex` gains an optional `SurveyPickScreen`: when the view is
  not plan, each candidate is projected **at its own elevation** and compared to the cursor in
  pixels, using the same `max(markerHalf, aperture) * 1.38` rule as the world path so the feel is
  unchanged. The plan path is untouched, byte for byte, for REQ-058 parity.
  All three call sites now go through one `PickSurveyPointAtCursor` wrapper rather than each
  deciding for itself. That is the point: a fix applied to hover alone would have left clicks on the
  plan test, so hovering would highlight one point while clicking selected another — worse than the
  original bug, and the kind of thing that looks like a random glitch instead of a missed site.
- 2026-08-15 — **#5** hover and selection now share one `highlightMarker` closure over the same
  camera basis and the same `AppendSurveyPointCrossVertices` call, so a highlight cannot differ in
  size or angle from the marker it highlights, and the two states cannot drift from each other.
  **Selection yellow is the renderer's own `highlightLines` colour** (1.00/0.92/0.15) rather than a
  new one — a selected point should read as selected in the language the rest of the app already
  speaks. Selection takes precedence over hover, matching `BuildHoverHighlight`'s rule for entities,
  so a selected point does not flicker to hover blue as the cursor crosses it.
  Drawn in `CadUi` rather than added to `BuildSelectionHighlight` because the X is **billboarded** —
  its shape depends on the camera, and that function takes only the command state. Same reason the
  markers themselves are built with a basis in `main.cpp`.
- 2026-08-15 — **Scope held.** The floating-viewport path colour-codes window (blue) vs crossing
  (green); model space uses one blue for both. Reusing that styling was tempting since it was right
  there, but it is a visible change nobody asked for, so model-space colours were preserved exactly
  and the inconsistency is reported to the user instead (ASSUMPTION-2).

## 9. Self-verification
- [x] build-project        — **PASS**. Clean build; the deletions removed two `-Wunused-function`
      and one `-Wunused-variable` warning that the edits had briefly introduced.
- [x] architecture-review  — **PASS**. No new abstraction, no ownership change, no format change.
      The fix moves a draw stage from Renderer to UI, which is the correct owner for a screen-space
      affordance and matches the floating-viewport path already there.
- [x] code-review          — **PASS**. Each change addresses its root cause, not the symptom: an
      assignment that was missing, a shape reused rather than re-derived, and a rectangle built from
      the coordinates the hit test actually uses.
- [x] testing              — **PASS (suite)**. 292 cases, 65,430 assertions, green; no existing test
      changed. **No new regression test** — see §7; none of the three is reachable by the test
      target, and inventing an adjacent test to look thorough would be worse than saying so.

## 10. Verification result
- Submitted:  2026-08-15
- Verdict:    PASS pending the user's manual confirmation of all three symptoms
- Findings:   none blocking. One inconsistency reported, not silently fixed (ASSUMPTION-2).

## 11. Outcome
- Defects fixed:   label elevation (REQ-057/058 violation), hover highlight (design change),
                   selection box shape (REQ-058 violation, and a genuine draw-vs-select mismatch)
- Tests added:     none possible — DEBT-7, stated in §7
- Code removed:    43 lines of superseded renderer geometry + one renderer parameter
- Done:            <pending user confirmation>
