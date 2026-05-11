# GoSurvey

GoSurvey is a desktop drafting and survey-helper: a **CAD-style drawing** with optional **survey points** (coordinates, IDs, descriptions) and **DXF** import/export. Use the **ribbon**, **command line**, **viewport**, and **panels** together—the **command log** explains what to enter at each step.

---

## Layout at a glance

- **Drawing viewport** — Pan with the **middle mouse button**. Zoom with the **mouse wheel** (smooth, cursor-centered). A **minor grid** follows the view; large coordinates may be **rebased** on DXF import so pan/zoom stay stable.
- **Ribbon** (under the menu bar) — **Draw**, **Modify**, **View**, **Inquiry**, and **Survey** blocks in a grid; hover a button for a short description and **command bar** aliases (e.g. `LINE` / `L`). On the right: **Layer** — **LAY** (future layer manager) and a **layer** dropdown built from layers used in the drawing.
- **Properties** (docked left) — When something is selected: layer, color, linetype, lineweight, transparency, and geometry where supported. **General** also has **default plotted text height** (inches on sheet) for new TEXT/MTEXT.
- **Command line** (docked bottom) — Scrollable **log**, command **input** + **Send**, context **hints**, then a **single-line status bar**: **OSNAP**, **ORTHO**, **GRID**, **POLAR** (UI placeholder), **annotation scale** (preset dropdown such as 1″ = 50′), and **cursor / UCS** readout.
- **Panels** — Create points, viewpoints table, import/export CSV, and reports open as separate windows when you use their commands.

---

## Angles and bearings

**North is 0°**; angles increase **clockwise** (survey-style): east 90°, south 180°, west 270°.

That applies to bearings in **LINE** / **POLYLINE**, **ROTATE**, **TEXT** rotation, and related readouts.

---

## Drawing geometry

| Tool | What it does |
|------|----------------|
| **LINE** (`LINE` or `L`) | Segments from clicks or `X,Y` / two numbers. After the first point use **`@dx,dy`** for relative offsets. |
| **POLYLINE** (`POLYLINE` or `PL`) | Like LINE but one object; **`CLOSE`** / **`CL`** to close, **`END`** for open. |
| **CIRCLE** (`CIRCLE` or `C`) | Center, then radius (or **`D`** + diameter). Type **`3P`** for a three-point circle. |
| **ARC** | Three picks: start, point on arc, end. |
| **ELLIPSE** (`ELLIPSE` or `EL`) | Center, major-axis end, then **minor/major ratio** (0–1] on the command line (Enter = default). |
| **Aligned dimension** (`DIMALIGNED` or `DAL`) | Two extension points, then a point on the dimension line. |
| **TEXT** | Insertion, then height, rotation (clockwise from north), and string—often typed on the command line. |
| **MTEXT** (`MTEXT` or `MT`) | Two corners for a frame, then content. |

### LINE / POLYLINE — distance along a locked bearing

After the segment anchor:

- **`A <bearing>`** or **`ANGLE`** + degrees — locks the next segment to that bearing; then a **signed distance** along the ray or a click snapped to the line.
- **`AP`** (or **`ANGLEPICK`** / **`A P`**) — pick **two points** for direction; **Enter** locks, or **`+90`**, **`-45`**, etc. (decimal or DMS) to adjust before locking.
- One line can combine bearing and turn, e.g. **`A 45 +90`**.
- **`A`** / **`ANGLE`** alone clears the lock.
- With **Ortho** on and **no** bearing lock, a **single number** is distance along H/V toward the cursor.

While using **`AP`**, **Esc** cancels only the pick sequence, not the whole LINE.

### Ortho

**Ortho** constrains picks and rubber-band previews to **horizontal or vertical** from the **current anchor** (LINE/POLYLINE and similar). Toggle from the status bar or **F8** when you are not typing in the command input.

---

## Modifying and selecting

### Selection

With **no command active**, **two clicks** define a selection **window**. The log reports CAD and survey hits.

Type **`SELECT`** for an explicit selection reminder.

### MOVE / COPY (`MOVE` / `M`, `COPY` / `CP`)

Window-select if needed, then **base** and **second point** (or **`@dx,dy`** from base).

If **survey points** are copied/moved and IDs collide, a dialog chooses **skip, renumber, merge,** or **overwrite**.

### ROTATE (`ROTATE` / `RO`)

Window-select, **base**, then angle (**° clockwise from north**, DMS allowed), or **`R`** for reference direction then new bearing or **`P`** for two points. **`C`** toggles **copy**.

### DELETE (`DELETE` / `DEL`)

Two-click window over geometry to remove.

### JOIN (`JOIN` / `J`)

Window-select **lines / polylines** that meet at endpoints to merge chains.

### TRIM (`TRIM` / `TR`)

Pick **cutting edges**, **Enter**, then click segments to trim (near the end to remove). Type **`L`** for **line-trim** (two clicks on an edge).

---

## View

| Command | Action |
|---------|--------|
| **ZOOM EXTENTS** (`ZOOMEXTENTS` / `ZE`) | Fit geometry and survey markers in view. |
| **ZOOM WINDOW** (`ZOOMWINDOW` / `ZW`) | Two clicks define the zoom rectangle. |

Delete and zoom-window corners use **unsnapped** picks so corners land exactly where you click.

---

## Plot scale, annotations, and display

- **Annotation scale** — Preset dropdown on the **command line status bar** (e.g. 1″ = 50′). Values map to **model units per plotted inch** (same meaning as **`PLOTSCALE`** / **`PSCALE`** on the command line, e.g. `PSCALE 50`).
- **Default text height** — **Properties → General**, in **inches on the sheet**; combined with plot scale for TEXT/MTEXT model height.
- **REGEN** (`REGEN` / `RE`) — Refreshes GPU caches if the display looks stale.

---

## DXF

**File → Import DXF…** / **Export DXF…** — Linework, circles, arcs, ellipses, polylines, and supported annotations. Very large coordinates may be **shifted** on import for floating-point precision; export adds the offset back.

---

## Survey points

World coordinates: **Easting = X**, **Northing = Y**.

| Command | Panel |
|---------|--------|
| **CREATEPOINTS** (`CRTPTS`) | Create/configure points and optional click-placement. |
| **VIEWPOINTS** (`VWPTS`) | Table of points (IDs, coordinates, elevation, layer, description). |
| **IMPORTPOINTS** (`IMPPTS`) | CSV import with presets and preview. |
| **EXPORTPOINTS** (`EXPPTS`) | CSV export with column layout options. |

Create-points: numbering, defaults, JSON save/load. **Click placement** when idle (Esc turns it off). Survey markers participate in selection; **duplicate ID** policy applies when copying/moving survey rows.

---

## Snapping

**OSNAP** (status bar or **F3** when not typing in the command line) snaps to **endpoints**, **midpoints**, **circle centers**, and **perpendicular** points. Context matters for perpendicular snaps (e.g. previous LINE point).

---

## Layers and appearance

**Properties** on a selection: **layer**, **color**, **linetype**, **lineweight**, **transparency** (named colors or hex). The ribbon **layer** dropdown lists layers present on entities; **LAY** will open a layer table when implemented.

---

## Keyboard

| Input | Behavior |
|--------|----------|
| **Esc** | Cancel active command; during LINE **`AP`** pick, exits only the bearing pick first. Idle: clear selection / close some placement UIs. |
| **Delete** | Start **DELETE** (window select). |
| **F3** | Toggle **object snap** (when not typing in the command input). |
| **F8** | Toggle **Ortho** (when not typing in the command input). |

Typed commands are resolved with **flexible / fuzzy** matching for short input.

---

## Help

Type **`HELP`** in the command line for a compact command list.

---

## Tips

- Read the **command log** and **hints** under the input—they list valid inputs for the current step.
- Use **`@dx,dy`** from the current anchor when chaining LINE segments.
- For bearings without mental math: **`AP`**, two clicks, **`+90`** or **Enter**, then the **distance**.
- **`ZE`** after import or big edits to frame everything.
