# EDITALIGNMENT

**Command:** `EDITALIGNMENT`
**Category overview:** [[Alignments]]

Open the Alignment Layout table: EDITALIGNMENT [<name>]

The table opens docked below the drawing (drag its title bar to float or move it; close it with its X) and stays open
while you work, refilling after every edit, undo or grip drag. Choose the alignment in the box at the top.

- **Tangency constraint** — Fixed, Float (prev), Float (next) or Free. Type a new one to change it.
- **Parameter constraint** — how the entity is defined: a tangent by length and bearing (Fixed) or length (floating),
  a curve by radius and delta, by radius and length, or by its radius between two tangents (Free), a spiral by its
  length and radii. Only those values can be edited; the others are greyed.
- **Criteria** — what the design check set requires of the entity (for example `R ≥ 120.00'` for a curve at its
  design speed). A failing row has a warning icon, bold text, and the finding in **Design check** (also as a tooltip).
- **Locked** — the values locked with `ALIGNMENTLOCK`.

See [[Alignments]] for the full workflow and the station format.

---

## Related

- [[Alignments]]
- [[Command Reference]]
