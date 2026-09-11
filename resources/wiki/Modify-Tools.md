# Modify Tools

Editing commands live on the **Modify** ribbon section, in the right-click **Basic Modify Tools**
submenu, and on the command line.

Most modify commands begin with a **two-click window selection** if nothing is already selected. If
you select objects first, the command uses that selection and skips straight to its own prompts.

Every modify command pushes a single **undo step** — `Ctrl+Z` reverses the whole operation.

---

## MOVE (`M`)

**Ribbon:** Modify → Move **Command:** `MOVE`, `M`

```
MOVE/COPY: Click opposite corners of selection window | ESC cancel
MOVE/COPY: Base point — click or X,Y | ESC cancel
MOVE/COPY: Second point — click or X,Y or @dx,dy from base | ESC cancel
```

1. **Select** — click two opposite corners of a window, or have a selection already.
2. **Base point** — click or type `X,Y`. Snap to a corner or a survey point for an exact move.
3. **Second point** — click, type `X,Y`, or type `@dx,dy` for an exact displacement from the base.

**Example — shift everything 5 feet east and 2 feet north:**

```
MOVE
(window-select)
0,0
@5,2
```

**Duplicate survey-point IDs.** If moving would land a survey point on an ID that already exists, a
dialog asks what to do: **skip**, **renumber**, **merge**, or **overwrite**.

---

## COPY (`CP`)

**Ribbon:** Modify → Copy **Command:** `COPY`, `CP`

Identical to MOVE, but the originals stay put. Same prompts, same duplicate-ID dialog.

> `COPY` duplicates objects **inside the drawing** by base and second point. It is not the
> clipboard — that is `Ctrl+C` / `Ctrl+V`, further down this page.

---

## ROTATE (`RO`)

**Ribbon:** Modify → Rotate **Command:** `ROTATE`, `RO`

```
ROTATE: Window-select — click two corners | ESC cancel
ROTATE: Base point — click or X,Y | ESC cancel
ROTATE: ° clockwise / DMS | R ref | C copy | ESC (north=0° CW)
```

1. **Select**, then **base point** (the pivot).
2. **Angle** — one of:

| Input | Effect |
|---|---|
| A number | Rotate by that many degrees **clockwise**. DMS accepted (`45d30m00s`) |
| `R` or `REF` or `REFERENCE` | Reference mode: pick two points that define the *current* direction, then give the *new* direction |
| `C` | Toggles **copy mode** — the rotated result is a copy and the original stays |
| `P` (after a reference) | Give the new direction by picking two points instead of typing it |

### Reference mode prompts

```
ROTATE ref: First point | C toggles copy | ESC cancel
ROTATE ref: Second point | C toggles copy | ESC cancel
ROTATE ref: New bearing ° from north (like props) | P two pts | C copy | ESC
```

Reference mode is the practical one for surveying: snap along an existing line to capture its
bearing, then type the bearing you want it to have.

---

## SCALE (`SC`)

**Ribbon:** Modify → Scale **Command:** `SCALE`, `SC`

Uniform scaling about a base point.

```
SCALE: Window-select — click two corners | ESC cancel
SCALE: Base point — click or X,Y | ESC cancel
SCALE: Second point or type factor (>0) — dist/base-ref | R = two-point ref length | ESC
```

| Input at the factor prompt | Effect |
|---|---|
| A number greater than 0 | Scale by that factor. `2` doubles, `0.5` halves |
| A click | Factor is the picked distance divided by the base reference distance |
| `R` or `REFERENCE` | Reference-length mode |
| `C` | Toggles copy mode |

### Reference-length mode

```
SCALE ref: First point of reference length | ESC cancel
SCALE ref: Second point (reference length) | ESC cancel
SCALE ref: Type new length (model units) or pick first point of new length | ESC
SCALE ref: Second point of new length (preview) | ESC cancel
```

Pick two points across something whose real length you know, then type that real length. This is
how you scale a traced or imported drawing to a known dimension.

---

## OFFSET (`O`)

**Ribbon:** Modify → Offset **Command:** `OFFSET`, `O`

Creates a parallel copy at a distance. Works on **lines, circles, arcs, ellipses, and polylines**.

```
OFFSET: Pick line, circle, arc, ellipse, or polyline | ESC cancel
OFFSET: Type distance then pick side — or through-click (line / circle / arc) | ESC cancel
OFFSET: Pick side of object (polyline/ellipse use closest edge) | ESC cancel
```

1. **Pick the object.**
2. Either **type a positive distance** and then click the side to offset toward, or **click a
   through-point** the offset must pass through (lines, circles, and arcs only).

Circles and arcs offset **concentrically**. Polylines and ellipses offset as a whole, using the
closest edge to decide the side.

---

## TRIM (`TR`)

**Ribbon:** Modify → Trim **Command:** `TRIM`, `TR`

TRIM has **two modes**, controlled by the `TRIMSTATE` system variable.

### TRIMSTATE

| Value | TRIM starts by | Default |
|---|---|---|
| `0` | Asking you to **draw a trim line** — two clicks | ✔ |
| `1` | Asking you to **pick cutting edges**, Civil 3D style | |

Set it with `TRIMSTATE 1` on one line, or type `TRIMSTATE` alone for a prompt (blank Enter keeps the
current value).

### Mode 0 — draw the trim line

```
TRIM: First point of the trim line | type T — pick cutting edges instead | ESC cancel
TRIM: Second point — dashed = removed part (midpoint picks side) | Ortho | ESC
```

Draw a line across what you want cut. The **dashed** part of the preview is the part that will be
removed — the midpoint of your trim line decides which side goes. Ortho applies.

### Mode 1 — pick cutting edges

```
TRIM: Pick cutting edges (hover highlights) | Enter | type L — draw the trim line | ESC cancel
TRIM: Click segment near end to remove | Enter done | ESC cancel
```

1. Click the objects that act as cutting edges — hovering highlights them.
2. Press **Enter**.
3. Click each segment **near the end you want removed**.
4. Press **Enter** when done.

Typing `L` at the cutting-edge prompt switches to the draw-a-line mode; typing `T` at the trim-line
prompt switches back.

---

## JOIN (`J`)

**Ribbon:** Modify → Join **Command:** `JOIN`, `J`

```
JOIN — window-select lines/polylines that meet at endpoints. ESC cancels.
JOIN: Window-select — click two corners | ESC cancel
```

Window-select collinear lines, or coaxial arcs and polylines, that **meet at their endpoints**.
Touching chains merge into single objects.

Objects that only cross, or that are near-but-not-touching, are not joined. Use `OVERKILL` for
duplicate and overlapping cleanup instead.

---

## DELETE (`DEL`)

**Ribbon:** Modify → Erase **Command:** `DELETE`, `DEL` **Key:** `Delete`

**With objects already selected**, DELETE erases them immediately — no prompt. Selected survey
points are removed together with their linked labels.

**With nothing selected**, it asks for a window:

```
DELETE — click two corners to window-select objects to erase. ESC cancels.
```

Two clicks define a window over the geometry to remove. The window uses the **unsnapped** cursor
position, so a nearby snap point cannot pull a corner off target.

Pressing the `Delete` key with nothing else happening starts this command. In paper space, DELETE
requires that you select the paper objects or viewports first —
`DELETE — select paper object(s) or viewport(s) first.`

---

## OVERKILL (`OK`)

**Ribbon:** — **Command:** `OVERKILL`, `OK`

Cleans the **entire drawing** in one pass. It takes no selection and asks nothing — run it and read
the counts in the log.

| Object type | What is removed or merged |
|---|---|
| **Lines** | Zero-length segments; exact duplicates; collinear overlapping or touching segments merged into the shortest covering segment |
| **Circles** | Exact duplicates — same centre and radius within tolerance |
| **Arcs** | Arcs whose underlying circle already exists as a full circle; exact duplicate arcs |
| **Polylines** | Zero-length (coincident) vertex steps |

**Tolerance** is derived from the drawing size: `1 × 10⁻⁴ × max(x-span, y-span)`. Removal counts
are reported in the command log.

> **Tip** — run `OVERKILL` after a DXF import or a long manual cleanup. Imported linework routinely
> carries stacked duplicates that are invisible until you try to trim or join.

---

## Clipboard — Cut, Copy, Paste

| Action | Shortcut | Menu | Command |
|---|---|---|---|
| **Cut** | *(see note)* | Right-click ▸ Clipboard ▸ Cut | — |
| **Copy** | `Ctrl+C` | Edit ▸ Copy, ribbon Copy | — |
| **Paste** | `Ctrl+V` | Edit ▸ Paste, ribbon Paste | `PASTE` |
| **Paste at Original Coordinates** | — | Edit ▸ Paste at Original Coordinates | `PASTEORIG`, `PO` |

> **Note on Cut** — the menu item is labelled `Ctrl+X`, but that key is only wired inside the
> MTEXT text editor; in the drawing it does nothing. Use the menu item. Cut is copy followed by
> erase, so it works on an existing selection.

- **Paste** places the copied objects at the cursor, with a live preview before you click:
  `PASTE: Click destination point | ESC cancel`
- **Paste at Original Coordinates** puts them back exactly where they were copied from — the right
  choice when moving objects between drawing tabs or between model and paper space.

The clipboard carries geometry **between model space and paper layouts** at 1:1 raw coordinates,
and between drawing tabs. Paper-space edits made this way are undoable.

---

## Grips

Selected objects show **grips** — small blue squares at endpoints, centres, and midpoints. Drag one
to edit the geometry directly.

- Grips honour **object snap**, so you can drag an endpoint onto another endpoint exactly.
- **Ortho** constrains a grip drag to horizontal or vertical from the grip start — unless a snap
  fires, which wins.
- **Esc** during a drag cancels it and restores the original position (`Grip edit canceled.`).
- Grip size is set in **Settings → Selection → Grip size (px)**.

---

## Object isolation

| Command | Aliases | Effect |
|---|---|---|
| `ISOLATEOBJECTS` | `ISOLATE` | Hide everything except the current selection |
| `HIDEOBJECTS` | — | Hide the selected objects |
| `UNISOLATEOBJECTS` | `UNISOLATE` | Show everything that isolation hid |

Also available from the viewport right-click menu under **Isolate Objects**.

---

## ALIGN (`AL`)

Transforms objects onto a real-world coordinate system from control-point pairs. It is a survey
tool rather than an ordinary edit, and has its own page: [[Coordinate Alignment]].

---

## Undo and Redo

| Action | Shortcut |
|---|---|
| Undo | `Ctrl+Z` |
| Redo | `Ctrl+Shift+Z` or `Ctrl+Y` |

The **Edit** menu names the step: *Undo: Edit hatch*, *Undo: Import model*.

Undo history is **per drawing tab** and is cleared when the tab is closed. The number of steps kept
is set in **Settings → User Preferences**; a history log is written to
`%APPDATA%\GoSurvey\history.log`.

---

## Not implemented

**MIRROR** appears on the Modify ribbon but is **disabled** — hovering it says *"Mirror — flip
selection across a mirror line (not implemented yet)."* There is no STRETCH, EXTEND, BREAK, FILLET,
CHAMFER, ARRAY, or EXPLODE command. See [[Known Limitations]].
