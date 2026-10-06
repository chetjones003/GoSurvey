## Summary

- Adds **REQ-371** (issue #624 increment 1): per-layout-viewport `VisualStyle` persisted in `.gs` and round-tripped on **R2007+** DWG via AutoCAD `VISUALSTYLE` handles on paper `VIEWPORT` entities.
- New `LibreDwgVisualStyle.cpp` creates/reuses dictionary styles and maps import back to GoSurvey 2D Wireframe / Hidden / Shaded.

## Out of scope (still #624)

- MATERIAL, LIGHT, SUN
- Model-space VPORT table
- Using per-viewport style in the renderer (export/import parity first)

## Test plan

- [x] `./dev/build`
- [x] `GoSurveySnapTests.exe` filter `[issue624][req371]` (7 assertions)

## Acceptance (REQ-371)

- [x] R2018 DWG Hidden + Shaded paper viewports re-import correctly
- [x] `.gs` field `visualStyle` on layout viewports (additive)
- [x] Gap doc updated — #624 narrowed to materials/lights
