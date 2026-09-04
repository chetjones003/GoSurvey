# File Format Specs

> Product interchange matrix for GoSurvey. **Authority:** this file plus REQ-170–REQ-174,
> ADR-041, and ADR-042. Implementation does not start until those requirements are
> sequenced on the roadmap; this document does not excuse code.
>
> Recorded **2026-08-29** (D-2026-08-29-g). User title: **File Format Specs**.

---

## 1. What this epic is

GoSurvey reads and writes the formats a survey CAD session actually exchanges **without**
Autodesk ODA membership and **without** Leica/Autodesk native scan-project SDKs.

| Role | Formats | Direction |
|------|---------|-----------|
| CAD codec | DWG, DXF (ASCII and binary) | **LibreDWG** is the codec (ADR-041) |
| Scan interchange | E57, LAS, LAZ, PTS, PTX | read **and** write |
| Rasters | JPEG, PNG, BMP | drawing **IMAGE** underlays; file read/write |
| BIM | IFC | **view only** (tessellate to REQ-063 meshes) |

`.dwg` is the drawing document (REQ-175 / ADR-044).

**`.gs` is retired (2026-09-03, D-2026-09-03-h).** The standalone GoSurvey JSON file, its `GsIo`
read/write path, workspace-template `.gs`, the version-migration code and the `kGsFormatVersion`
discipline are all to be removed from source. No feature spends effort on `.gs` from this date; a
requirement that reads "a `.gs` save/load round-trips X" is read as "the drawing file round-trips
X". The GoSurvey JSON document serializer (`BuildRoot` / `LoadGoSurveyFromJsonUtf8`) is **also**
slated for removal, but only after a native DWG-embedded representation of every survey-specific
domain field replaces the ADR-044 JSON trailer — until that ships, removing the serializer would
silently drop survey points, styles, point groups, paper layouts and 3D solids on every save,
which REQ-001 / REQ-201 forbid. See GitHub issue #264 for the removal order.

---

## 2. Binding product decisions

1. **LibreDWG is the DXF/DWG codec.** Not a from-scratch bit codec, not ODA Drawings SDK, not
   the Phase 1 external converter as the long-term path.
2. **DWG write in this epic is R2000 (AC1015) and R2004 (AC1018) only.** Default save is
   **R2004**. R2007+ write is out. AutoCAD 2018–2027 still open R2004 files.
3. **DWG read** is every version LibreDWG decodes (through R2018 / AC1032, including current
   AutoCAD which still writes AC1032). Objects LibreDWG skips are named in the import log
   (REQ-201), not silently absorbed (REQ-001).
4. **This epic does not deliver unknown-object preservation** (dwg-plan DM-08 / D3). Saving an
   R2018 Civil 3D drawing as R2004 is a **down-convert**: proxies, annotative contexts, and
   classes that do not exist in R2004 are dropped and **listed before write**. Phase 1’s
   honesty rule stays.
5. **GoSurvey is GPL-3.0-or-later** while it links LibreDWG (static or dynamic). That is a
   product licence change, not a comment in `third_party/`.
6. **No native** LGS, LGSX, BLK, BLKX, IMP, Leica BIN, PTG, or Autodesk RCP/RCS in this epic.
   Interchange for those workflows is **export E57/LAS from the vendor tool**.
7. **No IFC write.** No Plant 3D / Civil 3D custom-object decode (ADR-026 still holds).
8. **Point clouds are not TIN sources** in this epic (REQ-068 D4 / LiDAR density stays out).
   A cloud is reference geometry, like a mesh.

---

## 3. Format matrix

Legend: **R** = read into GoSurvey, **W** = write from GoSurvey, **—** = out of this epic.

### 3.1 CAD

| Ext | R | W | Codec | Notes |
|-----|---|---|--------|--------|
| `.dwg` | yes | R2000 / R2004 | LibreDWG | Open does not require ODA File Converter or AutoCAD. Save default AC1018. Written file must open in AutoCAD **without Recover** for the entity set we emit. |
| `.dxf` | yes | yes | LibreDWG | ASCII and binary DXF as LibreDWG supports them. Replaces `DxfIo` as the **interchange** path once REQ-170 is verified; Phase 1 converter may remain a **test oracle**. |
| `.gs` | — | — | (removed) | Retired 2026-09-03, D-2026-09-03-h. Standalone `.gs` file, `GsIo`, workspace-template `.gs` and `kGsFormatVersion` all leave source. The GoSurvey JSON *document* survives only as the ADR-044 in-`.dwg` trailer, itself pending replacement by a native representation. |

**Mapped into the GoSurvey domain** (lossy where the domain has no type): LINE, CIRCLE, ARC,
ELLIPSE, POINT, LWPOLYLINE / POLYLINE, TEXT, MTEXT, HATCH, DIMENSION (kinds we already
model), LAYER / LTYPE / STYLE tables, survey-point **GOSURVEY** XDATA/EED (ADR-005). INSERT is
a **reference** only after REQ-107 is accepted and built; until then INSERT continues to
explode (log the count). VIEWPORT / extra LAYOUTS: import what REQ-037 can store; extra
layouts beyond one paper store are logged until a layouts REQ exists. Meshes, TIN surfaces,
point clouds, and PDF underlays are **not** written to DWG/DXF in this epic (same logged
exclusion as REQ-068).

### 3.2 Point clouds

| Ext | R | W | Library | Notes |
|-----|---|---|---------|--------|
| `.e57` | yes | yes | **libE57Format** (BSL-1.0) | ASTM E57. Cartesian XYZ; colour and intensity when present. |
| `.las` | yes | yes | in-tree LAS 1.2/1.4 **or** LASlib if the in-tree reader cannot cover the files we hold | Public ASPRS spec. |
| `.laz` | yes | yes | **LASzip** (Apache-2.0) | Compressed LAS; decompress to the same point schema. |
| `.pts` | yes | yes | **in-tree** | Cyclone-style ASCII. No delimiter sniffing (same spirit as REQ-083). |
| `.ptx` | yes | yes | **in-tree** | ASCII; **one cloud per scan setup** with the 4×4 transform applied into world (local + `worldDocumentOrigin` rules, REQ-101). |

Malformed / truncated files refuse and leave the drawing unchanged (REQ-001).

### 3.3 Rasters

| Ext | R | W | Library | Notes |
|-----|---|---|---------|--------|
| `.jpg` / `.jpeg` | yes | yes | existing **stb_image** + write helper | IMAGE underlay (ADR-042), not a photo album. |
| `.png` | yes | yes | same | |
| `.bmp` | yes | yes | same | |

Georeference: insertion point (world XY), rotation, and scale (width/height in drawing units).
Path is stored; missing file on reload is a logged unload, not a crash (PDF-underlay precedent).

### 3.4 BIM

| Ext | R | W | Library | Notes |
|-----|---|---|---------|--------|
| `.ifc` | view | **no** | **IfcPlusPlus** (MIT) preferred; IfcOpenShell+OCCT only if tessellation of the files we care about cannot be done without it | One or more REQ-063 meshes. Names/colours when the parser yields them; otherwise one grey mesh and a log line. Schema IFC2x3 and IFC4 as the chosen library supports. |

---

## 4. Explicitly out of scope (this epic)

- ODA Drawings SDK / Scan-to-BIM / Civil / Map / Mechanical extensions
- LibreDWG write of R2007, R2010, R2013, R2018
- Native LGS, LGSX, BLK, BLKX, IMP, Leica `.bin`, PTG
- Autodesk RCP / RCS (ReCap is Autodesk, not Leica)
- IFC authoring or round-trip
- Plant 3D / Civil 3D object enablers (ADR-026)
- Point-cloud snapping, classification, or “surface from cloud”
- Making `.dwg` the native document (PART 11)

---

## 5. Dependencies (REQ-300 answers)

| Dependency | In-tree? | Maintained? | Solves today? | Decision |
|------------|----------|-------------|---------------|----------|
| GNU **LibreDWG** (GPL-3.0-or-later) | No — that is Route A, months | Yes | Yes — DXF/DWG codec | **Vendor.** Relicenses the linked application GPL-3. |
| **libE57Format** | Possible but large | Yes | Yes | **Vendor** (BSL). |
| **LASzip** | No (compression) | Yes | Yes — LAZ | **Vendor** (Apache-2.0). |
| LAS 1.2/1.4 reader | Yes | n/a | Yes | **In-tree first**; LASlib only if in-tree fails real files. |
| PTS / PTX | Yes | n/a | Yes | **In-tree.** |
| **stb_image** | already vendored | Yes | IMAGE decode | Keep. Add write path. |
| **IfcPlusPlus** | No | Yes | IFC view | **Vendor** (MIT). OCCT is **not** added unless a recorded follow-up proves we need it. |

Phase 1 `FindDwgConverter` / `ProcessRun` stay in the tree until REQ-170’s native open/save
acceptance is met; they are not the user-facing codec after that.

---

## 6. Delivery order (Workshop must not flatten this)

1. Vendor LibreDWG on MSVC + Ninja; version-tag probe + R2004 write of a **tiny** drawing that
   AutoCAD opens (REQ-170 increment 1).
2. Map LibreDWG → existing CAD stores for the entity set in §3.1; retire converter from File
   Import/Export when that mapping matches Phase 1 oracle on a golden file (REQ-170).
3. Point-cloud entity + PTS (smallest parser) (REQ-171, REQ-172).
4. PTX, then LAS, then LAZ, then E57 (REQ-172).
5. IMAGE underlay JPG/PNG/BMP (REQ-173).
6. IFC → mesh (REQ-174).

Each increment has its own tests (happy path + malformed refusal). No single PR dumps all
libraries.

---

## 7. Open follow-ups (not this epic)

- DM-08 pass-through / R2018 write
- REQ-107 blocks as INSERT on import/export
- Multiple paper layouts
- DWG as native format — **done as REQ-175** (JSON payload trailer; not per-field EED/XRECORD)
- REQ-100 point-cloud frame-budget profile (needed before huge E57 files are a product claim)
- LGSX read via Leica SDK, RCP via Autodesk Reality SDK
