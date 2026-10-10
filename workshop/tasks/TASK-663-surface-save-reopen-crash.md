# TASK-663 — Intermittent SIGSEGV on surface save/reopen

**Issue:** GitHub #663  
**Authority:** Issue characterization (headless transcripts req069/req070)

## Root cause

LibreDWG `dwg_add_POLYLINE_PFACE` crashes when a second large TIN is exported into the same model-space block during `ExportDwgFile`. Save/reopen round-trips embed the GoSurvey JSON trailer; the crash happened in the LibreDWG write before the file completed.

Secondary: `ClearCadGeometry` did not clear `surfaceDisplayCache`, so stale display cache could survive OPEN (ADR-036).

## Fix

1. `ClearCadGeometry`: clear surface caches and cancel async rebuild jobs on geometry reset.
2. `LibreDwgCad.cpp`: allow one POLYLINE_PFACE per export; further TIN/mesh exports use per-triangle 3DFACE (JSON trailer unchanged).
3. `libredwg` `dwg_add_POLYLINE_PFACE`: link last face vertex to previous owned vertex (VENDORED.md #7).
4. Regression: `LibreDwgCadTests` `[issue663]` — 100× export with two demo TINs.

## Verification

- `GoSurveySnapTests.exe "[issue663]"` — PASS
- req069 / req070 headless transcripts ×20 — 0 crashes
