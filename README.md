# GoSurvey

GoSurvey is a desktop drafting and survey-helper application: you work on a **drawing canvas** with CAD-style geometry, optional **survey points** (coordinates, IDs, descriptions), and **DXF** exchange. Use the **ribbon**, **command line**, **viewport clicks**, and **panels** together—the command log at the bottom explains what the program expects at each step.

---

## Layout at a glance

- **Drawing viewport** — Pan with the **middle mouse button**. Zoom with the scroll wheel (cursor-centered).
- **Ribbon** — Quick buttons for common draw and modify tools (tabs such as Home / Draw / Survey are visible; many actions also map to typed commands).
- **Properties** — When something is selected, edit layer, color, lineweight, transparency, and geometry where supported.
- **Hot toggles** — **Ortho mode**, **Object snap**, **Grid**, plot scale and default text height.
- **Command line** — Type commands and coordinates; hints appear above the input.
- **Panels** — Create points, viewpoints table, import/export CSV, and reports open as separate windows when you turn them on.

---

## Angles and bearings

Throughout the app, **north is 0°** and angles increase **clockwise** (survey-style): east is 90°, south 180°, west 270°.

That convention applies to typed bearings in **LINE** / **POLYLINE** locking, **ROTATE**, **TEXT** rotation, and bearing-style readouts in properties where relevant.

---

## Drawing geometry

| Tool | What it does |
|------|----------------|
| **LINE** (`LINE` or `L`) | Chain segments from points you click or type (`X,Y` or two numbers). After the first point you can use **`@dx,dy`** for relative offsets. |
| **POLYLINE** (`POLYLINE` or `PL`) | Like LINE but keeps one polyline object; finish with **`CLOSE`** / **`CL`** to snap back to the start, or **`END`** for an open polyline. |
| **CIRCLE** (`CIRCLE` or `C`) | Center, then radius by click or typed value; **`D`** prefix enters **diameter**. Type **`3P`** first for a three-point circle. |
| **ARC** | Three picks: start, a point on the arc, end. |
| **ELLIPSE** (`ELLIPSE` or `EL`) | Center, major-axis endpoint, then type a **minor/major ratio** (0–1] on the command line (Enter uses a default). |
| **Aligned dimension** (`DIMALIGNED` or `DAL`) | Two extension corners, then a point on the dimension line; drops a simple aligned dimension with text. |
| **TEXT** | Pick insertion, then height, rotation (clockwise from north), and text string—much of this can be typed on the command line. |
| **MTEXT** | Two corners for a box, then content. |

### LINE / POLYLINE — distance along a locked bearing

After your segment anchor is set:

- **`A <bearing>`** or **`ANGLE`** + degrees — locks the next segment to that bearing. Then type a **single distance** (positive or negative along that ray) or click—the click is pulled onto that infinite line.
- **`AP`** (or **`ANGLEPICK`** / **`A P`**) — pick **two points** in the drawing to define direction (first → second). Then **Enter** locks that bearing, or type **`+90`**, **`-45`**, etc. (decimal or DMS) to **rotate the bearing** before locking.
- On one line you can combine bearing and turn, e.g. **`A 45 +90`** or **`45+90`**.
- **`A`** or **`ANGLE`** alone clears the bearing lock.
- With **Ortho** on and **no** bearing lock, a **single number** is treated as distance along horizontal or vertical toward the cursor.

While picking bearings with **`AP`**, **Esc** cancels only the pick sequence, not the whole LINE command.

### Ortho

**Ortho mode** constrains new picks (and rubber-band previews) to horizontal or vertical relative to the **current anchor**—useful for chained LINE/POLYLINE segments.

---

## Modifying and selecting

### Selection

With **no command active**, **two clicks** define a **fence** (selection window). The command log reports how many CAD entities and survey hits were selected.

You can type **`SELECT`** for an explicit selection prompt.

### MOVE / COPY (`MOVE`/`M`, `COPY`/`CP`)

Window-select (if needed), then **base point** and **second point** (or **`@dx,dy`** from base).

If **survey points** were included and Copy/Move duplicates them, a dialog asks how to handle **duplicate point IDs** (skip, renumber, merge, overwrite).

### ROTATE (`ROTATE` or `RO`)

Window-select, **base point**, then:

- Type an angle (**degrees clockwise from north**, including DMS); or use **`R`** / reference for a **reference direction**, then a **new bearing** or **`P`** for two points defining the new direction.
- **`C`** toggles **copy** mode (keeps originals).

Preview rubber-banding follows the same bearing rules where applicable.

### DELETE (`DELETE` or `DEL`)

Two-click window over geometry to erase.

### JOIN (`JOIN` or `J`)

Window-select **lines / polylines** that meet at endpoints to merge compatible chains.

### TRIM (`TRIM` or `TR`)

**Civil-style trim:** pick **cutting edges**, press **Enter**, then click segments to trim (near the end you want removed). You can type **`L`** for a **line-trim** mode (two clicks along an edge).

---

## View

| Command | Action |
|---------|--------|
| **ZOOM EXTENTS** (`ZOOMEXTENTS` or `ZE`) | Fit all geometry (and survey markers) in view. |
| **ZOOM WINDOW** (`ZOOMWINDOW` or `ZW`) | Two clicks define the area to zoom. |

Delete and zoom-window picks intentionally avoid object snap so window corners land exactly where you click.

---

## Plot scale and annotations

- **Plot scale** — “Model units per plotted inch” (e.g. **50** for a 1″ = 50′ style drawing). Set from **Hot toggles** or command **`PLOTSCALE`** / **`PSCALE`**.
- **Default text height** — Shown in **inches on the printed sheet**; combined with plot scale to size TEXT/MTEXT in model units.
- **REGEN** (`REGEN` or `RE`) — Refreshes graphics caches if the display looks stale.

---

## DXF files

Under **File**:

- **Import DXF…** — Brings geometry (and supported data) into the current drawing context.
- **Export DXF…** — Writes your linework, circles, arcs, ellipses, polylines, and annotations out to a DXF file.

*(Other File menu entries such as New/Open may appear but are not described here as full workflows.)*

---

## Survey points

Survey data uses the same world coordinates as the drawing: **Easting = X**, **Northing = Y**.

### Commands that open panels

| Command | Panel |
|---------|--------|
| **CREATEPOINTS** (`CRTPTS`) | Create / configure points and optional click-placement. |
| **VIEWPOINTS** (`VWPTS`) | Spreadsheet-style table of all points (edit IDs, coordinates, elevation, layer, description). |
| **IMPORTPOINTS** (`IMPPTS`) | CSV import with column-layout presets and validation preview. |
| **EXPORTPOINTS** (`EXPPTS`) | CSV export with chosen column order and optional header row. |

### Create points

- Set numbering rules, default layer, description, elevation, and what to do when an ID already exists.
- Optional **click placement** on the drawing when no other CAD command is running (Esc turns placement off).
- Save/load a **JSON survey database** file from the panel (default name suggestions include `gosurvey_points.json`).

### Import / export CSV

Import supports several **column orders** (point ID with northing/easting variants, or coordinate-only rows with IDs assigned on import). Preview shows validation messages before you commit **Import**.

Export mirrors similar layout choices.

### In the viewport

Survey markers can be **picked** alongside CAD geometry (Shift can subtract from selection). When Copy/Move/Rotate duplicates IDs, the **duplicate policy** dialog applies.

---

## Snapping

With **Object snap** enabled, the cursor can snap to **endpoints**, **midpoints**, **circle centers**, and **perpendicular** locations on existing geometry (priorities favor centers over midpoints when distances tie). Perpendicular snapping respects the command context (e.g. previous LINE point).

---

## Layers and appearance

In **Properties**, selection sets expose **layer**, **color**, **linetype**, **lineweight**, and **transparency**. Colors can be named presets or hex values.

Hot toggles include simple **layer quick** reminders (e.g. survey-related naming)—actual layer lists are driven by what you assign in properties.

---

## Keyboard

| Key | Typical behavior |
|-----|-------------------|
| **Esc** | Cancels the active command; **during LINE bearing pick (`AP`)**, first exits only the bearing pick. With nothing running, clears selection / closes some placement modes. |
| **Delete** | Starts **DELETE** window workflow. |

Typed commands are matched flexibly (including fuzzy matching for short typos).

---

## Getting help

Type **`HELP`** in the command line for a compact list of commands and reminders.

---

## Tips

- Watch the **command log**—it tells you the valid inputs for the current step.
- Use **`@dx,dy`** from the current anchor when chaining LINE segments.
- For bearings without doing math in your head: **`AP`** + two clicks, then **`+90`** or **Enter**, then type the **distance**.
- **`ZE`** is the quickest way to see everything after import or large edits.
