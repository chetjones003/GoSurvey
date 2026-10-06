## Summary

**DWG fidelity follow-up to closed #622.** GoSurvey now ships GoSurvey-native annotative scaling (viewport/CANNOSCALE, SCALE list, per-scale visibility, GOSURVEY + AcadAnnotative EED on major entity types — PRs **#657–#687** on `beta`).

AutoCAD still stores **per-scale annotation context** (`*_ANNOTATION_CONTEXT_DATA`, `CONTEXTDATAMANAGER`, etc.) that GoSurvey does not read or write. Without that, full AutoCAD parity for annotative objects opened in AutoCAD (multiple scale-specific representations, native context-driven visibility) is incomplete.

## In scope (target)

- Research LibreDWG support for annotation context objects (STABLE vs UNSTABLE/DEBUGGING classes).
- Import/export strategy: either hand-built context blobs for R2010+ export, or documented honest degradation + export loss entries when context cannot be preserved.
- Native **DIMENSION** annotative flags where LibreDWG lacks fields (GoSurvey today uses AcadAnnotative / GOSURVEY EED — verify AutoCAD behavior and close gaps).
- Tests: round-trip or fixture-based parity where feasible; `[issue622]` tests remain the regression suite for GoSurvey-native annotative behavior.

## Out of scope

- Re-implementing GoSurvey-native annotative scaling (delivered in #622).
- Civil 3D–specific annotation containers beyond ordinary AutoCAD DWG.

## Spec

**SPEC GAP:** no accepted REQ yet for AutoCAD annotation **context** blobs. **REQ-110** remains **proposed**; decide whether to accept/amend REQ-110 for context work or add a new REQ before implementation.

## Depends on

- #601 (DWG fidelity tracker)
- #600 (R2010+ export path — shipped)

## References

- Closed epic: #622
- `docs/dwg-feature-gaps.md` (annotative section)
- `src/io/LibreDwgCad.cpp` — `WriteAnnotativeEntityEed`, `AppendAcadAnnotativeEntityEed`, SCALE export/import

Part of **#601**.
