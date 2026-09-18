## Summary
- `resources/blocks/fittings/*.sat` loads as block definitions only (no loose solids in the drawing)
- INSERT dialog library pane with list + wireframe preview (linework + solid edges)
- Editable **Block unit** selector drives the unit scale factor applied on insert
- Ships bundled 4" weld neck flange sample in the fittings folder

Closes increment 6 of #475 — completes the issue #475 epic.

## Test plan
- [x] `GoSurveySnapTests.exe "[issue475][block][library][fitting]"` — SAT import as block def, no loose solids
- [x] `GoSurveySnapTests.exe "[issue475][block][units]"` — dialog unit override scales insert
- [ ] Manual: INSERT → pick flange from library → preview → place at scale 1 in feet drawing
