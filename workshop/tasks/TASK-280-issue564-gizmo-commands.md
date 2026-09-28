# TASK-280 — the gizmo is summoned by 3DMOVE / 3DROTATE / 3DSCALE, not shown on every selection

- Type:    feat
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  #564 section 3 (first of the issue's eight increments)

## Requirement authority

REQ-060 as amended 2026-09-28 under D-2026-09-28-a. REQ-332 (solid rotate / uniform scale), REQ-333
(sub-object grips), REQ-201 (refusals are stated), REQ-101.

## Why

Selecting anything in model space put axis handles on screen whether or not the user was about to
transform it. Issue #564 §3 asks for the gizmo to be a command's UI instead.

## Decision (D-2026-09-28-a)

The user chose to deliver #564 one section per PR in the issue's suggested order, starting here.
§3 contradicted REQ-060's Statement, so REQ-060 was amended first. Two readings of the issue text
recorded with it: `GIZMO <op>` turns the persistent mode on and a new `GIZMO OFF` turns it off
(default off); each command performs one operation and ends.

## Plan / architectural-boundary check

No new transform, no new overlay, no new undo path: the commands set `gizmoOp`, show the existing
widget and let `CommitGizmoDrag` (the one gizmo commit) do the work. Command layer owns the state
machine; the UI only routes clicks (`ViewportClickRoute::GizmoHandlePick`) and shows the prompt.

## What changed

- `src/commands/CadCommands.hpp` — `Kind::Move3d / Rotate3d / Scale3d` (+ `KindName`),
  `gizmoPersistent`, `gizmoCmdPhase`, `gizmoOpBeforeCmd`; declarations for `IsGizmoCommandKind`,
  `CadGizmoSummoned`, `StartGizmoCommand`, `EndGizmoCommand`, `HandleGizmoCommandTextInput`,
  `CadGizmoCommandPromptText`.
- `src/commands/CadCommands.cpp`
  - `CadGizmoModeFor` returns `None` unless `CadGizmoSummoned` — the single gate.
  - `CommitGizmoDrag` ends a running 3D gizmo command after a successful commit (click, typed value
    and headless `DROP` alike); the old body is `CommitGizmoDragImpl`.
  - `ArmGizmoDrag` extracted from `SubmitGizmoClick` so a typed value arms the same state a grab does.
  - The three commands: start (pre-selection honoured; paper space refused), select-then-Enter,
    typed distance / degrees / factor (> 0), blank Enter commits an armed drag or ends the command,
    refusal with a reason when the selection cannot carry the gizmo.
  - `GIZMO OFF`, `GIZMO <op>` sets persistent; `GIZMO` refused while a 3D gizmo command runs.
  - Blank-Enter block, Kind-keyed text dispatch, `CancelActiveCommand`, `RepeatLastCommand`,
    `ResetAllCadDraftTools` (restores the op when another command replaces a running one), the
    command registry, and the select step's box-close in `SubmitViewportPickImpl`.
- `src/viewport/ViewportPickPolicy.hpp` — `GizmoHandlePick` route; select step is
  `SelectionAccumulate`.
- `src/ui/CadUi.cpp` — the `GizmoHandlePick` click case, 3DMOVE's `Ctrl`+click sub-object pick in
  its select step, the prompt hint.
- `src/app/main.cpp` — one `Esc` inside a 3D gizmo command ends the command (not only the drag).
- `tests/headless/HeadlessDriver.cpp` — `CLICK` routes `GizmoHandlePick` to `SubmitGizmoClick`.

## Tests

- `headless.issue564-gizmo-commands` (new): no gizmo on a line / solid / mixed selection; `GIZMO` /
  `GIZMO OFF`; 3DMOVE by drag and by `CLICK` + typed distance, with one `UNDO`; no-direction refusal
  with three handles; 3DROTATE typed and dragged; 3DSCALE typed with zero/negative refused; a solid
  rotated and scaled (volume/area); `Esc` restores the persistent op and the geometry; blank Enter
  ends with nothing changed; `GIZMO` refused mid-command; a solid face pushed by 3DMOVE and refused
  by 3DROTATE; paper space refused.
- `GizmoTranslateTests [issue564]` (2 new): the summoning gate; the op is restored when another
  command replaces 3DROTATE.
- `ViewportPickPolicyTests` (1 new): the two routes.
- Existing gizmo unit tests and the three gizmo transcripts now turn on `GIZMO MOVE` (or
  `gizmoPersistent`) — they test the widget, which is unchanged.
- Full suite: 1803/1811 — the 8 failures are `beta`'s pre-existing ones (issue233 CIRCLE loop,
  req313 solid-primitives DWG / isolines, req068, issue402, regression-58, req087, tessellation).

## Caught by the transcript, not by inspection

The first build of the select step accepted clicks but a window selection selected nothing:
`SubmitViewportPickImpl` has a per-command branch that closes the fence, and the new Kinds had none.
The GUI goes through the same function, so it would have shipped broken.

## Follow-up in the same PR: three rotate rings (D-2026-09-28-b)

The user asked for AutoCAD's 3DROTATE widget (screenshot): three rings, X red / Y green / Z blue,
following the active UCS.
- `CadGizmoAxisCountFor` → 3 for Rotate; `CadGizmoAxisWorld` ring `a` = UCS axis `a`.
- `PickGizmoAxis` measures the ray's distance to each ring at 96 samples; rings within
  `kGizmoRingEdgeOnCos` of edge-on are skipped.
- Commit: ring 2 (Z) → `ApplyRotationAboutUcsZ` (typed ROTATE); rings 0/1 → new public
  `ApplyRotationAboutAxis` (the in-place arbitrary-axis turn, same refusals).
- Overlay draws three rings, coloured by axis in the renderer.
- `BuildGizmoDragGhost` rotates / scales the preview instead of translating it by an angle or factor
  (latent bug the single plan-view ring hid).
- Tests: X-ring drag in unit tests; edge-on ring is not a target; transcript X-ring turn in an orbited
  view with typed 90°; ring-index / count assertions updated (Z ring is now handle 2).
- Full suite: 1805/1813 — the same 8 pre-existing failures.

## Follow-up in the same PR: AutoCAD 3DMOVE / 3DSCALE widgets + base point (D-2026-09-28-c)

User screenshots of Civil 3D's 3DMOVE and 3DSCALE; three questions put and decided as recommended
(uniform scale from every handle; working plane handles; a base-point step).
- `GizmoCmdPhase::BasePoint` between the selection and the handles: `SubmitGizmoBasePoint` (click, via
  `SubmitViewportPickImpl`, routed `SnappedPointPick`) or typed through `ResolveTypedModifyPoint`;
  Enter = centre. `gizmoBase` / `gizmoBaseValid` feed `CadGizmoAnchorWorld`. Sub-objects skip it.
- Plane handles 3–5 (`kGizmoPlaneHandleFirst`, `CadGizmoPlaneAxes`, `CadGizmoPlaneHandleCountFor`):
  picked after the arrows in `PickGizmoAxis`, grabbed in `SubmitGizmoClick` (ray∩plane), dragged in
  `UpdateGizmoDrag`, committed from the new `gizmoDragVec` (which the axis drag and typed values now
  also fill, and which the drag ghost reads). Typed `dx,dy` via `ParseTwoDoubles`.
- Scale: three handles (`CadGizmoAxisWorld` no longer forces X), each uniform; typed factor needs no grab.
- Overlay: cone heads (12 spokes + rim), box tips for scale, `plane[3]` corner marks drawn two-colour
  by the renderer, `center` circle; the amber single-scale-handle colour is gone.
- Tests: transcript sections for typed base (rotate pivot), clicked base (scale centre), Y-handle
  uniform scale, plane drag, plane typed dx,dy, a click beside the handles; unit tests for the
  base-point step and anchor; counts updated.
- Full suite: 1806/1814 — the same 8 pre-existing failures.

## Not in scope (later #564 increments)

§1 extents, §2 depth-ordered picking, §4 MIRROR / ARRAY on solids, §5 dynamic input, §6–7 layers and
pipe colours, §8 the Modeling ribbon tab (which will carry buttons for these three commands).

## Technical debt / assumptions

- None new. Rotating about UCS X/Y refuses the types typed ROTATE already refuses under a tilted UCS
  (ellipse, text, table, block, PDF, feature line, survey point) — stated, not skipped.
