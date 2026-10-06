## Summary

- R2007+ DWG export sets model-space VPORT `*Active` `visualstyle` (and `DRAGVS`) from `viewportVisualStyle`.
- Import maps that back into GoSurvey on open.

## Test plan

- [x] `GoSurveySnapTests.exe` filter `[issue624][req371]` — 17 assertions, 3 cases including `[model]`
- [x] `./dev/build`

Part of **#624** / **REQ-371** increment 2.
