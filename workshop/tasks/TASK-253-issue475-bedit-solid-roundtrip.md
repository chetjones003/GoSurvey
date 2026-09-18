# TASK-253 — Issue #475 Increment 4: BEDIT solid round-trip + 3D base point

**Requirement authority:** GitHub issue #475, REQ-107/ADR-043, plan D6

**Files/subsystems affected:**
- `src/commands/CadBlocks.cpp` — `LoadBlockPrimitivesIntoDrawing`, `HarvestDrawingPrimitivesIntoContent`, `CaptureSelectionInto`
- `src/util/cadblock.hpp` — `CadBlockShiftContent` solids/meshes
- `src/io/GsIo.cpp` — block `content.solids` JSON round-trip
- `tests/CadBlockTests.cpp`, `tests/CadBlockImportTests.cpp`

**Implementation approach:**
1. BEDIT load/harvest includes `content.solids` / `solidAttrs` (and meshes, already partial).
2. `CadBlockShiftContent` translates solids via `brep::Translate` and mesh vertices.
3. Block content JSON writes/reads solids (additive, like meshes).
4. `CaptureSelectionInto` copies selected solids into block content relative to base point.
5. `BLOCKREDEF` accepts optional `baseZ`.

**Test approach:**
- `CadBlockBakeBasePoint` shifts solid bounds
- BEDIT → BSAVE harvests solid back into definition
- `.gs` block content solid round-trip (optional if covered by existing GsIo tests)

**Acceptance:**
- BEDIT on a block with a B-rep solid shows and edits the solid in isolation
- BSAVE/BCLOSE preserves solids in the definition
- INSERT after BEDIT places the updated solid correctly
