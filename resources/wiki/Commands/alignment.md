# ALIGNMENT

**Command:** `ALIGNMENT` — aliases: `CREATEALIGNMENT`, `CREATEALIGNMENTLAYOUT`
**Category overview:** [[Alignments]]

With no arguments (Home ▸ Create Design ▸ Alignment ▸ Alignment Creation Tools) this opens **Create Alignment -
Layout**, like Civil 3D's:

- **Name**, **Type** (Centerline, Offset, Curb Return, Miscellaneous, Rail), **Description** and **Starting station**.
- **General** tab: the alignment style, layer and label set, and an optional starting point and direction (azimuth in
  degrees clockwise from north). With a starting point the layout starts there at once; with a direction too, the
  first tangent keeps it.
- **Design Criteria** tab: the starting design speed and a design check set.

OK creates the alignment and opens the **Alignment Layout Tools** window; choose Tangent-Tangent there to lay it out
(see [[Commands/alignmentlayout]]).

From the command line:

`ALIGNMENT <name>[, <description>[, <station>[, <layer>[, <style>]]]][, TYPE=<type>][, LABELSET=<set>][, CRITERIA=<check set>][, SPEED=<speed>]`

Names are unique (any capitalisation) and cannot contain commas. An unknown style, label set or check set is refused
and nothing is created.

---

## Related

- [[Alignments]]
- [[Commands/alignmentlayout]]
- [[Command Reference]]
