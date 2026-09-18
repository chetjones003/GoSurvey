## Summary
- Load B-rep solids and meshes into the drawing during BEDIT; harvest them back on BSAVE/BCLOSE
- Translate solids and mesh vertices in `CadBlockShiftContent` when baking the base point
- Persist `content.solids` in block JSON; capture selected solids into block definitions
- `BLOCKREDEF` accepts optional `baseZ`

Closes increment 4 of #475.

## Test plan
- [x] `GoSurveyTests.exe "[issue475][block][solid][bedit]"` — base-point bake shifts solid bounds
- [x] `GoSurveySnapTests.exe "[issue475][block][bedit][solid]"` — BEDIT load, BSAVE harvest, INSERT places solid at 3D offset
- [ ] Manual: BEDIT a SAT-imported flange, BSAVE, INSERT updated block
