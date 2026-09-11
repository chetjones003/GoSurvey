# OVERKILL


![OVERKILL command overview](wiki-img:commands/overkill.png)
**Command:** `OVERKILL` — aliases: `OK`
**Category overview:** [[Modify Tools]]

**Ribbon:** — **Command:** `OVERKILL`, `OK`

Cleans the **entire drawing** in one pass. It takes no selection and asks nothing — run it and read
the counts in the log.

| Object type | What is removed or merged |
|---|---|
| **Lines** | Zero-length segments; exact duplicates; collinear overlapping or touching segments merged into the shortest covering segment |
| **Circles** | Exact duplicates — same centre and radius within tolerance |
| **Arcs** | Arcs whose underlying circle already exists as a full circle; exact duplicate arcs |
| **Polylines** | Zero-length (coincident) vertex steps |

**Tolerance** is derived from the drawing size: `1 × 10⁻⁴ × max(x-span, y-span)`. Removal counts
are reported in the command log.

> **Tip** — run `OVERKILL` after a DXF import or a long manual cleanup. Imported linework routinely
> carries stacked duplicates that are invisible until you try to trim or join.

---

---

## Related

- [[Modify Tools]]
- [[Command Reference]]