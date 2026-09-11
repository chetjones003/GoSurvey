# TRIM


![TRIM command overview](wiki-img:commands/trim.png)
**Command:** `TRIM` — aliases: `TR`
**Category overview:** [[Modify Tools]]

**Ribbon:** Modify → Trim **Command:** `TRIM`, `TR`

TRIM has **two modes**, controlled by the `TRIMSTATE` system variable.

### TRIMSTATE

| Value | TRIM starts by | Default |
|---|---|---|
| `0` | Asking you to **draw a trim line** — two clicks | ✔ |
| `1` | Asking you to **pick cutting edges**, Civil 3D style | |

Set it with `TRIMSTATE 1` on one line, or type `TRIMSTATE` alone for a prompt (blank Enter keeps the
current value).

### Mode 0 — draw the trim line

```
TRIM: First point of the trim line | type T — pick cutting edges instead | ESC cancel
TRIM: Second point — dashed = removed part (midpoint picks side) | Ortho | ESC
```

Draw a line across what you want cut. The **dashed** part of the preview is the part that will be
removed — the midpoint of your trim line decides which side goes. Ortho applies.

### Mode 1 — pick cutting edges

```
TRIM: Pick cutting edges (hover highlights) | Enter | type L — draw the trim line | ESC cancel
TRIM: Click segment near end to remove | Enter done | ESC cancel
```

1. Click the objects that act as cutting edges — hovering highlights them.
2. Press **Enter**.
3. Click each segment **near the end you want removed**.
4. Press **Enter** when done.

Typing `L` at the cutting-edge prompt switches to the draw-a-line mode; typing `T` at the trim-line
prompt switches back.

---

---

## Related

- [[Modify Tools]]
- [[Command Reference]]