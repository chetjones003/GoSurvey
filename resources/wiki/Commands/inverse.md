# INVERSE


![INVERSE command overview](wiki-img:commands/inverse.png)
**Command:** `INVERSE` — aliases: `INV`
**Category overview:** [[Survey Points]]

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

---

## Related

- [[Survey Points]]
- [[Command Reference]]