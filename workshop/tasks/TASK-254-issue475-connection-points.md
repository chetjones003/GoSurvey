# TASK-254 — Issue #475 Increment 5: Connection points

**Requirement authority:** GitHub issue #475, plan D7

**Files/subsystems affected:**
- `src/util/cadblock.hpp` — `CadBlockConnection`, world collect, snap math
- `src/io/GsIo.cpp` — block `connections` JSON
- `src/commands/CadBlocks.cpp` — `BCONNECT`, `BCONNECTEDIT`, INSERT connector snap
- `src/ui/CadUi_InsertBlock.cpp`, `src/viewport/ViewportPickPolicy.hpp`, `src/ui/CadUi.cpp`
- `tests/CadBlockTests.cpp`, `tests/CadBlockImportTests.cpp`

**Implementation approach:**
1. Definition-scoped `CadBlockConnection` with local point, outward normal, name, nominal size tag.
2. `CadBlockSnapInsertToConnection` anti-aligns source/target normals and coincides points.
3. `BCONNECT` in BEDIT (typed coords or face pick); `BCONNECTEDIT` list/remove/update size.
4. INSERT dialog “Snap to connector” → pick target port on a placed block ref.

**Test approach:**
- Snap math: coincidence ±0.002 ft, normals anti-aligned
- `.gs` block def connection round-trip
- Headless: place host ref, INSERT tail with connector snap

**Acceptance:**
- BEDIT can author connection ports on block solids
- Connections persist in `.gs`
- INSERT snaps a fitting to an existing port with anti-aligned directions
