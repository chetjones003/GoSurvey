# TASK-300 — DWG save: 3D solids and pipe runs as ACIS 3DSOLID (issue #612)

- Type:    feature (amends ADR-045 (i), REQ-170 / REQ-201)
- Status:  complete (pending PR merge — closes #612)
- Opened:  2026-10-01
- Owner:   Workshop
- GitHub:  #612

## 1. Authority

- **D-2026-10-01-d** — user chose option 1: write ACIS SAT into `3DSOLID` (not polyface mesh).
- **ADR-045 (i)** — amended: DWG export writes exportable `CadSolid` / pipe-run geometry as
  LibreDWG `3DSOLID` (ACIS SAT); solids that cannot be encoded remain in the REQ-201 loss list.
- **REQ-201** — pipe runs must not drop silently (fix loss scan + writer).
- **REQ-300** — no vendored ACIS kernel; reuse LibreDWG `dwg_add_*` / `dwg_add_3DSOLID` and in-tree
  `acissat::ExportSatSolid` (inverse of ADR-051 import scope).

## 2. Approach

1. **`FillFromState`** — emit one `3DSOLID` per `cadSolids` entry and per pipe-run swept solid
   (`CadBuildPipeRunSolids`), applying layer/colour like other entities.
2. **Fast path** — `brep::Recipe` with a known `PrimitiveKind` → LibreDWG `dwg_add_BOX` /
   `CYLINDER` / `CONE` / `SPHERE` / `TORUS` / `WEDGE` / `PYRAMID` (AutoCAD-compatible ASM SAT).
3. **General path** — `acissat::ExportSatSolid` → `dwg_add_3DSOLID` for topology within ADR-051
   export scope (same analytic surfaces / loop rules as import).
4. **`ComputeDwgExportLossesImpl`** — count only solids/runs that the writer refused, not the whole
   store.

## 3. Tests

- `[issue612]` LibreDwgCad: box + cylinder via recipe export, `dwg_read` + `3DSOLID` count.
- `[issue612]` pipe run straight segment → at least one `3DSOLID` in output.
- `[issue612][acissat]`: round-trip `MakeBox` / `MakeCylinder`, straight + L-shaped `CadBuildPipeRunSolids` through Export → Import (real ASM header).

## 4. Verification

- `./dev/build`, `./build/GoSurveySnapTests.exe "[issue612]"`, `[libredwg]`.
