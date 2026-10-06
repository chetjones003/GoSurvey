## Summary

- **REQ-384 increment 2:** R2010+ DWG export hand-builds `MTEXTOBJECTCONTEXTDATA` (one per annotation SCALE) and wires `CONTEXTDATAMANAGER` on the MTEXT extension dictionary for annotative MTEXT in model space and block definitions.
- Registers SCALE object handles during export so each context links to the matching `AcDbScale`.
- Keeps AcadAnnotative/GOSURVEY EED from #622 unchanged.

## Test plan

- [x] `./dev/build`
- [x] `GoSurveySnapTests.exe "[issue688][req384]"`
- [x] `GoSurveySnapTests.exe "[issue622]"` (27 cases)

## Acceptance (REQ-384 inc 2)

- [x] Annotative MTEXT R2018 export decodes with context objects (LibreDWG read-back)
- [ ] AutoCAD visual parity (manual, when available)

## Notes

- Import still uses entity geometry + EED; context merge is a later increment.
- Parent **#601** / **#688** remain open until TEXT/DIMENSION and round-trip work land.
