# TASK-803b — REVCLOUD Revision Cloud command in Qt (GitHub #803, REQ-444)

- Type:    feature
- Status:  submitted
- Opened:  2026-10-20
- Owner:   Claude

## 1. Authority
- Requirements: REQ-444 (accepted 2026-10-20, D-2026-10-20-e). Qt only; beta removed ImGui support under REQ-440.
- Owning subsystem: Commands (typed command + state), util/geom2d (scallop geometry), Viewport
  (pick route, rubber preview), Qt ribbon catalog.

## 2. Scope
- In scope: `REVCLOUD` click path and Object mode (closed polyline, rectangle, circle), Arc length /
  Reverse / Delete-original options, live preview, both Qt ribbon buttons enabled.
- Out of scope: Spline (REQ-443), Wipeout (REQ-442), freehand drag, retired ImGui ribbon, paper space.

## 3. Architectural boundary check
- [x] No new entity kind, store, dependency or file-format change: the cloud is a closed bulge
      polyline (REQ-316).

## 4. Verification
- `RevisionCloudTests` (geometry), `headless.req444-revcloud` (command line, clicks, undo),
  `QtEditorTests::ribbonRevisionCloudButtonsDrawACloud`, screenshots reviewed and approved by the user.
- Full non-Qt ctest: 2688/2689; the one failure (`headless.regression-surfacedelete-selection`) is in
  surface deletion and untouched by this change. `qt.editor` crashes in
  `approvedTopographyStaysInPanelBackgrounds` (widget grab), unrelated; neither confirmed against a clean beta.

## 5. Assumptions / technical debt
- Closing: Enter, `C`, or a click within half an arc length of the first corner.
- Delete-original defaults to off (keep the source). A polyline with arcs or a non-flat shape is refused.
- A scallop's bulge is fixed at 0.5 (~106 degree arc); not user-adjustable.
- Draws at the work-plane elevation in the XY plane (as RECT does); a tilted UCS is not followed.
