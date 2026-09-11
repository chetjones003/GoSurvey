# RECT


![RECT command overview](wiki-img:commands/rect.png)
**Command:** `RECT` — aliases: `RECTANG`, `RECTANGLE`
**Category overview:** [[Drawing Tools]]

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

---

## Related

- [[Drawing Tools]]
- [[Command Reference]]