# GoSurvey

GoSurvey is a desktop drafting and survey-helper: a **CAD-style drawing** with optional **survey points** (coordinates, IDs, descriptions), **PDF underlays** with object snap, and **DXF** import/export. Use the **ribbon**, **command line**, **viewport**, and **panels** together — the **command log** and **dynamic cursor hint** explain what to enter at each step.

---

## Download and install (Windows)

### Installer (recommended)

1. Open **[Releases](https://github.com/chetjones003/GoSurvey/releases)** for this repository.
2. Download the **`.exe` installer** from the latest release.
3. Run it and follow the wizard. GoSurvey is installed to `%ProgramFiles%\GoSurvey` by default and a Start Menu entry is created. An optional desktop shortcut is offered.
4. **SmartScreen**: Windows may show "Windows protected your PC" for apps that are not code-signed. Choose **More info → Run anyway** if you trust the release.

### Portable ZIP

A portable **`.zip`** bundle is also provided for users who prefer not to run an installer.

1. Download and extract the zip to any folder.
2. **Keep the folder layout intact** — do not move the `.exe` away from its sibling folders:

   ```text
   YourFolder\
     GoSurvey-<version>.exe
     pdfium.dll
     resources\
       default-template.gs
       icons\
         bitmap.png
       layouts\
   ```

3. Run the executable directly from inside that folder, or create a shortcut whose **"Start in"** path points to that folder.

### Runtime requirement

If the app fails to start with a missing DLL error, install the latest **[Visual C++ Redistributable for x64](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)** from Microsoft.

---

## Layout at a glance

- **Drawing viewport** — Pan with the **middle mouse button**. Zoom with the **mouse wheel** (smooth, cursor-centred). A minor grid follows the view. While a command is active, a **floating hint panel** appears near the cursor showing what to enter next — the same hint also appears in the command line footer.
- **Ribbon** (under the menu bar) — **Draw**, **Modify**, **View**, **Inquiry**, and **Survey** blocks; hover a button for a short description and command aliases. Layer dropdown on the right.
- **Properties** (docked left) — Layer, color, linetype, lineweight, transparency, and geometry for the current selection. Also holds **default plotted text height** for new TEXT/MTEXT.
- **Command line** (docked bottom) — Scrollable **log**, command **input**, context **hints**, and a status bar with **OSNAP**, **ORTHO**, **GRID**, **POLAR**, annotation scale, and cursor readout.
- **Panels** — Create points, viewpoints table, CSV import/export, and reports open as separate windows.

**Enter key**: pressing **Enter** submits the current command input from anywhere — you do not need to click the command line first.

---

## Angles and bearings

**North is 0°**; angles increase **clockwise** (survey convention): east 90°, south 180°, west 270°.

Applies to LINE/POLYLINE bearings, ROTATE, ALIGN, TEXT rotation, and all readouts.

---

## Drawing geometry

| Tool | Alias | What it does |
|------|-------|--------------|
| **LINE** | `L` | Segments from clicks or `X,Y`. Use `@dx,dy` for relative offsets. |
| **POLYLINE** | `PL` | Like LINE but one object. `CLOSE`/`CL` to close, `END` for open. |
| **CIRCLE** | `C` | Center then radius (or `D` + diameter). `3P` for three-point. |
| **ARC** | — | Three picks: start, point on arc, end. |
| **ELLIPSE** | `EL` | Center, major-axis end, then minor/major ratio. |
| **TEXT** | — | Insertion, height, rotation (CW from north), content. |
| **MTEXT** | `MT` | Two corners for a frame, then content. |
| **DIMALIGNED** | `DAL` | Aligned dimension: two extension points, then dimension line point. |
| **DIMLINEAR** | `DLI` | Horizontal or vertical dimension. |
| **DIMANGULAR** | `DAN` | Angular dimension between two lines. |

### LINE / POLYLINE — bearing input

After the segment anchor:

- **`A <bearing>`** — lock to a bearing; then a signed distance or a click.
- **`ANGLE`** alone — enters bearing-lock mode; type the bearing on the next prompt.
- **`AP`** / **`ANGLEPICK`** — pick two points for direction; Enter to lock, or `+90` / `-45` (decimal or DMS) to adjust first.
- **`A 45 +90`** — bearing and turn in one line.
- **`A`** alone — clear the lock.
- With **Ortho** on and no bearing lock, a **single number** is distance along H/V toward the cursor.

While in `AP` mode, **Esc** cancels only the pick, not the whole command.

### Ortho

**F8** (or status bar toggle) constrains picks and rubber-band previews to horizontal/vertical from the current anchor.

---

## Modifying and selecting

### Selection

With no command active, **two clicks** define a selection window. Type `SELECT` for an explicit reminder.

### MOVE / COPY (`M` / `CP`)

Window-select, then **base** and **second point** (or `@dx,dy`). Duplicate survey-point IDs trigger a dialog: skip, renumber, merge, or overwrite.

### ROTATE (`RO`)

Window-select, base, then angle (° CW from north, DMS allowed), or `R` for reference direction. `C` toggles copy mode.

### SCALE (`SC`)

Window-select, base, scale factor. `C` toggles copy.

### OFFSET (`O`)

Pick an entity, enter a distance (or type `T` and click through-point), then click the side to offset toward. Works on lines, circles, and arcs.

### TRIM (`TR`)

Pick cutting edges, press Enter, then click the segment halves to remove (click near the end you want gone). Type `L` for line-trim (two clicks on one edge).

### JOIN (`J`)

Window-select collinear lines or coaxial arcs/polylines that meet at endpoints; merges touching chains into single entities.

### OVERKILL (`OK`)

Cleans up the entire drawing in one pass:

- **Lines** — removes zero-length and exact-duplicate segments; merges collinear overlapping/touching segments into the shortest covering segment (union-find + 1-D interval cover).
- **Circles** — removes exact duplicates (same centre and radius within tolerance).
- **Arcs** — removes arcs whose underlying circle is already present as a full circle entity; removes exact duplicate arcs.
- **Polylines** — strips zero-length (coincident) vertex steps.

Tolerance is auto-derived: `1 × 10⁻⁴ × max(x-span, y-span)`. Removal counts are reported in the command log.

### DELETE (`DEL`)

Two-click window over geometry to remove.

---

## PDF Underlay

### Attaching a PDF (`PDFATTACH` / `PA`)

Opens the PDF Attach dialog. Choose a PDF file and page, then:

- **Specify insertion point** — click in the viewport to place, then set scale.
- **Direct insert** — enter coordinates and scale numerically.

The PDF is rasterised and displayed as a GPU texture overlay. Both light-background (white paper) and dark-background (CAD export) PDFs are supported: the background is automatically detected and made transparent so the PDF floats cleanly over your drawing at any opacity.

Multiple PDFs can be attached simultaneously. Each underlay has independent position, scale, rotation, opacity, and snap toggles.

### PDF snap

Object snap works on attached PDFs. Snap targets are detected by analysing the **rendered raster image** rather than internal PDF path structure, so snap fires at the geometry you can actually see:

- **Endpoints** — line ends, stroke terminals (1 foreground neighbour in the raster topology).
- **Corners** — bends and junctions where two lines meet at an angle (2 non-opposite neighbours).
- **Junctions** — T-intersections and crossings (3+ neighbours).
- **Midpoints** and **perpendicular** snaps are also available on PDF line segments.

This image-based approach reliably handles GIS exports, scanned drawings, and other high-density PDFs where the internal PDF path objects do not correspond to visible line geometry.

---

## View

| Command | Action |
|---------|--------|
| **ZOOM EXTENTS** (`ZE`) | Fit all geometry and markers in view. |
| **ZOOM WINDOW** (`ZW`) | Two clicks define the zoom rectangle. |
| **REGEN** (`RE`) | Refresh GPU caches if the display looks stale. |

Delete and zoom-window use **unsnapped** cursor positions.

---

## Plot scale, annotations, and display

- **Annotation scale** — Preset dropdown on the status bar (e.g. `1″ = 50′`). Same as `PLOTSCALE`/`PSCALE` on the command line (e.g. `PSCALE 50`). Controls model units per plotted inch.
- **Default text height** — Properties → General, in inches on the sheet; combined with plot scale for new TEXT/MTEXT model height.
- **REGEN** (`RE`) — Refreshes GPU caches if the display looks stale.

---

## DXF

Use **File → Import DXF…** and **File → Export DXF…** for exchange with AutoCAD, AutoCAD Civil 3D, and other DXF readers.

### Export (ASCII DXF AC1032)

Exports are **ASCII DXF** tagged AC1032 (AutoCAD 2018-class):

- **HEADER** — Drawing limits, extents, system variables, `$HANDSEED`, and (when survey points are present) `$PDMODE`/`$PDSIZE` so POINT entities display as an X.
- **TABLES** — Standard tables: LAYER (with plot-style pointers), VIEW, UCS, VPORT, APPID, DIMSTYLE, BLOCK_RECORD with canonical `*Model_Space`/`*Paper_Space` names.
- **ENTITIES** — Lines, circles, arcs, ellipses, polylines, text, mtext, aligned dimensions (as exploded lines + text), with layers, ACI colors, lineweights, and model-space ownership.
- **OBJECTS** — Minimal named-object dictionary so hosts like Civil 3D accept the file.

Survey points are written as native `POINT`/`AcDbPoint` entities (easting, northing, elevation, per-point layer and ByLayer colour).

### Import

Model-space geometry: lines, circles, arcs, ellipses, polylines, and common annotations. `POINT` entities are approximated as small crossing line segments. Paper space and unsupported types are skipped; the command log reports details.

---

## Survey points

World coordinates: **Easting = X**, **Northing = Y**.

| Command | Panel |
|---------|-------|
| **CREATEPOINTS** (`CRTPTS`) | Open the create-points panel and click in the drawing to place survey points. |
| **VIEWPOINTS** (`VWPTS`) | Table of points: IDs, coordinates, elevation, layer, description. |
| **IMPORTPOINTS** (`IMPPTS`) | CSV import with column presets and preview. |
| **EXPORTPOINTS** (`EXPPTS`) | CSV export with column layout options. |

### Create Points

Open the **Create points** panel (ribbon Survey → point icon, or type `CREATEPOINTS`). While the panel is open, clicking in the drawing places survey points — no separate toggle needed. Clicking on an existing marker selects it instead.

The panel lets you set the **next point ID**, default **layer**, **description**, and **elevation** for new points, and choose a **duplicate ID policy** (skip, renumber, merge, overwrite).

Survey markers participate in selection; duplicate-ID policy applies when copying or moving. Export DXF writes survey points as AutoCAD `POINT` objects so they appear in Civil 3D and similar hosts.

---

## Coordinate alignment — ALIGN (`AL`)

`ALIGN` computes and applies a **2D Helmert (similarity) transformation** — scale, rotation, and translation — from a set of source→destination control point pairs. This is the standard least-squares 4-parameter adjustment used to transform a drawing from local coordinates into a real-world coordinate system.

### Workflow

1. **Type `ALIGN` or `AL`.**

   - If entities are already selected, ALIGN uses that selection immediately and skips to step 3.
   - If nothing is selected, ALIGN asks you to **window-select** the entities to transform. Press **Enter** to confirm the selection.

2. **Pick control point pairs.** For each pair:

   - Click (or type `X,Y`) the **source** survey point in the drawing — use OSNAP to snap precisely to an existing survey marker.
   - Click or type the **destination** real-world coordinates for that point.

   Repeat for as many pairs as needed. A minimum of **1 pair** gives translation only. **2 or more pairs** fit a full Helmert (scale + rotation + translation).

3. **Press Enter to solve.** The results window opens showing the computed transformation parameters and per-pair residuals.

4. **Review and optionally remove pairs.** Click the **`-`** button on any row to remove it — the solution updates live. Use this to exclude outliers.

5. **Apply.** Choose one of:

   - **Apply Scale** (checkbox checked) — applies the full Helmert including the computed scale factor.
   - **Apply** (checkbox unchecked) — applies rotation and translation only, with scale forced to 1.0. The translation is re-derived from centroids so the solution remains least-squares optimal.

6. **Report.** A transformation report is automatically added to the **Reports** tab showing all parameters and per-pair point errors.

### Control point tagging

After applying, ALIGN automatically tags affected survey points:

- **Source points** (those snapped-to as ALIGN sources) — have **` ADJ`** appended to their description, indicating they were adjusted.
- **Destination points** (those at the real-world coordinates) — have **` CON`** appended, indicating they are control (fixed) points. Their coordinates are **restored to the exact destination values** after the transform; they do not drift.

### Results window columns

| Column | Meaning |
|--------|---------|
| Pair | Sequential pair number |
| Src X / Src Y | Source point coordinates (pre-transform drawing space) |
| Dst X / Dst Y | Destination real-world coordinates |
| Resid | Point error — distance from predicted to actual destination after the transformation |

**Point error (RMS)** is the root-mean-square of per-pair residuals: the average distance by which each source maps away from its intended destination. Lower is a better fit.

---

## Snapping

**OSNAP** (status bar or **F3** when not typing) snaps to **endpoints**, **midpoints**, **circle centres**, and **perpendicular** points on both native geometry and PDF underlays.

For PDF underlays, snap targets are detected from the rendered raster image and filtered through a visibility mask so only candidates that land in areas with visible content are offered. A spatial grid (CSR layout) keeps endpoint lookup fast even for dense PDFs with thousands of snap targets.

---

## Layers and appearance

Select an entity and use **Properties** to edit layer, colour, linetype, lineweight, and transparency. The ribbon layer dropdown lists all layers present in the drawing. **LAYER** (`LA`) opens the layer manager.

---

## Keyboard

| Key | Behaviour |
|-----|-----------|
| **Esc** | Cancel active command. In `AP` bearing-pick mode, exits the pick first. Idle: clear selection / close placement UIs. |
| **Enter** | Submit command input from anywhere — no need to click the command line first. |
| **Delete** | Start DELETE (window select). |
| **F3** | Toggle object snap (when not typing in the command input). |
| **F8** | Toggle Ortho (when not typing in the command input). |

Typed commands use **flexible / fuzzy** matching so short input works.

---

## Help

Type **`HELP`** in the command line for a compact command list.

---

## Building from source

Requirements: CMake 3.20+, a C++17 toolchain (MSVC or Clang-cl), OpenGL, Windows SDK (for WinRT PDF thumbnail acceleration).

```bash
git clone https://github.com/chetjones003/GoSurvey.git
cd GoSurvey
cmake --preset ninja-release
cmake --build build
```

Dependencies (fetched automatically by CMake): GLFW, Dear ImGui, GLEW, PDFium (prebuilt Windows x64 binary), nlohmann/json.

---

## Tips

- The **floating hint panel** near the cursor tells you what to enter at every step — the same text also appears in the command line footer.
- Read the **command log** and **hints** under the input — they list valid inputs for the current step.
- Use **`@dx,dy`** when chaining LINE segments from the current anchor.
- For bearings without mental math: **`AP`**, two clicks, **`+90`** or Enter, then type the distance.
- **`ZE`** after import or large edits to frame everything in view.
- Run **`OVERKILL`** after DXF imports or manual cleanup to remove duplicates and merge collinear segments.
- Use **`PDFATTACH`** to bring in a reference plan, then snap directly to its visible geometry for accurate placement.
- Use **`ALIGN`** to transform a local-coordinate drawing into real-world coordinates using known control points. Add ≥ 2 source→destination pairs for a full Helmert fit; remove outlier pairs in the results window before applying.
