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
- [x] 4: viewport selection — border-only click band + crossing-box only when it touches a border edge.
- [x] 2a: idle left-clicks inside the floating viewport route to SubmitViewportPick (single-click select).
- [x] 1: object snapping inside the floating viewport — CadSnap::FindBest at the floating cursor with the
      viewport's own px→world tolerance; snapped point drives the pick (viewportSnapPickValid) and the
      rubber-band/crosshair preview; green snap glyph drawn in the overlay.
- [ ] 2b/3: window-DRAG box selection + hover highlight + grips inside the floating viewport (the input-path
      unification — model input path is gated `modelSpace`; needs the floating screen→model seam).

## 5. Implementation log
- 2026-06-17 task opened from user feedback (6 issues).
- 2026-06-17 [#5] ORTHO in paper space: paper LINE commit + rubber-band preview now constrain the segment
  to H/V from the anchor when cmd.orthoMode is on; object snap overrides ortho (AutoCAD behavior). Build
  green; 171 tests pass.
- 2026-06-17 [#4] Viewport border hit band clamped to 0.25×min(W,H) per viewport so a small/zoomed-out
  viewport keeps a non-selecting interior — only the visible border ring selects, never the rect interior.

- 2026-06-17 [#1 + #2a] Floating viewport now snaps + selects: mirrored the model snap pipeline in the
  floating input block (CadSnap::FindBest with the viewport-scale tolerance → viewportSnapPickValid +
  snapped pick); idle clicks route to SubmitViewportPick (single-click selection); preview cursor + green
  snap glyph drawn via the overlay's mlToScreen. Build green; 171 tests pass.

- 2026-06-17 [glyph + hover + selection] Snap glyph sized to cmd.objectSnapGlyphHalfPx (matches model).
  Hover pick (PickClosestCadEntity) at the floating cursor when idle; selected (blue) + hovered (light
  blue) model entities highlighted in the floating viewport's overlay draw (lines/circles/polylines/arcs).
  Build green; 171 tests pass.

- 2026-06-17 [hover + selection FIXED] Two root causes found: (1) the model snap/hover/grip block
  (~CadUi 6180) was not gated by modelSpace, so in floating it re-ran with paper coords and reset
  viewportHoverEntityValid — now gated to model space. (2) idle SubmitViewportPick is a NO-OP; model
  selection is the two-click/drag box (BeginSelectionBoxCorner → finishBox). Floating now arms+closes the
  box (window/crossing) and renders it. Build green; 171 tests pass.

- 2026-06-17 [grips DONE] TryBeginEntityGripAtLocal (CadCommands) does a local-coord grip grab + stores
  originals + undo snapshot (line/circle/polyline/arc/ellipse). Floating click handler grabs a grip
  (priority over select/box) and commits on the next click; the existing ungated drag/RMB-cancel blocks
  handle the live move + cancel using the floating outCursor (with snapping, dragged entity excluded). Grip
  squares rendered in the overlay (grabbed grip accent-colored). Build green; 171 tests pass.

## STATUS: floating model space feature-complete for this pass
Working in the floating viewport: snapping, selection (click-to-pick hovered + window/crossing box), hover,
draw + edit commands, full crosshair cursor, ORTHO, and grips. Issues #1–#6 from 2026-06-17 all addressed.

## Possible future polish (not requested)
- Survey-point hover/selection inside the floating viewport (currently model-only).
- Grip-edit for annotations/dimensions through the floating viewport (model-only today).
