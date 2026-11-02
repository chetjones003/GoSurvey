# TASK-308 — Qt interaction bug batch: grips, snapping, trim, undo, selection

- Type: bug batch
- Status: implementation in progress; verification incomplete
- Opened: 2026-11-02
- Owner: Codex

## 1. Authority

- Requirements: REQ-047 (ORTHO and grip editing), REQ-056 (TRIM), REQ-084 (object isolation),
  REQ-103 (modify commands), REQ-121 (selection-step behavior), REQ-330 (snap markers),
  REQ-438 (Qt isolation parity), and REQ-439 (Qt selection and grips).
- User report: grip drags do not snap; grip hover actions are missing; command-option clicks also
  reach the viewport; cursor does not snap to grips; LINE 2P does not accept Enter or typed angle
  adjustment after two picks; TRIM leaves a near-zero gap at line intersections; undo, snap
  isolation and multiselection are broken; snap indicators are too small; selection sometimes
  appears to swallow clicks.
- Constraints: identify each root cause before fixing; preserve domain ownership and REQ-121's
  deliberate unsnapped object-selection behavior; add focused regression coverage; no new dependency.

## 2. Scope

- In scope: Qt viewport press/move/release routing, grip hover and drag snapping, command footer/input
  routing for LINE 2P, model-space trim intersection precision, undo dispatch, isolation snap/pick
  gates, snap indicator sizing, and click/Shift/multi-selection behavior.
- Out of scope: changing accepted command semantics, introducing new geometry operations, and changing
  the shared selection/snap architecture without evidence that a shared defect requires it.

## 3. Initial diagnosis evidence

- **Grip drag snapping (confirmed):** `EditorViewport::mouseMoveEvent` computes snaps only when
  `midCmdUsesSnap` is true, but that expression omits entity, dimension, and MTEXT grip drags.
  `mousePressEvent` includes these grip states, so the initial press and subsequent drag use
  different snap gates. This explains no snap during the drag; test the fix against other hidden and
  visible candidates and ORTHO precedence.
- **Grip hover actions (confirmed):** grip drawing in `EditorViewport::paintGL` marks only an
  actively dragged grip as hot. `mouseMoveEvent` has no entity-grip hover resolution or action
  label state. Compare the existing beta behavior before adding Qt state or labels.
- **Click/selection symptoms (confirmed, likely related):** ordinary idle selection appends a new
  hit without clearing on a plain click, and the generic branch does not add a new hit on Shift
  click. Several selection-box branches also retain separate armed-box state. Trace exact click
  routes and protect REQ-439 box accumulation and dismiss behavior.
- **LINE 2P input (diagnosis incomplete):** `ApplySegmentAnglePickToViewportPick` correctly consumes
  its third click and asks for Enter or `+/-`; command submission behavior and Qt command-field focus
  need tracing to identify why keyboard input is not accepted in the reported state.
- **TRIM gap (diagnosis incomplete):** model line trimming computes an intersection and assigns that
  point to one endpoint. Reproduce with two independently trimmed intersecting lines and inspect the
  intersection solver, storage precision, and DIST pick tolerance before changing geometry math.
- **Undo, isolation snap, indicator size (diagnosis incomplete):** trace the Qt shortcut dispatch,
  REQ-084 hidden-entity snap filters, and the renderer's fixed `6.f` snap glyph argument against
  existing requirements and baseline behavior.

## 4. Plan

1. Finish deterministic reproductions and root-cause evidence for the remaining reports.
2. Record exact affected files, focused regression coverage, risks, and architectural boundary.
3. Implement the smallest fixes and add regression cases before production changes.
4. Build and run the applicable Qt/domain verification, inspect the diff, and record limitations.
5. Capture affected UI states and compare with Civil 3D where available; report an inconclusive
   comparison if Civil 3D or the required independent review panel is unavailable.

## 5. Architectural boundary

Qt owns event routing and presentation; shared Commands/Viewport services own picking, snapping,
isolation, trimming, and undo. Keep geometry corrections in the shared command layer only if a
repro proves the shared algorithm is at fault. No new abstractions or dependencies are planned.

## 6. Implementation and verification update (2026-11-02)

- Confirmed source-path changes: grip drags participate in snap resolution; selected grips are
  available as snap targets; snap glyphs use the configured drawing-state size; isolated
  line/circle/polyline/arc/ellipse/spline candidates are filtered; Shift-click toggles selection
  while a plain click replaces it; dynamic input yields Enter and `+/-` during LINE/POLYLINE bearing
  adjustment; and command hint labels stop mouse propagation into the viewport.
- Selected line-end grips now open a delayed Lengthen/Stretch action menu through the existing grip
  editing route.
- TRIM line and polyline target coordinates and intersection calculations now retain `double`
  precision rather than narrowing stored coordinates to `float`. The reported drawing was not
  available to reproduce, so acceptance of the gap correction remains unconfirmed.
- Ctrl+Z with an empty focused command field now routes to drawing undo. The broad undo report still
  lacks a deterministic reproduction; this narrow routing change does not demonstrate that every
  undo/redo case is fixed.
- `build.bat` completed successfully for Release and compiled both frontends, domain tests, and
  helpers. No test suite was run. Hidden-desktop Qt interaction tests, product repros, and screenshots
  remain outstanding. The required three-reviewer Civil 3D comparison is unavailable in this run,
  so visible UI review is inconclusive.
- Still unproven: full overlap Multi Selection behavior, all snap-isolation candidate families, the
  general swallowed-click symptom, and all undo/redo flows. Deterministic reproductions and focused
  regression coverage are still needed before marking the batch complete.
