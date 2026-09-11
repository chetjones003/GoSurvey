# OFFSET


![OFFSET command overview](wiki-img:commands/offset.png)
**Command:** `OFFSET` — aliases: `O`
**Category overview:** [[Modify Tools]]

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

---

## Related

- [[Modify Tools]]
- [[Command Reference]]