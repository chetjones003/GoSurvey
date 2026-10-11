# TASK-306 — Issue #617 live fields (REQ-368)

- Authority: REQ-368, D-2026-10-02-a/b, GitHub #617
- Branch: `feat/issue617-native-fields` (split from `feat/issue617-fields` / PR #677)
- Status: complete (inline evaluation + native FIELD/FIELDLIST export)

## Delivered

- `CadField.hpp/cpp` — evaluate `%<\GoSurvey …>%`, `%<\AcVar …>%`, subset of `%<\AcObjProp …>%`
- MTEXT viewport draw resolves field codes (model + paper)
- MTEXT toolbar Insert field enabled
- DWG TEXT/MTEXT export: R2004+ native FIELD + FIELDLIST + `_FldIdx`; R2000 evaluates
- TABLE cell MTEXT and block ATTRIB values with field wires get the same native export path
- `LibreDwgField.cpp` — hand-built AcDbField / AcDbFieldList (LibreDWG has no `dwg_add_FIELD`)
- `CadFieldTests` + `LibreDwgCadTests` issue #617

## Verification

- `./dev/build` PASS
- `GoSurveySnapTests.exe "[cadfield]"` PASS
- `GoSurveyTests.exe "[issue617]"` PASS
