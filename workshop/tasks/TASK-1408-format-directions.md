# TASK-1408 — Confirm file-format directions

**Issue:** [#1408 — Confirm the direction of each point-cloud and IO format](https://github.com/chetjones003/GoSurvey/issues/1408)

## Plan

- **Requirement authority:** accepted REQ-170 through REQ-174 and `spec/file-format-specs.md` §§3.1–3.4. The issue acceptance condition is a verified read/write/both list.
- **Files/subsystems:** audit the format rows in `spec/file-format-specs.md` against the current GoSurvey source; record the implementation direction beside the accepted target direction here.
- **Approach:** keep current implementation separate from the accepted product target. A reader or writer is counted only when a current code path is present; a planned requirement is not described as shipped behavior.
- **Verification:** inspect the import/export entry points and format implementations. No code behavior changes are in scope.
- **Architectural boundary:** documentation only; no format, API, dependency, or architecture decisions.

## Verified format directions

“Current” means a format path is present in this checkout. “Target” is the accepted direction in the File Format Specs requirements; it does not claim that the capability is already implemented.

| Format | Current direction | Accepted target direction | Evidence / notes |
|---|---|---|---|
| DWG | Read and write | Read and write | `src/io/DwgIo.cpp`; REQ-170. DWG is the drawing document; GoSurvey payload data is embedded in the DWG. |
| DXF | Read and write | Read and write | `src/io/DxfIo.cpp`; REQ-170. |
| `.gs` | Neither as a standalone drawing | Neither as a standalone drawing | Retired as a standalone document format; its serializer is used for the embedded DWG payload. REQ-175. |
| `.gst` | Read and write | Read and write (workspace template) | `LoadGoSurveyTemplateFile` / `SaveGoSurveyTemplateFile` in `src/io/GsIo.cpp`; workspace template, not an openable drawing. |
| E57 | Read only | Read and write | `src/io/CadPointCloudE57.hpp` exposes readers/streaming readers; `src/qt/EditorPointClouds.cpp` explicitly says E57 cannot be exported. REQ-172 still targets both. |
| LAS | Read and write | Read and write | `src/io/CadPointCloudLas.cpp`; `IMPORTLAS` / `EXPORTLAS`; REQ-172. |
| LAZ | Read and write | Read and write | `src/io/CadPointCloudLaz.cpp`; `IMPORTLAZ` / `EXPORTLAZ`; REQ-172. |
| PTS | Read and write | Read and write | `src/io/CadPointCloudPts.cpp`; `IMPORTPTS` / `EXPORTPTS`; REQ-172. |
| PTX | Read and write | Read and write | `src/io/CadPointCloudPtx.cpp`; `IMPORTPTX` / `EXPORTPTX`; REQ-172. |
| JPEG/JPG | Not implemented as an IMAGE underlay | Read and write | REQ-173 target. No IMAGE underlay implementation was found in `src`; LibreDWG explicitly logs IMAGE references as skipped in `src/io/LibreDwgCad.cpp`. |
| PNG | Not implemented as an IMAGE underlay | Read and write | REQ-173 target; same evidence as JPEG/JPG. |
| BMP | Not implemented as an IMAGE underlay | Read and write | REQ-173 target; same evidence as JPEG/JPG. |
| IFC | No IFC importer or writer found | Read only (view) | REQ-174 target; no IFC importer/library integration was found in `src` or `CMakeLists.txt`. |

Standalone `.gs` is not an interchange format: its serializer is used inside the DWG payload and for `.gst` workspace templates, per `spec/file-format-specs.md` §1 and REQ-175. The table lists `.gs` as retired and `.gst` separately as a template format.

## Completion report

- **Acceptance:** the point-cloud and File Format Specs matrix formats are listed with verified current directions and accepted target directions.
- **Findings:** E57 is currently read-only despite REQ-172's read/write target. The raster IMAGE-underlay and IFC-view targets have no implementation path in this checkout. These are implementation gaps against the accepted targets, not changes to those targets.
- **Pre-PR product documentation review:** `README.md` — reviewed, no update needed; `PRIVACY.md` — reviewed, no update needed; `resources/wiki/` — reviewed (including `Import-and-Export.md`), no update needed because this inventory changes no product behavior; public user manual wiki, `Import-and-Export` — reviewed, no update needed for the same reason.
- **Tests/build:** not run; this change records source-backed format directions and changes no executable code.
- **Assumptions/debt:** none. This inventory is limited to formats in `spec/file-format-specs.md` §§3.1–3.4; it does not claim to be a catalog of every unrelated application import/export workflow.
- **Result:** PASS — documentation acceptance condition met. The listed implementation gaps remain governed by REQ-170–REQ-174.
