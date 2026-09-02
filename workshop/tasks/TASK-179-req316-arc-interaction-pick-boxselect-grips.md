# TASK-179 — REQ-316: arc-aware polyline interaction (pick, box-select, midpoint bulge grips)

- Type:    feature
- Status:  self-verify
- Opened:  2026-09-02
- Owner:   chetjones003

## 1. Authority
- Requirements: REQ-316 (accepted) — acceptance item 3 ("selectable by picking on the curve"),
  the arc-snap line's pick prerequisite, and ADR-047 (h) arc-segment grips.
- ADR:          ADR-047 (increment 2 pick/select + increment 3 grips)
- Decision:     D-2026-09-02-b

## 2. Scope
- In scope:
  - `PickClosestCadEntity` hit-tests a polyline's curved segment against the ARC it draws
    (sampled), not its chord — so a click or hover on the bulge registers.
  - `ChainHitsRect` (box selection) tessellates a bulge segment for the crossing test; gains an
    optional `BULGE` array parameter (null for feature lines).
  - A midpoint (bulge) grip on every ARC segment of a selected polyline. Dragging it recomputes the
    bulge so the arc passes through the cursor (`geom2d::ArcBulgeThrough`). `which` is encoded as
    `kPolyBulgeGripBase + segmentIndex`. Wired at all four grip sites (model + floating viewport,
    draw + grab), `ApplyEntityGripPoint`, and both RMB/Esc cancel-restore paths.
- Out of scope: snapping to arc points (endpoint/mid/centre/quadrant on the curve) — REQ-316's OSNAP
  line, a separate follow-up; Properties-panel bulge field; straight-segment midpoint grips.
- Smallest change: sample the arc where a chord was tested; one grip family keyed off a base index.

## 3. Architectural boundary check
- [x] No NEW abstraction/layer/dependency/global/API/data-format change. `ArcBulgeThrough` and
  `CadForEachPolylineArcMidGrip` are value helpers; `kPolyBulgeGripBase` is a constant.
  `ChainHitsRect` gains a defaulted parameter. Proceed.

## 8. Implementation log
- 2026-09-02 PickClosestCadEntity polyline loop: `polySegD2` samples the arc for a curved segment.
- 2026-09-02 ChainHitsRect: `segHitsRect` tessellates a bulge segment; +optional BULGE param;
  polyline call passes `&st.userPolylineVertsBulge`.
- 2026-09-02 geom2d::ArcBulgeThrough (circle through 3 points → signed bulge) + 4 unit tests.
- 2026-09-02 CadForEachPolylineArcMidGrip helper (CadCommands.hpp); kPolyBulgeGripBase.
- 2026-09-02 grip draw (model 16911, float-vp 15097), grip grab (model 14135, float-vp
  TryBeginEntityGripAtLocal), ApplyEntityGripPoint bulge branch, cancel-restore (hpp + CadUi).
- 2026-09-02 CadRubberPreview + appendCommittedPolylineStrip already arc-aware (TASK-177 inc 1d) —
  so live preview and the selection / hover highlight follow the curve.
- 2026-09-02 req316-polyline-arc-segments.txt +section 5 (box-select follows the arc). Full suite:
  820 Catch2, 1001 ctest green.

## 9. Self-verification
- [x] build-project — PASS (release / GoSurveyTests / gosurvey_headless, MSVC)
- [x] architecture-review — PASS (no Workshop architectural decision)
- [x] code-review — PASS. One grip helper shared by 4 sites so they cannot disagree; bulge grip
  cancel restores the original value; ArcBulgeThrough returns 0 on collinear/degenerate.
- [x] dependency-audit — n-a
- [x] performance-review — n-a. Arc sampling is 24 points per curved segment, only for selected /
  hovered polylines and the box test — not a per-frame all-entities path.
- [x] testing — PASS. box-select over the bulge (and a chord-straddling box that must NOT select);
  ArcBulgeThrough round-trip + sign + collinear.

## 10. Verification result
- Verdict: PASS (self-verified). Arc OSNAP and a Properties bulge field remain follow-ups.
