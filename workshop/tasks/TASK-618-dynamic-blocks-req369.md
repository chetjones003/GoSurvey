# TASK-618 — REQ-369 dynamic blocks (issue #618)

- Authority: REQ-369, D-2026-10-02-b, GitHub issue #618
- Branch: `feat/issue618-dynamic-blocks`

## Increments

| Inc | Scope | Status |
|-----|--------|--------|
| 1 | `*U` anonymous INSERT import/export; hide anon defs from library | **done** |
| 2 | Foreign golden display via `*U`; skip def-target INSERT; no action re-eval | **done** |
| 3 | GoSurvey BPARAM/BACTION → R2004+ DWG evaluation graph | **done** (linear/stretch MVP) |
| 4 | Import params + grip re-evaluation | **done** (linear/stretch MVP) |
| 5 | Full round trip + `#614` loss honesty | **done** (GoSurvey↔DWG CI; AutoCAD loop manual) |

## Files (increment 5)

- `src/io/LibreDwgDynamicBlock.cpp` — `ComputeCadBlockDynamicExportLossCounts`; INSERT-unified linear distance on export
- `src/io/LibreDwgCad.cpp` — `#614` R2004+ dynamic loss lines
- `tests/LibreDwgCadTests.cpp` — `[issue618][inc5]` round trip + loss summary

## Verification

- `./dev/build`
- `GoSurveySnapTests.exe "[issue618]"` and `GoSurveyTests.exe "[issue618]"`
