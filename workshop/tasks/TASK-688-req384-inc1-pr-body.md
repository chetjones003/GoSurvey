## Summary

- Accepts **REQ-384** and decision **D-2026-10-05-f** for issue **#688** (AutoCAD annotation context DWG interop under parent **#601**).
- **Increment 1:** scan DWG for `*_OBJECTCONTEXTDATA` / `CONTEXTDATAMANAGER` on import and append a REQ-201 log line when present; GoSurvey still uses entity geometry + AcadAnnotative/GOSURVEY EED from #622.
- Adds `LibreDwgAnnotContext.cpp` and `[issue688][req384]` tests.

## Test plan

- [x] `./dev/build`
- [x] `GoSurveySnapTests.exe "[issue688][req384]"`

## Acceptance (#688 / REQ-384 inc 1)

- [x] SPEC GAP closed with accepted REQ-384
- [x] Import log when context objects present
- [x] GoSurvey annotative export still has zero context objects (baseline for inc 2)

## Notes

- **#601** stays open until **#688** (inc 2–5) and **#624** (lights/sun) complete.
- **REQ-110** unchanged (proposed; different scope).
