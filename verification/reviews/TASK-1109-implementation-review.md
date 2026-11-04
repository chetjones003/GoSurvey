# TASK-1109 implementation review

- Date: 2026-11-03
- Scope: REQ-055, REQ-367, REQ-384, REQ-456
- Reviewer: implementation self-review
- Result: PASS; user visual approval remains pending

## Findings

No blocking findings.

- DWG import reads the saved model-space VPORT and active `CANNOSCALE`; the viewport camera and annotation scale are restored independently.
- Legacy LEADER + MTEXT import retains the MTEXT attachment, rotation, dimensions, font, arrow size, and the LEADER's flagged hookline. The hookline is reconstructed from the DXF direction flag/vector and the annotation-scaled DIMASZ.
- Native MLeader annotation context names and label dimensions are retained through import/export. Export uses the stored world label height instead of deriving a different height from the plotted default.
- Windows launch forwarding is scoped to the executable path, so a second launch routes the DWG to a matching running editor while a separate worktree executable remains independent.
- Regression coverage exercises saved viewport/scale restoration, annotative context round-trip, legacy callout geometry, export dimensions, and Windows window reuse.
- TTF CAD text height is measured as rendered cap height. `cadTrueTypeNominalHeightMapsToRenderedCapitalHeight` failed before the font-size calibration and passes after it.
- Legacy LEADER paths run from arrow tip to the text-side landing. The synthesized hookline outer endpoint is now inserted before that landing; the MESSER diagnostic failed with the old path ordering and the focused regression passes with the corrected ordering.
- Hookline direction accounts for MTEXT attachment column as well as the saved direction bit. A paired left/right attachment regression failed with the shared sign and passes with side-aware orientation.

## Verification evidence

- `build.bat`: passed.
- `ctest --preset ninja-release` after the path ordering and attachment-side direction corrections: passed, 191/191 tests.
- Focused `Legacy LEADER MTEXT keeps glyph height and orders hookline before text landing (REQ-367)`: paired left/right attachment regression passed after the direction correction.
- `git diff --check`: passed.
- Exact MESSER DWG hidden-Qt captures are under `build/artifacts/Release/` and are presented for user visual review. One uses the DWG's saved model view; one uses the live Civil 3D camera values shown in the user reference.

## Review limits

The supplied Civil 3D screenshot shows an unsaved viewport state (the drawing title has an unsaved-change marker). That state is not represented by the DWG's saved active VPORT. The GoSurvey captures are therefore labeled separately: one uses the same live camera position/height shown in the Civil 3D properties, and one verifies the saved-DWG open behavior. The live-view comparison after the tail-order correction is presented for user review; final acceptance of visual fidelity remains pending.
