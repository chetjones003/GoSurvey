# HATCH


![HATCH command overview](wiki-img:commands/hatch.png)
**Command:** `HATCH` — aliases: `H`, `BHATCH`
**Category overview:** [[Drawing Tools]]

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

---

## Related

- [[Drawing Tools]]
- [[Command Reference]]