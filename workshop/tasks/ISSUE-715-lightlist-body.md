## Summary

**DWG fidelity follow-up** after closed **#624** / **REQ-385**. GoSurvey now preserves individual **LIGHT** entities and **SUN** through import, `.gs`, and R2010+ export (`LibreDwgLights.cpp`, PR **#713**).

AutoCAD also maintains a **LIGHTLIST** object (and related presentation wiring) that GoSurvey does not read or write. Without **LIGHTLIST** parity, some drawings may not show the full light registry AutoCAD expects after a GoSurvey export pass.

## In scope (target)

- Research vendored LibreDWG **LIGHTLIST** (UNSTABLE in `classes.inc`) and header **DICTIONARY_LIGHTLIST** usage.
- Import: detect/count **LIGHTLIST** (+ linked lights) and REQ-201 logging when not preserved.
- Export: hand-built **LIGHTLIST** or documented degradation when LibreDWG cannot encode safely.
- Tests: fixture or helper-based round-trip where feasible; tag `[issueNNN]` once REQ exists.

## Out of scope

- **SUNSTUDY** / geographic sun study UI (separate issue if scheduled).
- GoSurvey viewport lighting model (REQ-064 stays as-is).
- Photometric IES / web lights beyond REQ-385 capture.

## Spec

**SPEC GAP:** no accepted REQ yet for **LIGHTLIST**. Propose **REQ-386** (or successor) before implementation.

## Depends on

- **REQ-385** (shipped on `beta`)
- Issue **#600** (R2010+ export path)

## References

- `docs/dwg-feature-gaps.md` (deferrals section)
- Closed: **#624**, parent **#601**
