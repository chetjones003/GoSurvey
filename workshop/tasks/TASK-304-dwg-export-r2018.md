# TASK-304 — DWG export through R2018 (AC1032)

- Type:    feature (REQ-170, D-2026-10-01-f, issue #600 extension / #601 enabler)
- Status:  complete (merged PR #643)
- Opened:  2026-10-01
- Branch:  `feat/dwg-export-r2018`

## Goal

Let Export DWG write **R2000, R2004, R2010, R2013, R2018** so R2010+ container features (GEODATA v2,
later native MESH / POINTCLOUDEX, etc.) are possible. **R2007 omitted.**

## Delivered

- `DwgSaveVersion` + helpers in `DwgIo.hpp` / `DwgProbe.cpp`
- Export dialog rows all selectable through 2018
- `LibreDwgVersionFromExport` + `ExportLibreCadFile` encode path
- `WriteDwgGeoData` uses R2010+ layout when target ≥ R2010
- `LibreDwgWriteMinimalAtVersion` smoke API
- Tests: minimal LINE R2010/2013/2018, full export R2018, `[issue623]` on R2018 (class_version 2)

## Verification

- `./dev/build`
- `GoSurveyTests.exe "[libredwg]"` (includes new DwgProbe export-row case)
- `GoSurveySnapTests.exe "[libredwg]"`, `"[r2018]"`, `"[issue623]"`

## Follow-ups (not this task)

- Default export policy still R2000 unless user changes
- Native MESH writer when LibreDWG exposes `dwg_add_MESH`
- Group B #617–#619, #622, #624 (separate issues)
