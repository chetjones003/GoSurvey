# TASK-alignment-spiral-projection-review — spiral station/offset fixes

- Type: bug fix
- Status: verified; pushed to PR #1152
- Opened: 2026-11-05
- Authority: REQ-481 spiral geometry; REQ-484 closest-point and out-of-range behavior; REQ-503 regression tests.

## Observed and expected behavior

- A point generated from a spiral station and offset must project back to that station and offset, including large offsets.
- A point at a spiral endpoint or on its perpendicular offset line is on the alignment boundary, not out of range. A point beyond an endpoint along its tangent remains out of range.

## Verification evidence and root cause

- The PR reviewer reproduced poor convergence on a 100-unit tangent-to-radius-100 spiral at station 50.03 and offset -1000.
- `RefineSpiralFoot` applies `s + along`, which assumes a unit derivative. For a spiral, the derivative depends on signed curvature times signed offset, so the iteration can overshoot or oscillate.
- The spiral candidate marks every foot within 1e-9 of either endpoint as clamped without checking the query's along-track residual. This misclassifies exact and perpendicular endpoint queries.

## Implementation and verification plan

1. Added two REQ-484 regression cases. Both failed before the fix: the large-offset query returned station 50 instead of 50.03; endpoint-normal queries were marked out of range.
2. Refined a bracketed spiral foot with a curvature-aware Newton step safeguarded by bisection; retained the sampled minimum when no local-minimum root is bracketed.
3. Marked an endpoint clamped only when the signed along-track residual points beyond that endpoint beyond the existing 1e-9 tolerance.
4. Verification passed: both regression cases (27 assertions), REQ-481/482/484 tests (49 cases, 201,912 assertions), `build.bat`, and Release CTest (194/194).

## Architectural boundary

- Keep the projection change local to `Alignment::GetStationOffset`; add no new API, state, or dependency.
