# ROTATE


![ROTATE command overview](wiki-img:commands/rotate.png)
**Command:** `ROTATE` — aliases: `RO`
**Category overview:** [[Modify Tools]]

**Ribbon:** Modify → Rotate **Command:** `ROTATE`, `RO`

```
ROTATE: Window-select — click two corners | ESC cancel
ROTATE: Base point — click or X,Y | ESC cancel
ROTATE: ° clockwise / DMS | R ref | C copy | ESC (north=0° CW)
```

1. **Select**, then **base point** (the pivot).
2. **Angle** — one of:

| Input | Effect |
|---|---|
| A number | Rotate by that many degrees **clockwise**. DMS accepted (`45d30m00s`) |
| `R` or `REF` or `REFERENCE` | Reference mode: pick two points that define the *current* direction, then give the *new* direction |
| `C` | Toggles **copy mode** — the rotated result is a copy and the original stays |
| `P` (after a reference) | Give the new direction by picking two points instead of typing it |

### Reference mode prompts

```
ROTATE ref: First point | C toggles copy | ESC cancel
ROTATE ref: Second point | C toggles copy | ESC cancel
ROTATE ref: New bearing ° from north (like props) | P two pts | C copy | ESC
```

Reference mode is the practical one for surveying: snap along an existing line to capture its
bearing, then type the bearing you want it to have.

---

---

## Related

- [[Modify Tools]]
- [[Command Reference]]