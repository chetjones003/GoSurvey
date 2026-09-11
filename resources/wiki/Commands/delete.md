# DELETE


![DELETE command overview](wiki-img:commands/delete.png)
**Command:** `DELETE` — aliases: `DEL`
**Category overview:** [[Modify Tools]]

**Ribbon:** Modify → Erase **Command:** `DELETE`, `DEL` **Key:** `Delete`

**With objects already selected**, DELETE erases them immediately — no prompt. Selected survey
points are removed together with their linked labels.

**With nothing selected**, it asks for a window:

```
DELETE — click two corners to window-select objects to erase. ESC cancels.
```

Two clicks define a window over the geometry to remove. The window uses the **unsnapped** cursor
position, so a nearby snap point cannot pull a corner off target.

Pressing the `Delete` key with nothing else happening starts this command. In paper space, DELETE
requires that you select the paper objects or viewports first —
`DELETE — select paper object(s) or viewport(s) first.`

---

---

## Related

- [[Modify Tools]]
- [[Command Reference]]