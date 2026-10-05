## Summary

- **REQ-384 increment 4:** R2010+ export adds `MLEADEROBJECTCONTEXTDATA` and `ACDB_HATCHSCALECONTEXTDATA_CLASS` per-scale context on annotative **MULTILEADER** and **HATCH** hosts.
- DWG import resolves default-scale context from `CONTEXTDATAMANAGER` and merges **MTEXT** / **TEXT** geometry when present.
- `[issue688][req384]` export and import-merge tests.

## Test plan

- [x] `./dev/build`
- [x] `GoSurveySnapTests.exe "[issue688][req384]"`
- [x] `GoSurveySnapTests.exe "[issue622]"`

## Acceptance (REQ-384 inc 4)

- Annotative MULTILEADER and HATCH exports write scale context objects when annotation scales are present.
- Import applies default MTEXT/TEXT context geometry; REQ-201 log reflects merge count when > 0.
- #688 stays open for increment 5 (round-trip).
