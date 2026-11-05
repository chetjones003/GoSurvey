# Plan review — TASK-1100

## SPEC and scope
- REQ-122 is accepted and requires the shared framing operation to be used for ZOOM WINDOW; issue #1100's expected two clicked corners select the view region, consistent with REQ-121's explicit ZOOM exclusion from object-selection mode.
- REQ-201 covers a reason when the requested operation cannot change the view. This fix only supplies the missing first-corner input and does not alter the existing no-op reporting path.
- The prior accepted TASK-1024 explicitly left first-corner initiation out of scope and recorded issue #1100; this task now owns that focused UI gap.

## Architecture and risk
- PASS: the corner is input/UI behavior owned by `EditorViewport`; reuse `BeginSelectionBoxCorner`, existing `selBoxWaitingSecond`, and existing Commands pick handling. No layering change, new state, or abstraction.
- Main edge to verify: only the first click for active Zoom should start the rectangle; second-click submission remains on the existing path. Keep the first click's snapped world coordinate behavior consistent with current active-command input.

## Verification plan verdict
- PASS — requirements and acceptance are clear, scope is minimal, and the proposed regression exercises two real viewport clicks and queued zoom consumption.
