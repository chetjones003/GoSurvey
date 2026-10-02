# TASK-618 — REQ-369 dynamic blocks (issue #618)

- Authority: REQ-369, D-2026-10-02-b, GitHub issue #618
- Branch: `feat/issue618-dynamic-blocks`

## Increments

| Inc | Scope | Status |
|-----|--------|--------|
| 1 | `*U` anonymous INSERT import/export; hide anon defs from library | **done** |
| 2 | Foreign golden display via `*U`; skip def-target INSERT; no action re-eval | **this PR** |
| 3 | GoSurvey BPARAM/BACTION → R2004+ DWG evaluation graph | open |
| 4 | Import params + grip re-evaluation | open |
| 5 | Full round trip + `#614` loss honesty | open |

## Files (increment 2)

- `src/util/cadblock.hpp` — skip `CadBlockApplyActionsToPoint` on `dynamicAnonymous`
- `src/io/LibreDwgCad.cpp` — canonical name via `DYNAMICBLOCKPURGEPREVENTER`; skip INSERT→def
- `tests/LibreDwgCadTests.cpp` — golden display (`inc2` tag)
- `tests/CadBlockTests.cpp` — action skip unit test

## Verification

- `./dev/build`
- `GoSurveySnapTests.exe "[issue618][inc2]"` and `GoSurveyTests.exe "[issue618][block]"`
