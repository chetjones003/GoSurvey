## Summary

- **REQ-372 increment 4:** `#614` export loss lines for hosts whose MATERIAL appearance is not written (3DSOLID entity material gap; pre-R2010 native MATERIAL).
- Fix MATERIAL **name** import on R2007+ DWGs (UTF-16 decode via `DecodeDwgString` / `IS_FROM_TU_DWG` gate).
- Tests: mesh R2018 export/import round-trip with `#614` solid-material loss disclosure.

## Test plan

- [x] `GoSurveySnapTests.exe "[issue624][req372]"`

## Notes

- Full GoSurvey→DWG→GoSurvey **material** round-trip on `3DSOLID` remains blocked (entity-level MATERIAL corrupts LibreDWG encode/decode); mesh hosts + honest loss list satisfy inc 4 until subentity DXF 331 follow-up.

## Links

- Issue #624 / REQ-372
- Builds on #701 (inc 3)
