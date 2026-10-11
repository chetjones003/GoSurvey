# TASK-251 — Issue #475 Increment 2: Render block-reference solids

**Requirement authority:** GitHub issue #475, REQ-107, REQ-320/ADR-051, plan D2 (linked reference render)

**Files/subsystems affected:**
- `src/util/cadblock.hpp` — `CadBlockCollectWorldSolids`, `CadBlockTransformSolid`
- `src/commands/CadBlocks.cpp` — `ExplodeRef`, INSERT non-uniform scale log
- `src/commands/CadCommands.{hpp,cpp}` — `blockRefWorldSolids`, `RefreshSolidDisplayGeometry`, `ComputeWorldExtents`
- `tests/CadBlockTests.cpp`, `tests/CadBlockImportTests.cpp`

**Implementation approach:**
1. `CadBlockTransformSolid` applies uniform scale + Z/Y/X rotations + translation (matches `CadBlockXformPoint` order).
2. `CadBlockCollectWorldSolids` walks defs/nested like lines; refuses non-uniform scale per `brep::Scale`.
3. `RebuildBlockRefWorldSolids` caches transformed solids keyed by block-ref signature; tessellated via existing `solidDisplayCache`.
4. `ExplodeRef` appends world solids to `cadSolids`.
5. `ComputeWorldExtents` / `CadBlockWorldAabb` include solid bounds.

**Test approach:**
- Unit: collect transform Z offset; non-uniform scale empty; explode → `cadSolids` count + bounds.

**Acceptance:**
- INSERT of a block with `content.solids` draws the solid at the insert transform.
- EXPLODE copies the solid into `cadSolids`.
- ZOOM EXTENTS includes block-ref solid bounds.
- Non-uniform scale on a solid block logs refusal and skips solid draw.
