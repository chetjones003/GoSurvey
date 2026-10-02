# TASK-618 — REQ-369 dynamic blocks (issue #618)

- Authority: REQ-369, D-2026-10-02-b, GitHub issue #618
- Branch: `feat/issue618-dynamic-blocks`

## Increments

| Inc | Scope | Status |
|-----|--------|--------|
| 1 | `*U` anonymous INSERT import/export; hide anon defs from library | **this PR** |
| 2 | Foreign AutoCAD DWG golden: current visibility/size via `*U` | open |
| 3 | GoSurvey BPARAM/BACTION → R2004+ DWG evaluation graph | open |
| 4 | Import params + grip re-evaluation | open |
| 5 | Full round trip + `#614` loss honesty | open |

## Files (increment 1)

- `src/util/cadblock.hpp` — `CadBlockNameIsDynamicAnonymous`, flags
- `src/io/LibreDwgCad.cpp` — importable `*U`, anonymous save bit
- `src/io/GsIo.cpp` — persist flags
- `src/commands/CadBlocks.cpp` — library filter
- `tests/LibreDwgCadTests.cpp` — issue #618 cases

## Verification

- `./dev/build`
- `./dev/test --filter issue618`
