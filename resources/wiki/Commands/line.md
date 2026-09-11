# LINE


![LINE command overview](wiki-img:commands/line.png)
**Command:** `LINE` — aliases: `L`
**Category overview:** [[Drawing Tools]]

**Ribbon:** Draw → Line **Command:** `LINE`, `L`

Draws one or more straight segments. Each segment is a separate object.

### Workflow

```
Command: LINE
LINE — specify first point (click or type X,Y / X Y). ESC to cancel.
LINE: Next: click; X, Y; @dx,dy; [A]zimuth, [2P];
```

1. **First point** — click, or type `X,Y`.
2. **Next point** — click, type `X,Y`, or type `@dx,dy` relative to the previous point.
3. Repeat. **Esc** ends the command.

### Bearing entry

This is the part that makes LINE useful for survey work. After the first point:

| Input | Effect |
|---|---|
| `A <bearing>` | Locks the direction to that bearing. Example: `A 45` or `A 45d30m00s`. |
| `A` alone | Enters bearing-lock mode; the next line you type is the bearing. Blank Enter cancels. |
| `A <bearing> +90` | Bearing plus a turn, in one line. `-45` also works. |
| `A` (while locked) | Clears the lock. |
| `2P` or `AP` or `ANGLEPICK` | Pick two points to define the direction. Then **Enter** locks it, or type `+90` / `-45` to adjust before locking. |
| A number, with a bearing locked | Distance along the locked ray. A negative number runs backwards. |
| A number, with **Ortho** on and no lock | Distance along the horizontal or vertical toward the cursor. |

Bearings are **degrees clockwise from north** and accept decimal degrees (`132.5`) or DMS
(`132d30m00s`). While in `2P` pick mode, **Esc** cancels only the pick, not the whole command.

### Prompts you will see

| Prompt line | Meaning |
|---|---|
| `LINE: First point — click or X,Y` | Waiting for the anchor |
| `LINE: Type bearing ° CW from N (decimal/DMS) \| blank Enter cancels` | You typed `A` alone |
| `LINE bearing pick: First direction point — click \| 2P started` | You typed `2P` |
| `LINE bearing pick: Enter locks \| +90/-45 adjust+lock` | Two direction points taken |
| `LINE (bearing lock): distance ± along ray \| click on line \| X,Y \| @dx,dy \| A clears` | Direction is locked |

### Example — a bearing-and-distance leg

```
LINE
0,0                     ← start at the origin
A 132d15m30s            ← lock the bearing S47°44'30"E, entered as 132°15'30" CW from north
248.55                  ← run 248.55 units along it
A 132d15m30s +90        ← turn 90° right
100                     ← run 100 units
Esc
```

### Common problems

| Problem | Cause | Fix |
|---|---|---|
| Typing a number does nothing useful | No bearing lock and Ortho is off, so a bare number has no direction | Lock a bearing with `A`, or turn on Ortho (`F8`) |
| The bearing came out backwards | Bearings are clockwise from **north**, not counter-clockwise from east | `90` is due east, `180` due south |
| Segments will not join into one object | LINE makes separate segments by design | Use `POLYLINE`, or draw with LINE and then `JOIN` |

---

---

## Related

- [[Drawing Tools]]
- [[Command Reference]]