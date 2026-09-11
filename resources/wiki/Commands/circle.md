# CIRCLE


![CIRCLE command overview](wiki-img:commands/circle.png)
**Command:** `CIRCLE` — aliases: `C`
**Category overview:** [[Drawing Tools]]

**Ribbon:** Draw → Circle **Command:** `CIRCLE`, `C`

Two construction methods.

### Centre and radius (default)

1. **Centre** — click or type `X,Y`.
2. **Radius** — click a point on the rim, type the radius, or type `D <value>` / `D<value>` for a
   diameter.

```
CIRCLE: Click or type center | Type 3P for three-point circle | ESC cancel
CIRCLE: Click edge for radius | Type radius | D <value> or D<value> for diameter | ESC cancel
```

### Three-point (`3P`)

Type `3P` **before** picking the centre, then pick three points on the circle:

```
CIRCLE (3P): Point 1 of 3 — click or X,Y | ESC cancel
CIRCLE (3P): Point 2 of 3 — click or X,Y | ESC cancel
CIRCLE (3P): Point 3 of 3 — click or X,Y | ESC cancel
```

> **Tip** — with 3P, snap each pick. The circle is committed at the **snapped** point, not where
> the cursor happened to be, so a rim snap gives you exactly the circle through those three points.

### Options

| Option | Where | Effect |
|---|---|---|
| `D <value>` | At the radius prompt | Treats the value as a diameter |
| `3P` | At the centre prompt | Switches to three-point construction |

---

---

## Related

- [[Drawing Tools]]
- [[Command Reference]]