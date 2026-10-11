# TASK-255 — Issue #475 Increment 6: Fitting library + units UX

**Requirement authority:** GitHub issue #475, plan D5/D6 increment 6

**Files/subsystems affected:**
- `src/commands/CadBlocks.cpp` — bundled fittings load, library catalog, insert units scale
- `src/ui/CadUi_InsertBlock.cpp` — library pane, preview, block unit selector
- `resources/blocks/fittings/` — bundled `.sat` fittings

**Implementation approach:**
1. `resources/blocks/fittings/*.sat` imports as block defs only (no loose solids).
2. INSERT dialog library list + wireframe preview (linework + solid edges).
3. Editable block unit override (`insertBlockUnitsBuf`) drives INSERT scale factor.

**Test approach:**
- LoadBundledBlockLibrary: fittings sat → block def, zero loose solids
- CadBlockInsertUnitsScale honours dialog override

**Acceptance:**
- INSERT lists bundled fittings; preview shows solid wireframe
- User can set block units and see factor before placing
