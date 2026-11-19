# DWG inventory: versions and object types the current app reads and writes

Issue: #1407 (parent #1403, Rust and Tauri port). Source of truth for this list is the Qt app's code
at the time of writing, read from `src/io/`. Every item below names the file that does the work.

## Method

- **Versions:** `src/io/DwgProbe.cpp` (tag table, export rows), `src/io/DwgIo.hpp` (save versions),
  `src/io/LibreDwgCad.cpp` (`LoadDwgData`, `LibreDwgVersionFromExport`).
- **Read:** `ImportObject` in `src/io/LibreDwgCad.cpp` (the model-space dispatch), the paper-space
  branch in `ImportPaperEntity`, and the `NoteSkip` labels. A `NoteSkip` label means the app reads
  nothing from that object and reports it.
- **Write:** the `dwg_add_*` calls in `src/io/LibreDwgCad.cpp` and the LibreDWG API they use.
- **Decoder:** GNU LibreDWG 0.14, vendored with local patches (`third_party/libredwg/VENDORED.md`).

## Versions

| Version | Tag | Read | Write (save choice) | Notes |
|---|---|---|---|---|
| R10 / R11-12 / R13 / R14 | AC1006 / AC1009 / AC1012 / AC1014 | Not refused by version. LibreDWG decodes them if it can. | No | `DwgVersionNameFromTag` names them. Decoding not tested here. |
| R2000 | AC1015 | Yes | Yes | Default for the "AutoCAD 2000" save row. Nearest ACI colours only (no true colour). |
| R2004 | AC1018 | Yes | Yes | True colour and R2004+ entity encoding. Dynamic-block and field objects allowed. |
| R2007 | AC1021 | Named and decoded by LibreDWG, no version gate | **No** | `DwgIo.hpp` omits it: "separate container; features exist in R2010+". |
| R2010 | AC1024 | Yes | Yes | Native MULTILEADER, lights, materials, visual styles, annotation contexts. |
| R2013 | AC1027 | Yes | Yes | Same write features as R2010. |
| R2018 | AC1032 | Yes | Yes | Default save version (`CadCommands.hpp`). DXF export always writes R2018. |
| Unknown `AC` tag | other | Named "unknown DWG (tag)" and decoded if possible | No | Refused only if LibreDWG returns a critical decode error. |

**Refusal today:** an unsupported version is not refused by the app. The only refusal is a LibreDWG
critical decode error (`LoadDwgData`). Issue #1442 asks for the refusal and is still open.

## Object types

### Entities on read (`ImportObject`, `src/io/LibreDwgCad.cpp`)

| Object type | Read | Write | Notes and tracking |
|---|---|---|---|
| LINE, ARC, CIRCLE, ELLIPSE, LWPOLYLINE, POLYLINE_2D/3D, VERTEX | Yes | Yes (LINE, ARC, CIRCLE, ELLIPSE, LWPOLYLINE, POLYLINE_3D) | Degenerate arcs and circles skipped with a report. Trimmed ellipses reported. Children: #1444, #1445, #1446. |
| TEXT, MTEXT | Yes | Yes | Child: **#1526** (new). |
| FIELD (live text objects) | No (not read; `LibreDwgField.cpp` only creates them) | Yes (R2004+) | Child: **#1526** (new). |
| HATCH | Yes (solid and pattern) | Yes | Gradient fills and degenerate boundaries skipped with a report. Child: **#1527** (new). |
| DIMENSION: Aligned, Linear, Angular 3-point | Yes | Yes (Aligned, Linear, Angular 3-point) | Child: **#1528** (new). |
| DIMENSION: Radius, Diameter, Ordinate, 2-line Angular | Skipped (reported) | No | Explicitly unsupported subtypes. Keep as reported skips. |
| LEADER, MULTILEADER | Yes (MULTILEADER block content and unsupported layouts reported) | Yes (MULTILEADER R2010+; LEADER plus MTEXT before) | Child: **#1528** (new). |
| TABLE | Yes (degenerate skipped) | Grid LINEs plus cell MTEXT (LibreDWG has no `dwg_add_TABLE`) | Child: **#1528** (new). |
| SPLINE | Yes (degenerate reported) | Yes (fit-point curves) | Child: **#1529** (new). |
| POINT | Yes | No | Child: **#1529** (new). |
| SOLID (2D), TRACE | Yes (degenerate reported) | No | Child: **#1529** (new). |
| 3DFACE, POLYLINE_MESH, POLYLINE_PFACE | Yes (degenerate reported) | Yes (3DFACE, POLYLINE_PFACE) | Child: **#1529** (new). |
| INSERT, ATTDEF, ATTRIB | Yes (block refs, attributes) | Yes | Blocks and inserts: #1448. |
| 3DSOLID (ACIS SAT/SAB) | Yes (converted; rotated/scaled placement and Civil 3D catalog parts reported) | Yes (from SAT text) | Read: #1509, #1510. Write: **#1531** (new). |
| Solid primitives BOX, WEDGE, PYRAMID, CYLINDER, CONE, SPHERE, TORUS | No (read as 3DSOLID) | Yes | Write: **#1531** (new). |
| IMAGE, WIPEOUT, PDF/DWF/DGN underlays | Reported as skipped ("reference not imported") | No | Parity means keep the report. No new ticket. |
| VIEWPORT, LAYOUT, paper-space entities | Yes (paper space read via `ImportPaperEntity`) | Yes (LAYOUT, VIEWPORT) | Child: **#1530** (new). |
| Plant 3D custom objects (AcPp*) | Refused, reported with the Plant class name | No | Refusal: #1458. Reading AcDs ASM bodies: #786. |
| Anything else (REGION, BODY, OLE2FRAME, RAY, XLINE, PROXY objects, unknown types) | Skipped and reported by DXF name | No | Keep as reported skips. |

### Objects and tables

| Object | Read | Write | Notes |
|---|---|---|---|
| LAYER, LTYPE | Yes | Yes | Child: #1447. |
| STYLE (text style table) | Yes | Yes | Child: **#1526** (new). |
| DIMSTYLE | Yes | Yes | Child: **#1528** (new). |
| BLOCK_HEADER, BLOCK, ENDBLK | Yes | Yes | Child: #1448. |
| APPID, DICTIONARY, XRECORD, class records | Yes | Yes | Used for survey XDATA and app data (#1470). |
| GEODATA | Yes | Yes (R2010 layout) | Child: #1466. |
| SCALE (annotation scale list) | Yes | Yes (R2007+) | Child: **#1528** (new). |
| VISUALSTYLE, VPORT visual style | Yes | Yes (R2007+) | Child: #1456. |
| MATERIAL | Yes | Yes (R2007+; native appearance R2010+) | Child: #1455. |
| LIGHT, SUN, LIGHTLIST | Yes | Yes (R2010+) | Child: #1454. |
| BLOCKFLIP*, BLOCKLINEAR*, BLOCKSTRETCHACTION, EVALUATION_GRAPH | Yes | Yes (R2004+) | Dynamic blocks: #1457. |
| FIELDLIST | No | Yes (R2004+) | Child: **#1526** (new). |
| CONTEXTDATA (annotation context objects) | Not checked | Yes (R2010+) | Child: **#1528** (new). |
| Survey points (XDATA on POINT, `GOSURVEY`) | Yes | Yes | Child: #1470. |
| Unknown objects | Preserved on save | Preserved | Child: #1468. |

### Import behaviour that must survive the port

- Every skipped object is reported by name (`NoteSkip`). A silent skip is a bug (REQ-201 history).
- Plant 3D objects are refused with a message, not read.
- Civil 3D AECC catalog parts keep their 2D content and refuse the 3DSOLID with a named reason.

## Gaps

Six object families are read or written today and have no open child under #1403. Tickets added:

- **#1526** TEXT, MTEXT, FIELD objects and text styles
- **#1527** HATCH fills
- **#1528** Dimensions, leaders, multileaders, tables, dimension styles, annotation scales and contexts
- **#1529** SPLINE, POINT, 2D SOLID, TRACE, 3DFACE and polyface meshes
- **#1530** Layouts, paper space and viewports
- **#1531** ACIS 3D solids and solid primitives on write

Not ticketed, on purpose: items the current app already reports as skipped (underlays, gradient
hatches, radial and ordinate dimensions, REGION, BODY, OLE, RAY, XLINE, PROXY). Parity means keeping
the report, not reading them.

## Open questions for the owner

1. **Which versions does the port read?** The app reads whatever LibreDWG decodes, from R10 up.
   The user stories in #1403 name R2000 to R2018. Recommendation: read R2000 to R2018, and refuse
   older versions with a message (this is what #1442 asks for). R2007 is read by the Qt app but not
   saved; the port should match that.
2. **Is R2007 save needed?** The Qt app does not write R2007. Recommendation: no, unless a user asks.
