# TASK-214 — Issue #400 (increment 2 of 3): ARRAY Rectangular "levels" (3D grid)

- Type:    feature
- Status:  done — PR #410 (stacked on #409)
- Opened:  2026-09-07
- Owner:   Workshop
- GitHub:  #400

## 1. Authority

- REQ-305 acceptance 12 (added 2026-09-07).
- Builds on increment 1 (TASK-213, PR #409): `ArrayCellWorldDelta` already converts a UCS-local
  (colOffset, rowOffset, 0) vector to a world dx/dy/dz; this increment only needs a non-zero third
  local component.

## 2. Scope

- `AppCommandState` gains `arrayLevels` (int, default 1) and `arrayLevelSpacing` (float).
- New phases `Rect_WaitLevels` (typed count, Enter = 1 = skip) and `Rect_WaitLevelSpacing` (typed
  distance only — no click; see REQ-305 acceptance 12 for why), inserted between
  `Rect_WaitRowSpacing` and the commit.
- `CommitArrayRectangular` gains a third loop dimension; `ArrayCellWorldDelta` takes a level offset
  as its Z-axis local component.
- `FinishArrayCommand`'s shape string becomes `cols x rows x levels` only when levels > 1.
- Out of scope: Solid/Surface duplication (increment 3).

## 3. Files

- `src/commands/CadCommands.hpp`: `AppCommandState::ArrayPhase`, new fields.
- `src/commands/CadCommands.cpp`: `ResetArrayDraft`, `ArrayCellWorldDelta`, `CommitArrayRectangular`,
  `HandleArrayText` (Rect_WaitRowSpacing now advances to Rect_WaitLevels instead of committing;
  new Rect_WaitLevels/Rect_WaitLevelSpacing blocks).

## 4. Tests

- New headless transcript: 3-level rectangular array under World UCS (regression: 0/1 levels
  identical to today), N-level array under a UCS rotated about Z, N-level array stacking along a
  tilted UCS's Z axis.

## 5. Verification

- `build-project`, `testing`.
