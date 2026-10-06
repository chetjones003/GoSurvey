## Summary
- Accept **REQ-386** (D-2026-10-05-h) and implement AutoCAD **LIGHTLIST** / **ACAD_LIGHTLIST** interop on top of REQ-385 lights/sun.
- Import captures registry entries and dictionary key; R2010+ export hand-builds **LIGHTLIST** (LibreDWG has no public writer) and wires **DICTIONARY_LIGHTLIST**.
- Additive `.gs` field `dwgImportedLightList`; update `docs/dwg-feature-gaps.md`.

## Test plan
- [x] `./dev/build`
- [x] `GoSurveySnapTests.exe [issue715] [req386]`
- [x] `GoSurveySnapTests.exe [req385] [libredwg]`

## Acceptance (REQ-386)
- [x] Import/export helper round-trip finds **LIGHTLIST** with registry entries at R2018
- [x] Below-R2010 export loss includes **LIGHTLIST** with LIGHT/SUN
- [x] Gap doc no longer lists **LIGHTLIST** as SPEC GAP

Closes #715
