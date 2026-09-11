# MOVE


![MOVE command overview](wiki-img:commands/move.png)
**Command:** `MOVE` — aliases: `M`
**Category overview:** [[Modify Tools]]

**Ribbon:** Modify → Move **Command:** `MOVE`, `M`

```
MOVE/COPY: Click opposite corners of selection window | ESC cancel
MOVE/COPY: Base point — click or X,Y | ESC cancel
MOVE/COPY: Second point — click or X,Y or @dx,dy from base | ESC cancel
```

1. **Select** — click two opposite corners of a window, or have a selection already.
2. **Base point** — click or type `X,Y`. Snap to a corner or a survey point for an exact move.
3. **Second point** — click, type `X,Y`, or type `@dx,dy` for an exact displacement from the base.

**Example — shift everything 5 feet east and 2 feet north:**

```
MOVE
(window-select)
0,0
@5,2
```

**Duplicate survey-point IDs.** If moving would land a survey point on an ID that already exists, a
dialog asks what to do: **skip**, **renumber**, **merge**, or **overwrite**.

---

---

## Related

- [[Modify Tools]]
- [[Command Reference]]