# SURFELEV


![SURFELEV command overview](wiki-img:commands/surfelev.png)
**Command:** `SURFELEV` — aliases: `SE`
**Category overview:** [[Survey Points]]

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

---

## Related

- [[Survey Points]]
- [[Command Reference]]