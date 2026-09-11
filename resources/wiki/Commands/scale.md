# SCALE


![SCALE command overview](wiki-img:commands/scale.png)
**Command:** `SCALE` — aliases: `SC`
**Category overview:** [[Modify Tools]]

**Ribbon:** Modify → Scale **Command:** `SCALE`, `SC`

Uniform scaling about a base point.

```
SCALE: Window-select — click two corners | ESC cancel
SCALE: Base point — click or X,Y | ESC cancel
SCALE: Second point or type factor (>0) — dist/base-ref | R = two-point ref length | ESC
```

| Input at the factor prompt | Effect |
|---|---|
| A number greater than 0 | Scale by that factor. `2` doubles, `0.5` halves |
| A click | Factor is the picked distance divided by the base reference distance |
| `R` or `REFERENCE` | Reference-length mode |
| `C` | Toggles copy mode |

### Reference-length mode

```
SCALE ref: First point of reference length | ESC cancel
SCALE ref: Second point (reference length) | ESC cancel
SCALE ref: Type new length (model units) or pick first point of new length | ESC
SCALE ref: Second point of new length (preview) | ESC cancel
```

Pick two points across something whose real length you know, then type that real length. This is
how you scale a traced or imported drawing to a known dimension.

---

---

## Related

- [[Modify Tools]]
- [[Command Reference]]