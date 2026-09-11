# Import and Export

| Format | Import | Export | Where |
|---|---|---|---|
| **`.gs`** — GoSurvey drawing | ✔ | ✔ | File → Open / Save |
| **DXF** (ASCII) | ✔ | ✔ | File → Import DXF… / Export DXF… |
| **DWG** | ✔ *(converter)* | ✔ *(converter)* | File → Import DWG… / Export DWG… |
| **CSV** — survey points | ✔ | ✔ | `IMPORTPOINTS` / `EXPORTPOINTS` |
| **JSON** — survey point database | ✔ | ✔ | Viewpoints / Create points panel |
| **FBK** — Autodesk Field Book | ✔ | — | Traverse Editor → Import .fbk… |
| **PDF** | ✔ *(as underlay)* | ✔ *(as a plot)* | `PDFATTACH` / Layout ribbon → Plot |
| **glTF / GLB** | ✔ | — | `IMPORTMODEL` |
| **STL** | ✔ | — | `IMPORTMODEL` |

---

## DXF

The main exchange route with AutoCAD, Civil 3D, and everything else.

### Export — File → Export DXF…

Writes **ASCII DXF tagged AC1032** (AutoCAD 2018 class).

| Section | Contents |
|---|---|
| **HEADER** | Drawing limits, extents, system variables, `$HANDSEED`, `$INSUNITS` from the `UNITS` insertion-units setting, and — when survey points are present — `$PDMODE` / `$PDSIZE` so POINT entities display as an X |
| **TABLES** | LAYER (with plot-style pointers), VIEW, UCS, VPORT, APPID, DIMSTYLE, BLOCK_RECORD with canonical `*Model_Space` / `*Paper_Space` names |
| **ENTITIES** | Lines, circles, arcs, ellipses, polylines, text, mtext, aligned dimensions, with layers, ACI colours, lineweights, and model-space ownership |
| **OBJECTS** | A minimal named-object dictionary, so hosts like Civil 3D accept the file |

**Survey points** are written as native `POINT` / `AcDbPoint` entities at world coordinates, with
per-point layer and ByLayer colour. Each carries its identity in `GOSURVEY` **XDATA** — point ID,
label style, description — so a GoSurvey DXF round-trips with full survey identity, while any other
DXF reader still sees a plain valid POINT.

The log reports `DXF export — wrote <path>`. Anything excluded is stated:
`DXF export — excluded …`.

> **Known gaps.** Aligned dimensions are written as **exploded lines plus text**, not associative
> `DIMENSION` entities. Some entity types have no export branch yet and are silently absent — check
> the log line that reports exclusions, and see [[Known Limitations]].

### Import — File → Import DXF…

Brings in model-space geometry: lines, circles, arcs, ellipses, polylines, and common annotations.

| Behaviour | Detail |
|---|---|
| **Paper space** | `N paper-space-only ENTITIES (group 67); layouts/title blocks not imported.` |
| **Unsupported types** | `N unsupported ENTITIES record(s).` — reported, never silently dropped |
| **No ENTITIES section** | Falls back to the `*MODEL_SPACE` block and says so |
| **Large coordinates** | State-plane values are imported at **full precision**; an internal local origin is set automatically |
| **View** | Zoom extents is applied after import |

The log prints the imported bounding box, and a robust box that excludes outliers, so a stray
entity at a wild coordinate is visible immediately.

**Binary DXF is not supported** — `Binary DXF is not supported — in AutoCAD use Save As → ASCII
DXF.`

### Survey points on DXF import

| Case | Result |
|---|---|
| A `POINT` **with** GoSurvey XDATA | Rebuilt as a survey point with ID, coordinates, elevation, description, layer, and label style; its label is re-linked |
| A `POINT` **without** that XDATA — from another program | Imported as a small snappable **cross marker**, unchanged |

**Importing a DXF keeps the survey points already in the session.** Only the CAD geometry is
replaced. So importing CSV points then a DXF, or a DXF then points, both keep everything.

Reconstructed points are **merged** with the existing ones. Non-colliding IDs are added directly; a
colliding ID opens a prompt to **overwrite** the existing point or **offset** the imported IDs —
`N imported survey point ID(s) conflict with existing points; choose overwrite or offset.`

### Civil 3D

Civil 3D **COGO points are custom objects** and are not written as standard DXF entities, so they
cannot be recovered from a plain DXF, no matter which program reads it.

**The route that works:** export the points from Civil 3D as a **PNEZD or PENZD CSV** and use
`IMPORTPOINTS`; use DXF for the linework. Either order is safe.

The same applies to Plant 3D drawings, whose contents are largely `AcPp*` custom objects requiring
Autodesk's object enabler. No third-party reader can reach them.

---

## DWG

**File → Import DWG… / Export DWG…**

DWG goes through an **external converter**. The menu items stay disabled until one is found:

> *DWG needs a converter: install the free ODA File Converter, or set `GOSURVEY_DWG_CONVERTER` to
> ODAFileConverter.exe or accoreconsole.exe.*

| Converter | Notes |
|---|---|
| **ODA File Converter** | Free, from opendesign.com |
| **accoreconsole.exe** | Present on machines with AutoCAD installed |
| **`GOSURVEY_DWG_CONVERTER`** | Environment variable pointing at either executable |

Hovering an enabled menu item shows which converter is in use.

### Export warning

Because DWG is written **through DXF**, export is lossy, and GoSurvey says so before it writes:

> *GoSurvey writes DWG through DXF, so this export drops:*
> - block definitions and inserts (geometry is written exploded)
> - paper-space layouts beyond the first
> - elevations, block attributes, multileaders and tables
> - Civil 3D objects, proxies and anything else GoSurvey does not model

If the target file already exists, the dialog escalates:
*"\<file\> already exists. Overwriting it will permanently discard the data listed above."* with an
**Overwrite** button.

Import is subject to the same modelling limits in reverse.

Failures name the cause — a missing converter, a timeout, a file that is not a DWG
(`is not a DWG file (no AC#### format tag)`), an empty output, or a converter exit code.

> Native DWG reading, with no converter, is planned.

---

## Survey point CSV

`IMPORTPOINTS` / `IMPPTS` and `EXPORTPOINTS` / `EXPPTS`.

| Preset | Columns |
|---|---|
| `P,N,E,Z,D` | Point ID, northing, easting, Z, description |
| `P,E,N,Z,D` | Point ID, easting, northing, Z, description |
| `N,E,Z` | Northing, easting, Z — IDs assigned on import |
| `E,N,Z` | Easting, northing, Z — IDs assigned on import |

Import gives a live preview, a validation summary, an optional header-row skip, and a per-row
problem report. Export offers the same presets plus an optional header row, and adds a tab to the
**Reports** panel.

CSV coordinates are **world** values, converted into the drawing's frame in double precision.

Full detail: [[Survey Points]]

---

## Survey point JSON

The Create points panel and the Viewpoints table both have **Save** and **Load** buttons that write
and read the whole point database as JSON, defaulting to `gosurvey_points.json`. Useful for moving
a point set between drawings without going through CSV.

---

## FBK — Autodesk Field Book

**Traverse Editor → Import .fbk…**

> *Import an Autodesk Field Book (.fbk) raw data file. Replaces the current traverse with the
> imported stations, backsight and observations.*

**This replaces the current traverse.** Sample files are in the repository under `samples/`.

Full detail: [[Traverse Editor]]

---

## 3D models — `IMPORTMODEL`

**Command:** `IMPORTMODEL`, `GLTF`, `IMPORT3D`

Imports a mesh as **reference geometry**.

| Extension | Reader |
|---|---|
| `.glb`, `.gltf` | Built in |
| `.stl` | Built in |
| `.dwg` | Through the DWG converter — extracts 3D solids as a mesh |

### Usage

```
IMPORTMODEL                                          ← file browser, unit scale 1 at 0,0,0
IMPORTMODEL "C:\models\tank.glb" 0.0833 1000 2000 0  ← path, scale, insertion X Y Z
```

The bare form opens a file browser and states what it assumed:
*IMPORTMODEL — unit scale 1, insertion 0,0,0. Use IMPORTMODEL "\<path\>" \<scale\> \<x\> \<y\> \<z\>
to place it otherwise.*

A DWG import warns first — *Converting the DWG's 3D solids via \<converter\> — this can take a few
minutes on a large model.*

On success:

```
Imported tank.glb — 184320 triangles, 12 parts, scale 0.0833.
```

**Anything not imported is named**, never dropped in silence:

```
Not imported (geometry only): materials, animations, cameras.
```

The unit scale must be a non-zero finite number.

Set the visual style to **Shaded** and use `ORBIT` to look at an imported model. See
[[Views and Navigation]].

---

## PDF

- **In** — as a snappable raster underlay, `PDFATTACH`. See [[PDF Underlays]].
- **Out** — as a plotted sheet, from the Layout ribbon. See [[Plotting]].

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| DXF import brings in nothing | Binary DXF, or the geometry is all in paper space | Save as **ASCII** DXF; read the log for the paper-space count |
| Imported geometry is miles from the survey points | Different coordinate systems | `ALIGN` with control pairs |
| Civil 3D points did not import | They are custom objects, absent from the DXF | Export a PNEZD CSV from Civil 3D |
| DXF import wiped my points | It should not — points are kept by design | Check the log; if IDs collided you were prompted to overwrite or offset |
| Some entities did not export | The type has no export branch yet | Read the `DXF export — excluded …` line; see [[Known Limitations]] |
| DWG menu items are greyed out | No converter found | Install the ODA File Converter, or set `GOSURVEY_DWG_CONVERTER` |
| DWG export lost blocks and layouts | Documented and warned about before writing | Use DXF where the receiving program accepts it |
| `IMPORTMODEL` refuses the scale | Zero or non-finite | Give a non-zero finite number |
| A DXF import zoomed out to nothing | An outlier entity at a wild coordinate | The log prints both the full and the outlier-excluded bounding box — find and delete it, then `ZE` |

---

## Related

[[Survey Points]] · [[Files and Drawings]] · [[Coordinate Alignment]] · [[PDF Underlays]] · [[Traverse Editor]]
