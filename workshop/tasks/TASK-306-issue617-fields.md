# TASK-306 — Issue #617 live fields (REQ-368)

- Authority: REQ-368, D-2026-10-02-a, GitHub #617
- Branch: `feat/issue617-fields`
- Status: implemented (v1); native FIELD/FIELDLIST DWG objects deferred

## Delivered

- `CadField.hpp/cpp` — evaluate `%<\GoSurvey …>%`, `%<\AcVar …>%`, subset of `%<\AcObjProp …>%`
- MTEXT viewport draw resolves field codes each frame
- MTEXT toolbar Insert field enabled
- DWG TEXT/MTEXT export: R2004+ keeps wires; R2000 evaluates
- `CadFieldTests` on GoSurveySnapTests

## Deferred (REQ-368 v1 out of scope)

- ATTRIB / TABLE cell hosts
- Paper-space TEXT/MTEXT field resolution in draw (model MTEXT done)
- LibreDWG `dwg_add_FIELD` / FIELDLIST reactors

## Verification

- `./dev/build` PASS
- `GoSurveySnapTests.exe "[cadfield]"` PASS
