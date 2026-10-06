# TASK — REQ-384 annotation context inc 2 (issue #688)

- Branch: `feat/issue688-req384-inc2-mtext-export`
- Authority: **REQ-384** inc 2, GitHub **#688**
- Scope: R2010+ export hand-built `MTEXTOBJECTCONTEXTDATA` + `CONTEXTDATAMANAGER` on annotative MTEXT (model + block defs).
- Out of scope: TEXT, DIMENSION, import merge (inc 3+).

## Verification

- `./dev/build`
- `GoSurveySnapTests.exe "[issue688][req384]"`
