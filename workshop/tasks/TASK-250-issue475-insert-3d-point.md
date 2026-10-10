# TASK-250 — Issue #475 Increment 1: 3D INSERT insertion point

**Requirement authority:** GitHub issue #475 (split from #473), REQ-107 (block support), REQ-318/ADR-049 (sub-object pick), REQ-322 (MOVE 3D), REQ-058 (snap Z)

**Files/subsystems affected:**
- `src/commands/CadBlocks.{hpp,cpp}` — `SubmitInsertBlockPick` signature, `CadBlockInsertPreviewXform`
- `src/commands/CadCommands.cpp` — typed INSERT `ParseStoragePointZ` path
- `src/ui/CadUi.cpp` — viewport `InsertBlockPick` click route (commitZ)
- `src/viewport/CadRubberPreview.cpp` — ghost preview Z
- `tests/headless/HeadlessDriver.cpp` — CLICK InsertBlockPick 3D
- `tests/CadBlockImportTests.cpp` — update/extend 3D assertions (keep 2-arg wrapper for legacy)

**Implementation approach:**
1. Widen `SubmitInsertBlockPick` to `(float wx, float wy, float wz, vector<string>&)`; keep 2-arg overload that forwards `wz=0` for legacy tests.
2. Store `insertBlockZ = wz` in `WaitInsertPoint` phase (was missing).
3. Typed path: switch `ParseStoragePoint` → `ParseStoragePointZ` to capture `worldZ`, publish via `resolvedPointZ`, pass as `wz`.
4. Viewport path: compute `commitZ = viewportSnapPickValid ? viewportSnapPickLocalZ : uiCursorWorldZ` (mirrors Trim's `commitZ` pattern) and call 3-arg pick.
5. Preview: `CadBlockInsertPreviewXform` gains `curZ` param (overload); during `WaitInsertPoint` use cursor Z, not stored `insertBlockZ`, so ghost matches commit.
6. Headless: `CLICK` InsertBlockPick forwards `clickHasZ ? clickZ : 0` and `PICK`-style `resolvedPointZ` handling.

**Test approach:**
- Existing `CadBlockImportTests` keep passing (2-arg wrapper → Z=0).
- New unit assertion: `SubmitInsertBlockPick(st, 10,20,5, log)` → `insertBlockX/Y/Z == 10/20/5`.
- Headless transcript `insert-3d-point.txt`: `INSERT` → `PICK`/`CLICK` with Z → `EXPECT BLOCKREFS 1` and custom check for insertion point XYZ via new `EXPECT INSERTBLOCK` or by inspecting `cadBlockRefs[0].xf`.

**Architectural-boundary check:**
- No new layer violation; Commands→Entities only.
- No `gl*` outside Renderer.
- Solids remain `shared_ptr<const Solid>` — INSERT only stores `xf.z`.
- `CadBlockShiftContent` not touched in this slice.

**Acceptance:**
- Viewport snap to a solid face centre (F4) at Z=5 inserts block at Z=5, not 0.
- Typed `10,20,5` and `@5,0,3` work (UCS-aware via `ParseStoragePointZ`).
- Ghost preview Z matches committed Z within REQ-101 (±0.002 ft).
