# TASK-622 — Per-scale annotative visibility (GoSurvey + GOSURVEY EED)

## Authority

- GitHub issue **#622** (remaining slice after PR #679)

## Goal

When a drawing has a SCALE list, annotative objects may list which scale **names** they appear at. Empty list = visible at all scales (default). Gate model and layout viewport drawing; persist in `.gs` and GOSURVEY DWG EED (`annoVisScales`).

## Out of scope

- AutoCAD `CONTEXTDATAMANAGER` / per-scale geometry blobs
- Full DWG import of native annotation context objects (LibreDWG UNSTABLE)

## Verification

- `./dev/build`
- `GoSurveySnapTests.exe "[issue622]"` + paperspace visibility test
