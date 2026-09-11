# Inquiry Commands

Three commands that measure and report rather than change the drawing. Results go to the command
log, so scroll it (or press `F2` for the console) to read them.

---

## ID — identify a point

**Ribbon:** Inquiry → ID Point **Command:** `ID`

```
ID — specify point (click in drawing or type X,Y). UCS = World. ESC cancels.
ID: Pick point (OSNAP when enabled) or type X,Y — logs UCS World | ESC cancel
```

Reports the coordinates of a point:

```
ID — UCS (World)  X = 1543268.250  Y = 483112.900  Z = 0.000
```

Coordinates are always in **World** — X is easting, Y is northing, Z is elevation — regardless of
the current work plane. Decimals follow the `UNITS` display precision.

**With object snap on**, ID reports the snapped feature, which makes it the quick way to read off
the exact coordinates of an endpoint, an intersection, or a survey point.

If a command is already running, ID refuses:
`ID — finish or cancel the active command first.`

---

## INVERSE — distance and bearing between two points

**Ribbon:** Survey → Inverse **Command:** `INVERSE`, `INV`

The core COGO measurement: a survey leg between two points.

```
INVERSE — first point (World X=Easting, Y=Northing); then second.
INVERSE: First point — pick or X,Y (Easting, Northing) | ESC cancel
INVERSE: Second point — pick or X,Y / @ from first | ESC cancel
```

1. **First point** — click (snapping to a survey point or endpoint) or type `X,Y`.
2. **Second point** — click, type `X,Y`, or type `@dx,dy` relative to the first.

### The result

```
INVERSE — ΔE = 743.350  ΔN = -332.460  horiz dist = 814.397  bearing = 114°08'42"
```

| Value | Meaning |
|---|---|
| **ΔE** | Change in easting |
| **ΔN** | Change in northing |
| **horiz dist** | Horizontal distance between the two points |
| **bearing** | Direction, **clockwise from north** |

The bearing is reported in the format set in `UNITS` — decimal degrees, degrees/minutes/seconds, or
surveyor's bearings. See [[Coordinate Input]].

If both picks land in the same place:
`INVERSE — horizontal distance is zero; pick a different second point.`

> **Tip** — turn OSNAP on and set **Survey point** as a snap type before inversing between points.
> A pick a hundredth off is a hundredth of error in the reported distance.

---

## SURFELEV — surface elevation and grade

**Ribbon:** Inquiry → Elev/Grade **Command:** `SURFELEV`, `SE`

Reads elevations off TIN surfaces, and the grade between two locations.

```
SURFELEV — pick a point for its surface elevation; pick a second for grade. ESC cancels.
SURFELEV: Pick a point for its surface elevation | ESC cancel
SURFELEV: Second point for grade | ESC stops after the elevation
```

Requires at least one surface. Without one:
`SURFELEV — there are no surfaces in the drawing. Build one from a point group first.`
See [[Point Groups and Surfaces]].

### First pick — elevation

Reports the interpolated elevation on **every surface that covers that point**, one line each:

```
SURFELEV — Existing Ground: elevation 412.685
SURFELEV — Proposed Grade: elevation 414.200
```

A pick outside every surface says so rather than extrapolating:

```
SURFELEV — outside surface. No elevation at that point.
```

### Second pick — grade

```
SURFELEV — Existing Ground: grade -2.45%  slope 40.82:1  horiz 118.220  vert -2.896
```

or, when the two points are at the same elevation:

```
SURFELEV — Existing Ground: level (0.00%)  horiz 118.220  vert 0.000
```

| Value | Meaning |
|---|---|
| **grade** | Rise over run as a percentage |
| **slope** | Run per unit of rise, as `N:1` |
| **horiz** | Horizontal distance between the picks |
| **vert** | Elevation difference |

Cases the command reports rather than guessing at:

| Message | Meaning |
|---|---|
| `both picks are at the same location: horizontal distance 0. No grade.` | The two picks coincide |
| `<surface>: second point is outside this surface. No grade.` | The second pick left that surface |
| `outside surface at the first point; no grade to report.` | The first pick was already off the surface |

Press **Esc** after the first pick to stop with just the elevation.

---

## Related reporting

| Need | Use |
|---|---|
| Distance and bearing between two points | `INVERSE` |
| Exact coordinates of a feature | `ID` |
| Elevation and grade on a surface | `SURFELEV` |
| Traverse closure and residuals | [[Traverse Editor]] |
| Transformation parameters and point errors | [[Coordinate Alignment]] |
| A CSV of every point | `EXPORTPOINTS` — see [[Survey Points]] |

There is no `AREA`, `DIST`, `LIST`, or `MASSPROP` command. The only area GoSurvey reports is a
**circle's**, shown under *Derived* in the Properties panel along with its diameter and
circumference. A parcel area has to be computed outside the program — export the boundary or the
points and compute there.

---

## Related

[[Survey Points]] · [[Point Groups and Surfaces]] · [[Coordinate Input]] · [[Object Snaps]]
