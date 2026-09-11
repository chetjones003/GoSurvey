# Coordinate Input

Everything you can type when GoSurvey asks for a point, a distance, or an angle.

---

## The coordinate frame

| Axis | Survey meaning |
|---|---|
| **X** | **Easting** |
| **Y** | **Northing** |
| **Z** | **Elevation** |

Coordinates you type and coordinates you read are **world** coordinates — the state-plane or
project values, not any internal frame. The status-bar readout, dynamic input, Properties, the
Viewpoints table, and every report all show world values.

Large state-plane coordinates are handled at full precision. GoSurvey sets an internal local origin
automatically when a drawing first sees big numbers, and does the subtraction in double precision
before storing, so an imported CSV point lands exactly on the matching DXF geometry.

---

## Point entry

At any prompt that asks for a point:

| Form | Meaning | Example |
|---|---|---|
| **A click** | The cursor position, snapped if a snap fires | — |
| `X,Y` | Absolute world coordinates | `1543268.25,483112.90` |
| `X Y` | Same, space separated | `1543268.25 483112.90` |
| `@dx,dy` | **Relative** to the previous point | `@100,0` |
| `@dx dy` | Same, space separated | `@100 0` |

Relative entry is only accepted where a previous point exists to measure from — the second point of
a LINE segment, the destination of a MOVE, the second extension line of a dimension. At a first
point there is no anchor, so `@` is refused.

> **Polar coordinate entry (`@100<45`) is not supported.** To go a distance along a bearing, use
> the **bearing lock** described below.

### Z and elevation

Point prompts take X and Y. The **Z of new geometry** comes from the current work plane, set with
`ELEV` and shown on the status bar as `UCS: World` or `UCS: Elev <z>`. See [[Drafting Aids]].

---

## Angle and bearing entry

**North is 0°. Angles increase clockwise.**

| Direction | Value |
|---|---|
| North | `0` |
| East | `90` |
| South | `180` |
| West | `270` |

This entry convention is fixed. How angles are **displayed** — decimal degrees, DMS, or surveyor's
bearings — is a separate setting in `UNITS`, and changing it does not change what you type.

### Accepted formats

| Form | Meaning |
|---|---|
| `132.5` | Decimal degrees |
| `132d30m00s` | Degrees, minutes, seconds |
| `132d30m` | Degrees and minutes; seconds default to 0 |
| `132d` | Degrees only |
| `-45` | Negative, i.e. counter-clockwise |

Angles are used by LINE and POLYLINE bearing lock, `ROTATE`, `ALIGN`, and TEXT rotation.

---

## Bearing lock — distance and direction

The survey way to draw. Available in **LINE** and **POLYLINE** after the first point.

| Input | Effect |
|---|---|
| `A <bearing>` | Lock the direction. `A 132d15m30s` |
| `A` alone | Enter bearing-lock mode; type the bearing on the next line. Blank Enter cancels |
| `A <bearing> +90` | Bearing plus a turn on one line. `-45` works too |
| `A` again | Clears the lock |
| `2P` (or `AP`, `ANGLEPICK`) | Define the direction by picking two points |
| `+90` / `-45` after a `2P` pick | Adjust the picked direction, then lock |
| **Enter** after a `2P` pick | Lock the picked direction unchanged |
| A number, while locked | Distance along the locked ray. Negative runs backwards |

### Example — a traverse leg

```
LINE
1543268.25,483112.90
A 132d15m30s
248.55
```

### Example — turning off an existing line

```
LINE
(snap to the start point)
2P                     ← pick two points along the existing line
(click, click)
+90                    ← turn 90° right from that direction, and lock
150                    ← run 150 units
```

---

## Distance entry

A bare number is interpreted as a **distance** when the command knows a direction:

- **With a bearing locked** — distance along the locked ray. A negative value runs the other way.
- **With Ortho on and no lock** — distance along the horizontal or vertical **toward the cursor**.
- **With neither** — there is no direction, so a bare number does nothing useful. Lock a bearing or
  turn on Ortho.

Distances are in **model units**, whatever unit your drawing is in. GoSurvey does not convert
units; the `UNITS` insertion-units setting is a label only.

---

## Dynamic input

While a command expects a **coordinate point**, two live fields follow the crosshair showing the
current **world** X and Y at the configured display precision.

| Key | Effect |
|---|---|
| **Type** | Locks the active field to what you type |
| **Tab** | Moves between the X and Y fields |
| **Enter** | Commits the point |
| **A click in the viewport** | Also commits the point |

Prompts that expect a bearing, angle, distance, option, or command name show a **single** field
instead of two.

---

## Display precision and format

Set in the **Drawing Units** dialog — type `UNITS`, `UN`, or `DDUNITS`.

![Drawing Units dialog](wiki-img:04-drawing-units.png)

*Note the **Sample Output** box at the bottom: it previews a coordinate and an angle in the formats
you have chosen, before you accept them.*

| Setting | Controls |
|---|---|
| **Length → Precision** | Decimal places for coordinates, distances, and lengths across the status bar, Properties, dynamic input, and reports |
| **Survey-point precision** | An independent decimal count for survey-point coordinates and labels (Settings → Drafting) |
| **Angle → Type** | Decimal Degrees · Deg/Min/Sec · Surveyor's Units |
| **Angle → Precision** | Decimals on the smallest unit shown |
| **Angle → Base (0°)** and **Clockwise** | The direction base and sense used for **display** |
| **Insertion scale** | Feet · Meters · Unitless. A **relabel only** — written to the `.gs` file and to DXF `$INSUNITS`; **coordinates are never scaled** |

Length **Type** is fixed at `Decimal` — GoSurvey works in decimal units, the survey and civil norm.
The other length formats are reserved.

The dialog shows a live **Sample Output** so you can see the effect before you accept it. Display
precision and angle format are saved as user preferences (`gosurvey-user.json`); plot scale and
insertion units are stored per drawing.

---

## Coordinate readout

The status bar shows `X … Y … Z …` for the crosshair, plus the current work plane:

- `UCS: World` — new geometry goes on the world XY plane (Z = 0).
- `UCS: Elev 125.400` — new geometry goes at that elevation.

`ID` reports the coordinates of a picked point into the command log. `INVERSE` reports ΔE, ΔN,
distance, and bearing between two picks. See [[Inquiry Commands]].

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| `@100,50` is refused | The prompt has no previous point to measure from | Use absolute `X,Y` for the first point |
| `@100<45` does nothing | Polar entry is not supported | Use `A 45` to lock the bearing, then `100` |
| The line went the wrong way | Bearings are clockwise from north, not counter-clockwise from east | `90` is east; `270` is west |
| Typed coordinates land in the wrong place | Typed values are always **world** coordinates | Check the status-bar readout; if the drawing was imported in local coordinates, use `ALIGN` |
| The readout shows too few decimals | Display precision | `UNITS` → **Length → Precision** |

---

## Related

[[Object Snaps]] · [[Drafting Aids]] · [[Drawing Tools]] · [[Inquiry Commands]] · [[Settings and Options]]
