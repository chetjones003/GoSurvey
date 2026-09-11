# Views and Navigation

---

## Mouse navigation

| Input | Action |
|---|---|
| **Middle mouse drag** | Pan |
| **Mouse wheel** | Zoom, smooth and cursor-centred |

The wheel zoom factor is set in **Settings → Display → Wheel zoom factor**, from `1.01x` to `3.00x`
per notch. The same page reports the current zoom and pan values.

---

## Zoom commands

### ZOOMEXTENTS (`ZE`)

**Ribbon:** View → Extents **Command:** `ZOOMEXTENTS`, `ZE`

Fits all drawing content — geometry, survey point markers, everything — in the view. The first
thing to reach for after an import or a large edit.

### ZOOMWINDOW (`ZW`)

**Ribbon:** View → Window **Command:** `ZOOMWINDOW`, `ZW`

```
ZOOM WINDOW: Two corners (unsnapped) — rubber previews fit area | ESC cancel
```

Two clicks define the rectangle to zoom to. The rubber-band preview shows the area that will fit.
The corners use the **unsnapped** cursor, so a nearby snap point cannot pull one off target.

Both zoom commands refuse to start while another command is running —
`ZOOM EXTENTS — finish or cancel the active command first.`

There is no `ZOOM PREVIOUS`, no named views, and no `ZOOM` with a scale factor.

---

## PAN (`P`)

**Command:** `PAN`, `P` **Menu:** Right-click ▸ Pan

```
PAN — drag with the left mouse button. Press Esc, Enter, or right-click to exit.
```

A modal pan. Most of the time the middle-mouse drag is quicker — this command exists for when the
middle button is not available.

---

## ORBIT (`3DORBIT`, `3DO`)

**Command:** `ORBIT`, `3DORBIT`, `3DO` **Menu:** Right-click ▸ Free Orbit

```
ORBIT — drag with the left mouse button to tumble. Press Esc, Enter, or right-click to exit.
```

Free-orbits the model view. **Model space only** — `ORBIT — model space only; a paper sheet is 2D.`

Orbit is how you check elevations. A plan view cannot show you that two lines which appear to cross
are at different heights, or that a surface has a spike in it. Tumbling the view makes both
obvious in seconds.

---

## ViewCube

A cube in the corner of the viewport, **model space only**. Click a face to swing the view to that
orientation:

**TOP · BOTTOM · FRONT · BACK · LEFT · RIGHT**

The view **eases** into place rather than jumping, so you keep your bearings. The cube also tracks
the current view azimuth.

To get back to plan view, click **TOP**.

---

## Visual styles

**Ribbon:** View → Visual style **Command:** `VISUALSTYLE`, `VS`, `VSCURRENT`

How the viewport draws.

| Style | Effect |
|---|---|
| **2D Wireframe** | Every edge visible, no depth testing — the classic CAD view. Draw order decides what sits on top |
| **Hidden** | Depth testing on, so near geometry hides far geometry |
| **Shaded** | Depth testing on, **plus** lit fills for imported meshes and hatches |

**What "Shaded" actually fills.** Only two things: **imported meshes** (`IMPORTMODEL`) and
**hatches / filled regions**. It does **not** fill TIN surfaces — those are drawn as triangle edges
in all three styles, so switching styles on a surface changes nothing you can see. See
[[Known Limitations]].

On a purely 2D drawing the three styles also look alike, because nothing occludes anything.

Usage:

```
VS SHADED
VS 2D
VISUALSTYLE            ← reports the current value and the options
```

A bare `VISUALSTYLE` answers `Visual style = 2D Wireframe. Usage: VS 2D | HIDDEN | SHADED.` and
anything unrecognised answers `VISUALSTYLE — enter 2D, HIDDEN or SHADED.`

To read a **TIN surface** in three dimensions, use `ORBIT` — not the visual style. Tilting the view
is what exposes elevation, spikes, and bad shots.

---

## Elevation and the work plane

`ELEV` / `UCS` sets the Z that new geometry is drawn at, reported on the status bar as `UCS: World`
or `UCS: Elev <z>`. See [[Drafting Aids]].

---

## Model and paper space

| Control | Effect |
|---|---|
| Status bar **MODEL / PAPER** button | Toggle between model space and the current layout |
| Status bar layout tabs | Switch directly to any layout |
| **☰** menu | Model space · each layout · New paper layout |
| Double-click a viewport | Enter floating model space (**FLOAT**) |
| `MSPACE` / `MS`, `PSPACE` / `PS` | The same, from the command line |

See [[Paper Space and Layouts]].

---

## REGEN (`RE`)

**Command:** `REGEN`, `RE`

Regenerates the drawing and refreshes the GPU caches. Use it if the display looks stale after a
large edit — geometry that should have disappeared but is still drawn, or an edit that has not
shown up.

---

## Isolating what you can see

`ISOLATEOBJECTS`, `HIDEOBJECTS`, `UNISOLATEOBJECTS` temporarily reduce a dense drawing to what you
are working on. **VP Freeze** does the same per viewport in a layout. See [[Object Selection]] and
[[Layers]].

---

## Panel layouts

**View → Reset layout** restores the built-in panel arrangement.

**View → Layout ▸** saves and recalls named panel layouts:

- **Save current as…** — *Saves the current panel layout as a named .ini file.* Names may contain
  letters, digits, dash, and underscore
- **Switch to** — pick from the saved layouts

The log confirms `UI layout: saved as "<name>"` and `UI layout: switched to "<name>"`.

---

## Display settings that affect the viewport

**Settings → Display** (see [[Settings and Options]]):

| Setting | Effect |
|---|---|
| **Color theme** | Dark or Light |
| **Wheel zoom factor** | Zoom per wheel notch |
| **Display scroll bars in drawing window** | Scroll bars |
| **Display printable area**, **Display paper background**, **Display paper shadow** | Paper-space appearance |
| **Smooth line display** | Removes the jagged look on diagonal lines and curved edges |
| **Accelerated font display** | GPU-accelerated TrueType rendering |

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| Zoom extents shows a huge empty area | Something is at a wild coordinate — often a stray point from a bad import | Find and delete the outlier, then `ZE` again |
| The drawing looks stale | GPU cache | `REGEN` / `RE` |
| ORBIT will not start | You are in a paper layout | Switch to model space |
| Lines that should cross do not connect | They are at different elevations | Orbit to check, and see [[Object Snaps]] on apparent intersections |
| A surface is a flat mess of triangle edges | Surfaces render as edges in every style; `SHADED` does not fill them | Expected. `ORBIT` to read the shape |
| Panels are in the wrong place | Layout drift | **View → Reset layout** |

---

## Related

[[Drafting Aids]] · [[Paper Space and Layouts]] · [[Point Groups and Surfaces]] · [[Settings and Options]]
