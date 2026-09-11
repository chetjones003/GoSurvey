# Drawing Tools

All drawing commands work the same way at the top level:

1. Start the command — click a **Draw** ribbon button, or type its name and press **Enter**.
2. Answer each prompt by **clicking in the drawing** or **typing a value**.
3. **Esc** cancels. Most commands stay active for repeated use until you press Esc.

Everything you may type at a point prompt is listed in [[Coordinate Input]]. What the cursor snaps
to is in [[Object Snaps]]. Ortho and the work-plane elevation are in [[Drafting Aids]].

New geometry is created on the **current layer** (ribbon, top right) with the current colour,
linetype, lineweight, and transparency from **Properties → General**, and at the current
**elevation** shown as `UCS:` on the status bar.

---

## LINE (`L`)

**Ribbon:** Draw → Line **Command:** `LINE`, `L`

Draws one or more straight segments. Each segment is a separate object.

### Workflow

```
Command: LINE
LINE — specify first point (click or type X,Y / X Y). ESC to cancel.
LINE: Next: click; X, Y; @dx,dy; [A]zimuth, [2P];
```

1. **First point** — click, or type `X,Y`.
2. **Next point** — click, type `X,Y`, or type `@dx,dy` relative to the previous point.
3. Repeat. **Esc** ends the command.

### Bearing entry

This is the part that makes LINE useful for survey work. After the first point:

| Input | Effect |
|---|---|
| `A <bearing>` | Locks the direction to that bearing. Example: `A 45` or `A 45d30m00s`. |
| `A` alone | Enters bearing-lock mode; the next line you type is the bearing. Blank Enter cancels. |
| `A <bearing> +90` | Bearing plus a turn, in one line. `-45` also works. |
| `A` (while locked) | Clears the lock. |
| `2P` or `AP` or `ANGLEPICK` | Pick two points to define the direction. Then **Enter** locks it, or type `+90` / `-45` to adjust before locking. |
| A number, with a bearing locked | Distance along the locked ray. A negative number runs backwards. |
| A number, with **Ortho** on and no lock | Distance along the horizontal or vertical toward the cursor. |

Bearings are **degrees clockwise from north** and accept decimal degrees (`132.5`) or DMS
(`132d30m00s`). While in `2P` pick mode, **Esc** cancels only the pick, not the whole command.

### Prompts you will see

| Prompt line | Meaning |
|---|---|
| `LINE: First point — click or X,Y` | Waiting for the anchor |
| `LINE: Type bearing ° CW from N (decimal/DMS) \| blank Enter cancels` | You typed `A` alone |
| `LINE bearing pick: First direction point — click \| 2P started` | You typed `2P` |
| `LINE bearing pick: Enter locks \| +90/-45 adjust+lock` | Two direction points taken |
| `LINE (bearing lock): distance ± along ray \| click on line \| X,Y \| @dx,dy \| A clears` | Direction is locked |

### Example — a bearing-and-distance leg

```
LINE
0,0                     ← start at the origin
A 132d15m30s            ← lock the bearing S47°44'30"E, entered as 132°15'30" CW from north
248.55                  ← run 248.55 units along it
A 132d15m30s +90        ← turn 90° right
100                     ← run 100 units
Esc
```

### Common problems

| Problem | Cause | Fix |
|---|---|---|
| Typing a number does nothing useful | No bearing lock and Ortho is off, so a bare number has no direction | Lock a bearing with `A`, or turn on Ortho (`F8`) |
| The bearing came out backwards | Bearings are clockwise from **north**, not counter-clockwise from east | `90` is due east, `180` due south |
| Segments will not join into one object | LINE makes separate segments by design | Use `POLYLINE`, or draw with LINE and then `JOIN` |

---

## POLYLINE (`PL`)

**Ribbon:** Draw → Polyline **Command:** `POLYLINE`, `PL`

Like LINE, but the whole chain is **one object**.

### Workflow

1. **First point** — click or type.
2. **Next point** — as many as you need. Accepts everything LINE accepts, bearing lock included.
3. Finish with one of:
   - `CLOSE` or `CL` — adds a closing segment back to the first point and ends the polyline.
   - `END` — ends the polyline open.
   - **Esc** — ends the command.

### Prompts

```
POLYLINE: First point — click or X,Y | CLOSE closes | ESC cancel
POLYLINE: Next — click | X,Y | @dx,dy | A/2P | CLOSE / END | ESC
POLYLINE (bearing lock): distance ± | click on ray | X,Y | A clears | CLOSE / END
```

### Notes

- A **closed** polyline is what `HATCH` needs a boundary for, and what the **Geometric center**
  object snap targets.
- Polylines can be offset and joined; see [[Modify Tools]].

---

## RECT (`RECTANG`, `RECTANGLE`)

**Ribbon:** Draw → Rectangle **Command:** `RECT`

Two opposite corners. The result is stored as a **closed polyline**, so it hatches, offsets, and
snaps like any other polyline.

```
RECT — pick the first corner (or type X,Y):
```

1. **First corner** — click or type `X,Y`.
2. **Opposite corner** — click, type `X,Y`, or type `@dx,dy` for an exact size.

If the two corners give a zero width or zero height, the log says
`RECT — corners give a zero-width or zero-height rectangle; pick again.` and you pick again.

**Example — a 200 × 150 rectangle from a known corner:**

```
RECT
1000,2000
@200,150
```

---

## CIRCLE (`C`)

**Ribbon:** Draw → Circle **Command:** `CIRCLE`, `C`

Two construction methods.

### Centre and radius (default)

1. **Centre** — click or type `X,Y`.
2. **Radius** — click a point on the rim, type the radius, or type `D <value>` / `D<value>` for a
   diameter.

```
CIRCLE: Click or type center | Type 3P for three-point circle | ESC cancel
CIRCLE: Click edge for radius | Type radius | D <value> or D<value> for diameter | ESC cancel
```

### Three-point (`3P`)

Type `3P` **before** picking the centre, then pick three points on the circle:

```
CIRCLE (3P): Point 1 of 3 — click or X,Y | ESC cancel
CIRCLE (3P): Point 2 of 3 — click or X,Y | ESC cancel
CIRCLE (3P): Point 3 of 3 — click or X,Y | ESC cancel
```

> **Tip** — with 3P, snap each pick. The circle is committed at the **snapped** point, not where
> the cursor happened to be, so a rim snap gives you exactly the circle through those three points.

### Options

| Option | Where | Effect |
|---|---|---|
| `D <value>` | At the radius prompt | Treats the value as a diameter |
| `3P` | At the centre prompt | Switches to three-point construction |

---

## ARC

**Ribbon:** Draw → Arc **Command:** `ARC`

Three-point arc only: **start**, a **point on the arc**, then the **end**.

```
ARC: Start point | ESC cancel
ARC: Point on arc | ESC cancel
ARC: End point | ESC cancel
```

There are no start/centre/angle variants. To build an arc from a centre and radius, draw a circle
and trim it, or place the middle point with snaps.

---

## ELLIPSE (`EL`)

**Ribbon:** Draw → Ellipse **Command:** `ELLIPSE`, `EL`

1. **Centre** — click or type.
2. **Major-axis endpoint** — click or type. This sets both the major radius and the rotation.
3. **Ratio** — type the minor/major ratio in the range `(0, 1]` on the command line. **Enter alone
   uses 0.5.**

```
ELLIPSE: Center | ESC cancel
ELLIPSE: Major axis end | ESC cancel
ELLIPSE: Ratio (0-1] on command line | Enter = 0.5 | ESC cancel
```

A ratio of `1` gives a circle-shaped ellipse. Ellipses can be offset.

---

## HATCH (`H`, `BHATCH`)

**Ribbon:** Draw → Hatch **Command:** `HATCH`, `H`, `BHATCH`

Fills a closed area.

```
HATCH — pick an internal point inside a closed area (Esc to cancel).
```

**Pick a point inside the area you want filled.** GoSurvey traces the boundary from the
surrounding geometry and creates a filled region.

### The Hatch ribbon section

While HATCH is running, a contextual **Hatch** section appears on the ribbon carrying the creation
defaults:

| Control | What it sets |
|---|---|
| **Pattern** | The hatch pattern, chosen from thumbnails. 83 patterns from the bundled `acadiso.pat` library — `SOLID`, the `ANSI31`–`ANSI38` family, `AR-*` architectural patterns, `EARTH`, `GRAVEL`, `NET`, and the rest |
| **Colour** | The fill colour |
| **Transparency** | 0–100% |
| **Layer** | Layer for the new hatch |
| **Angle** | Pattern rotation in degrees |
| **Scale** | Pattern spacing multiplier |

### Editing hatches after the fact

Select one or more hatches and the same ribbon section returns, titled **Hatch (selected)**. Now
the controls edit the selected objects **live**, and each change is a single undo step (`Undo: Edit
hatch`).

### Common problems

| Problem | Cause | Fix |
|---|---|---|
| Nothing fills | The area is not actually closed — a gap between segments | Zoom in and check the corners; `JOIN` the chain or redraw with `POLYLINE` / `RECT` |
| The pattern looks solid or invisible | The pattern scale is far too small or too large for the drawing | Adjust **Scale** in the Hatch ribbon section |

---

## PDFATTACH (`PA`)

Not geometry, but it lives on the Draw ribbon: attaches a PDF page as a snappable raster underlay.
See [[PDF Underlays]].

---

## What is not here

GoSurvey has no `SPLINE`, `POINT` (as a drawing command — use `CREATEPOINTS` for survey points),
`BLOCK`/`INSERT`, `XLINE`, `RAY`, `DONUT`, `SOLID`, `REVCLOUD`, `WIPEOUT`, or `TABLE` command, and
no `MIRROR` (the ribbon button exists but is disabled). See [[Known Limitations]].
