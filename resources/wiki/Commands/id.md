# ID


![ID command overview](wiki-img:commands/id.png)
**Command:** `ID`
**Category overview:** [[Survey Points]]

**Ribbon:** Inquiry → ID Point **Command:** `ID`

```
ID — specify point (click in drawing or type X,Y). UCS = World. ESC cancels.
ID: Pick point (OSNAP when enabled) or type X,Y — logs UCS World | ESC cancel
```

Reports the coordinates of a point:

```
ID — UCS (World)  X = 1543268.250  Y = 483112.900  Z = 0.000
```

Coordinates are always in **World** — X is easting, Y is northing, Z is elevation — regardless of
the current work plane. Decimals follow the `UNITS` display precision.

**With object snap on**, ID reports the snapped feature, which makes it the quick way to read off
the exact coordinates of an endpoint, an intersection, or a survey point.

If a command is already running, ID refuses:
`ID — finish or cancel the active command first.`

---

---

## Related

- [[Survey Points]]
- [[Command Reference]]