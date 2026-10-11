# TASK-302 — DWG save: 24-bit true colours on R2004 (issue #615)

- Type:    feature (REQ-170 / issue #601 Group B)
- Status:  complete (merged PR #641)
- Opened:  2026-10-01
- Owner:   Workshop
- GitHub:  #615

## 1. Authority

- **#615** — custom `#RRGGBB` entity and layer colours must survive R2004+ DWG save/open; R2000
  keeps nearest ACI.
- **REQ-170** — CAD fidelity; import already maps `DWG_COLOR_METHOD_TRUECOLOR` via `ColorToStorage`.
- Depends on **#600** (R2004 write path).

## 2. Approach

1. **`SetCmcFromStorage` / `ApplyLayerTableColor`** — write `0xc3` + `0xC3000000|rgb` when
   `st.dwgExportVersion == R2004` and RGB is not an exact ACI palette match; else ACI as today.
2. **`TableWriter::useTrueColor`**, **`DimStyleSetCmc(..., useTrueColor)`**.
3. **`ComputeDwgExportLossesImpl`** — colour-rounding degradation only when export is R2000; include
   layer-table colours in the scan.

## 3. Tests

- `[issue615]` R2004 round-trip `#1E90FF` entity + `#8B4513` layer; R2000 rounds; loss list clean
  on R2004.

## 4. Verification

- `./dev/build`, `./build/GoSurveySnapTests.exe "[issue615]"`, `[libredwg]`.
