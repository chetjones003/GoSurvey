## Summary
- Add `CadBlockConnection` on block definitions (local point, outward normal, name, nominal size)
- `BCONNECT` / `BCONNECTEDIT` in BEDIT (typed coords or face pick); connections persist in `.gs` JSON
- INSERT dialog **Snap to connector** picks a target port on a placed block ref and anti-aligns the fitting

Closes increment 5 of #475.

## Test plan
- [x] `GoSurveyTests.exe "[issue475][block][connector]"` — snap math and base-point bake
- [x] `GoSurveySnapTests.exe "[issue475][block][connector]"` — BEDIT BCONNECT typed + INSERT connector snap
- [ ] Manual: BEDIT face pick BCONNECT on flange, INSERT second fitting with connector snap
