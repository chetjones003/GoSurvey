## Summary
- Properties and hatch ribbon expose comma-separated **Visible scales** for block refs, hatches, and multileaders (annotations already had this).
- Solid hatch GL fills and model-tab multileader overlay respect `CadAnnotativeVisibleAtActiveScale`.
- `docs/dwg-feature-gaps.md` notes per-scale visibility through PR #681.

## Test plan
- [x] `./dev/build`
- [x] `GoSurveySnapTests.exe "[issue622]"`
- [x] `GoSurveyTests.exe "[paperspace][issue622]"`
