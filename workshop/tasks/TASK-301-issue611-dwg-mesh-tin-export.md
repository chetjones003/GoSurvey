# TASK-301 — DWG save: meshes and TIN surfaces as POLYLINE_PFACE (issue #611)

- Type:    feature (amends ADR-026 (c), ADR-028 (f), REQ-170 / REQ-201)
- Status:  complete (pending PR merge — closes #611)
- Opened:  2026-10-01
- Owner:   Workshop
- GitHub:  #611

## 1. Authority

- **D-2026-10-01-e** — DWG export writes triangle meshes and built TIN surfaces as
  `POLYLINE_PFACE` via LibreDWG `dwg_add_POLYLINE_PFACE` (R2000/R2004). Native subdivision
  `MESH` deferred until R2010+ encode and `dwg_add_MESH` exist in LibreDWG (`HAVE_NO_DWG_ADD_MESH`).
- User's #611 preference was option 2 (MESH); this increment delivers visible triangles in AutoCAD
  on supported save versions without blocking on R2010.
- **REQ-201** — only meshes/surfaces that cannot be encoded stay in the loss list.

## 2. Approach

1. **`FillFromState`** — one `POLYLINE_PFACE` per exportable `CadMesh` and per `CadSurface` with a
   non-empty `tin`.
2. **`ComputeDwgExportLossesImpl`** — count only skipped meshes (no triangles) and surfaces (no tin).

## 3. Tests

- `[issue611]` export mesh + TIN → `dwg_read` finds `POLYLINE_PFACE`; loss list omits them.
- Round-trip import → `CadMesh` triangle count preserved (same path as #613).

## 4. Verification

- `./dev/build`, `./build/GoSurveySnapTests.exe "[issue611]"`, `[libredwg]`.
