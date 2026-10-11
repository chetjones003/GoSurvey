## Summary

- **REQ-372 increment 3:** Persist imported/exported material **diffuse override** and optional **AutoCAD material name** on `EntityAttributes` through `.gs` (additive JSON keys, no `kGsFormatVersion` bump).
- DWG import records `MATERIAL` name; R2007+ export reuses that name when writing dictionary entries.
- Spec/task revision notes for inc 3.

## Test plan

- [x] `GoSurveySnapTests.exe "[issue624][req372]"` — 3 cases, 31 assertions

## Links

- Issue #624 / REQ-372
- Builds on inc 1 (#697) and inc 2 (#699)
