# Verification: TASK-1016 MVIEW fit, presets, and object viewport plan

Date: 2026-11-03

## Architecture

**PASS with ADR-077.** The command layer continues to own MVIEW input and creation. `Viewport` owns its optional copied boundary as a value; it does not store a fragile array index or upward dependency. Qt Renderer owns screen clipping, IO owns DWG/DXF native clip-boundary references, and the plotter owns PDF clipping. The rectangular path remains the existing fast path. The significant data-model, renderer, and file-format choice is recorded before Workshop. No new dependency, layer, global state, or speculative abstraction is planned. The accepted scope reopens only the closed-polyline Object case from withdrawn REQ-034; it does not restore a vertex-drawn polygon command.

## Correctness and risk

The command geometry is testable from layout paper dimensions: Fit uses the full sheet; 2 and 4 divide evenly; 3 uses one full-height left half and two right quadrants. Object must use a copied boundary so edits/deletion of the source cannot invalidate the viewport. Clipping must apply to every model entity rendered through the viewport and to PDF output; viewport selection/editing must continue to use the visible outline. Invalid and cancelled selections must be no-ops.

The main correctness/interoperability risk is preserving curved polyline edges and native viewport clip-boundary handles across DWG/DXF. Another risk is accidentally treating a polygon viewport's bounding box as its visible/selectable shape. Test an edge-crossing geometry fixture and confirm AutoCAD reads the exported viewport without recovery. Keep legacy rectangular viewport serialization/rendering unchanged.

## Dependencies and performance

**PASS for the plan.** No new dependency is required. The clipping path adds work per rendered viewport; retain the rectangle fast path and exercise a layout with several clipped viewports. If the implementation affects frame-time requirements materially, measure against REQ-100 on the reference GPU. PDF clipping should remain vector output.

## Required verification

1. Command/headless coverage for existing two-click behavior, Fit, 2/3/4 exact counts and boundaries, adding without altering existing viewports, paper-only routing, and cancellation.
2. Closed, open, non-polyline, degenerate, straight-edge, and bulge-arc Object cases; assert source geometry is unchanged and refused selections create nothing.
3. Qt editor coverage for clipping model geometry inside the polygon and excluding geometry outside, viewport selection/edit behavior, and undo/redo.
4. PDF output coverage that verifies paths crossing the boundary are clipped and output remains vector.
5. DWG and DXF round trips for clip geometry, count, center, scale, and layout; legacy rectangular viewport round trip; AutoCAD 2025 read/reopen without recovery.
6. Windows build and relevant CTest suite after implementation. Capture the GoSurvey viewport task and compare it with Civil 3D; Civil 3D is unavailable in this environment, so the visual panel must be marked inconclusive unless that product becomes available.

## Implementation plan

Use the current MVIEW route and `Viewport` value. Add fixed rectangle partition helpers; clone supported closed polyline boundary data into the viewport; use a Qt stencil/mask when compositing and a polygon clip for PDF; use the existing standard DWG/DXF viewport clip-boundary fields; and update the command help page. Touch only the command, viewport, render/plot, IO, focused test, and help files required by these seams. Do not add a general polygon-drawing tool or a new abstraction.

## Verdict

**APPROVE the plan for implementation.** The user's decisions resolve the feature scope and acceptance conditions; ADR-077 resolves the architecture decision before code. Product validation and visual comparison limitations are explicit follow-up gates, not guessed behavior. No tests or builds were run because this was a SPEC/Verification planning turn.
