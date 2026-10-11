## Summary

- **REQ-384 increment 3:** R2010+ DWG export hand-builds per-scale `TEXTOBJECTCONTEXTDATA`, `BLKREFOBJECTCONTEXTDATA`, and `ALDIM` / `ANGDIMOBJECTCONTEXTDATA` plus `CONTEXTDATAMANAGER` for annotative **TEXT**, **INSERT** (block refs), and **DIMENSION** hosts (model space and block-definition TEXT).
- Refactors shared context attach factory in `LibreDwgAnnotContext.cpp` (no function pointers).
- `[issue688][req384]` export test covers TEXT + INSERT + aligned DIMENSION with two annotation scales.

## Test plan

- [x] `./dev/build`
- [x] `./dev/test --filter "[issue688][req384]"`
- [x] `./dev/test --filter "[issue622]"` (regression)

## Acceptance (REQ-384 inc 3)

- Annotative TEXT, INSERT, and DIMENSION exports write context objects when annotation scales are present (R2010+).
- SPEC revision and `#601` gap doc note inc 3 shipped; #688 stays open for inc 4–5.
