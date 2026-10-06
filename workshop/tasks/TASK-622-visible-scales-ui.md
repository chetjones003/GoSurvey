# TASK-622 — Visible scales UI parity + solid hatch gate

## Authority

- GitHub issue **#622** (follow-up after PR #681)

## Goal

Expose **Visible scales** wherever annotative is editable for block refs, hatches (ribbon), and multileaders; gate GL solid hatch fills and model-tab multileader overlay on `CadAnnotativeVisibleAtActiveScale`.

## Verification

- `./dev/build`
- `GoSurveySnapTests.exe "[issue622]"` + `GoSurveyTests.exe "[paperspace][issue622]"`
