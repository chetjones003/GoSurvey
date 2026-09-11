# POLYLINE


![POLYLINE command overview](wiki-img:commands/polyline.png)
**Command:** `POLYLINE` — aliases: `PL`
**Category overview:** [[Drawing Tools]]

**Ribbon:** Draw → Polyline **Command:** `POLYLINE`, `PL`

Like LINE, but the whole chain is **one object**.

### Workflow

1. **First point** — click or type.
2. **Next point** — as many as you need. Accepts everything LINE accepts, bearing lock included.
3. Finish with one of:
   - `CLOSE` or `CL` — adds a closing segment back to the first point and ends the polyline.
   - `END` — ends the polyline open.
   - **Esc** — ends the command.

### Prompts

```
POLYLINE: First point — click or X,Y | CLOSE closes | ESC cancel
POLYLINE: Next — click | X,Y | @dx,dy | A/2P | CLOSE / END | ESC
POLYLINE (bearing lock): distance ± | click on ray | X,Y | A clears | CLOSE / END
```

### Notes

- A **closed** polyline is what `HATCH` needs a boundary for, and what the **Geometric center**
  object snap targets.
- Polylines can be offset and joined; see [[Modify Tools]].

---

---

## Related

- [[Drawing Tools]]
- [[Command Reference]]