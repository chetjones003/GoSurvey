# TASK-622 — annoVisScales DWG round-trip tests

## Authority

- GitHub issue **#622** (follow-up after PR #681)

## Goal

Lock in GOSURVEY `annoVisScales` EED import/export for block INSERT, HATCH, MULTILEADER, and aligned DIMENSION (TEXT already covered).

## Verification

- `./dev/build`
- `GoSurveySnapTests.exe "annoVisScales"`
