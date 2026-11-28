# STRETCH


![STRETCH command overview](wiki-img:commands/stretch.png)
**Command:** `STRETCH` — aliases: `S`
**Category overview:** [[Modify Tools]]

Crossing/window-select, then move only the vertices inside the box

## What moves

STRETCH moves the points that lie inside the crossing window and leaves the rest where they are,
the way AutoCAD's STRETCH does.

| Object | What happens |
| --- | --- |
| Line, polyline, feature line | Each vertex inside the window moves. |
| Arc | Each endpoint inside the window moves, and the arc keeps its included angle. An arc tilted out of plan, or stretched in a tilted UCS, moves whole when its centre is inside. |
| Spline | The fit points inside the window move and the curve is refit through them. A spline drawn with control vertices moves those instead. If the moved points can no longer make a curve (two land on each other), the spline is left as it was and the command line says why. |
| Multileader | Each leader point inside the window moves. When the landing is inside, the text moves with it. |
| Block insert | Moves whole when its insertion point is inside the window. |
| Circle, ellipse, text, MTEXT, dimension, table, point | Moves whole when its centre or insertion point is inside the window. |
| Hatch | Moves whole when its first boundary vertex is inside the window. |
| PDF underlay, survey point | Moves when its insertion point or position is inside the window. |

Solids, pipe runs, imported models and Position Markers can only move whole: STRETCH leaves them
out and says so on the command line. Use MOVE for those.

The preview while you pick the destination is the stretch itself, applied to a copy, so it shows
exactly what the click will do.

See [[Modify Tools]] for related tools and workflows.

---

## Related

- [[Modify Tools]]
- [[Command Reference]]