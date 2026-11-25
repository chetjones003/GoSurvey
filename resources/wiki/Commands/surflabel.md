# SURFLABEL

**Command:** `SURFLABEL`
**Category overview:** [[Point Groups and Surfaces]]

Places linked labels on a surface.

| Usage | Label |
|---|---|
| `SURFLABEL <surface>, SPOT, x, y` | Spot elevation |
| `SURFLABEL <surface>, SLOPE, x1, y1, x2, y2` | Slope between two points |
| `SURFLABEL <surface>, CONTOUR, elevation, x, y` | One contour label |
| `SURFLABEL <surface>, CONTOUR, elevation, GUIDE, x1, y1, …` | Repeated labels where a guide crosses the contour |
| `SURFLABEL <surface>, WATERSHED` | Watershed basins |

The surface needs a built TIN; points must lie on it.

---

## Related

- [[Point Groups and Surfaces]]
- [[Command Reference]]
