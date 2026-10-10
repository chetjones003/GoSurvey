## Summary
- REQ-372 increment 1: import AutoCAD MATERIAL diffuse on 3D mesh and solid hosts (R2007+).
- Shaded drawing uses material RGB via `ApplyMaterialDiffuseForShaded`.
- Import log reports diffuse, map-only, and missing-diffuse counts (REQ-201).

## Test plan
- [x] `./dev/build`
- [x] `GoSurveySnapTests.exe "[issue624][req372]"`

Closes #624 (materials slice, partial — export in follow-up PR).
