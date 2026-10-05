## Summary

- **REQ-384 increment 5:** `[issue688][req384]` round-trip test — R2018 export + LibreDWG re-read preserves **MTEXT** and **aligned DIMENSION** annotation context object counts.
- **`#614`:** `ComputeDwgExportLosses` reports annotative hosts without context on pre-R2010 export and **annotative hatch** simplified scale-only context on R2010+.
- Closes REQ-384 phased delivery; `#601` gap doc updated for #688.

## Test plan

- [x] `./dev/build`
- [x] `GoSurveySnapTests.exe "[issue688][req384]"`
- [x] `GoSurveySnapTests.exe "[issue622]"`

## Acceptance (REQ-384 inc 5)

- Re-read after export keeps ≥2 `MTEXTOBJECTCONTEXTDATA` and ≥2 `ALDIMOBJECTCONTEXTDATA` for two-scale samples.
- Export loss lists hatch context limitation when annotative hatches are present.
