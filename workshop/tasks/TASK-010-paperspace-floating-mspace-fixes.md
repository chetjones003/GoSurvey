# TASK-010 — Paper space / floating-MSPACE fixes (REQ-036, REQ-037 refinements)

- Type:    bug / feature-refinement
- Status:  implement (incremental — user reported 6 issues, fixing in sub-slices)
- Opened:  2026-06-17
- Owner:   Workshop

## 1. Authority
- Requirements: REQ-036 (floating model space, accepted), REQ-037 (native paper geometry, accepted)
- Constraints:  no broken functionality; coding standards; no new dependency

## 2. User-reported issues (2026-06-17)
1. Object snapping does not work inside the floating viewport (REQ-036).
2. Selection (pick/window) does not work inside the floating viewport.
3. "Many things broken" inside the floating viewport — root cause: the floating-MSPACE input block
   (CadUi ~5997) is a thin forwarder; it only calls SubmitViewportPick on a single click WHEN a command
   is active. The full model input path (selection, window-box, snapping, hover, grips) is gated on
   `modelSpace` and skipped in floating mode.
4. Selecting a viewport should hit only the viewport's visible border, not clicking inside the rect.
5. ORTHO does not work in paper space (paper LINE bypasses the model ortho path).
6. Large feature — keep chipping away incrementally.

## 3. Root-cause note (issues 1–3)
The model input pipeline derives world coords from the model-view window (worldLeft/Right/Top/Bottom +
screen mx/my) and is gated `modelSpace` throughout. Floating MSPACE needs that same pipeline but with the
cursor mapped through the active viewport's transform and clipped to its rect. Proper fix = inject the
floating screen→model mapping into the shared model input path (a coordinate seam), not duplicate it.
This is a larger refactor; deliver in slices.

## 4. Plan (incremental sub-slices)
- [x] 5: ORTHO in paper space for LINE (constrain to H/V from anchor; osnap overrides ortho).
- [ ] 2a: route idle left-clicks inside the floating viewport to selection (SubmitViewportPick when idle).
- [ ] 1: object snapping inside the floating viewport (snap against model geometry at the floating cursor).
- [ ] 2b/3: window-select + hover + grips inside the floating viewport (the input-path unification).
- [ ] 4: viewport selection hit area = border only (verify/adjust the inOuter/!inInner band + grips).

## 5. Implementation log
- 2026-06-17 task opened from user feedback (6 issues).
- 2026-06-17 [#5] ORTHO in paper space: paper LINE commit + rubber-band preview now constrain the segment
  to H/V from the anchor when cmd.orthoMode is on; object snap overrides ortho (AutoCAD behavior). Build
  green; 171 tests pass.
- 2026-06-17 [#4] Viewport border hit band clamped to 0.25×min(W,H) per viewport so a small/zoomed-out
  viewport keeps a non-selecting interior — only the visible border ring selects, never the rect interior.

## NEXT (floating MSPACE — needs a focused pass + user testing; do NOT ship blind half-fixes)
- #2a idle selection: route idle left-clicks inside the floating viewport to SubmitViewportPick. Risk:
  SubmitViewportPick's idle window-box flow (selBoxWaitingSecond) has no box rendering in floating mode —
  verify behavior before enabling.
- #1 snapping: compute model object-snap at the floating cursor (mLocalX/Y) and use the snapped point for
  picks + show the glyph. The floating block currently passes the raw mapped cursor.
- #2b/#3 window-select + hover + grips: the real fix is to drive the shared model input path with the
  floating screen→model mapping (a coordinate seam), rather than the thin single-click forwarder at
  CadUi ~5997. Larger refactor; the model input path is gated `modelSpace` throughout.
