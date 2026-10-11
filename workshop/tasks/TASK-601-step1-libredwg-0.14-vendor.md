# TASK — LibreDWG 0.14 vendor (DWG fidelity step 1 / #600 foundation)

- Branch: `chore/libredwg-0.14-vendor`
- Authority: ADR-041 (h), D-2026-09-30-d, issue #600, #601 tracker
- Scope: Replace vendored LibreDWG 0.13.4 with upstream tag **0.14**; re-apply GoSurvey patches in `third_party/libredwg/VENDORED.md`; add `[issue600]` R2004 minimal encode round-trip test.
- Out of scope: Export UI version picker (step 2), full Export DWG R2004 path, spec amendment D-2026-09-30-c.

## Verification

- `./dev/build`
- `./build/GoSurveyTests.exe "[libredwg]"` and `"[issue600]"`
- `./build/GoSurveySnapTests.exe "[libredwg]"` (52 cases)
- `./dev/test` — 7 pre-existing headless transcript failures unchanged

## GoSurvey patches re-applied

1. GEODATA bit layout — `src/dwg2.spec`
2. INSERT attrib chain encode — `src/encode.c`
3. `dwg_add_ATTRIB` stale `insobj` — `src/dwg_api.c`
