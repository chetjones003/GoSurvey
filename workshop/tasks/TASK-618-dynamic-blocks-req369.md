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
| 5 | Full round trip + `#614` loss honesty | open |

## Files (increment 4)

- `src/io/LibreDwgDynamicBlock.cpp` — `ImportDynamicBlockDefinitionFromDwg`
- `src/io/LibreDwgCad.cpp` — seed `paramState`; allow INSERT→evaluable dynamic def
- `src/util/cadblock.hpp` — linear stretch grips + local re-evaluation
- `src/commands/CadBlocks.cpp` — arm/restore generic dynamic grips
- `tests/LibreDwgCadTests.cpp` — `[issue618][inc4]`; `tests/CadBlockTests.cpp` — grip drag

## Verification

- `./dev/build`
- `GoSurveySnapTests.exe "[issue618]"` and `GoSurveyTests.exe "[issue618]"`
