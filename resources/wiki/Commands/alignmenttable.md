# ALIGNMENTTABLE

**Command:** `ALIGNMENTTABLE` (Annotate ▸ Add Tables ▸ Alignment)
**Category overview:** [[Alignments]]

Insert a line, curve, spiral, segment or equation table: ALIGNMENTTABLE <name>, LINE|CURVE|SPIRAL|SEGMENT|EQUATIONS[, STATIC|DYNAMIC][, SPLIT=<rows>][, STYLE=<name>][, <x>, <y>]

Inserts a drawing TABLE built from the alignment. LINE lists tangents, CURVE lists curves (radius, delta, length, tangent, chord, direction), SPIRAL lists spirals (length, A, start and end radius, deflection, direction), SEGMENT lists every labelled entity in travel order, and EQUATIONS lists station equations (no labelling needed). DYNAMIC (the default) rebuilds the table after every change to the alignment; STATIC is a frozen snapshot. The table sits at the point given, or at the drawing origin.

- **Tags.** Each row is named by its segment's tag, as Civil 3D numbers them: `L1`, `L2`, … for tangents, `C1`, … for curves and `S1`, … for spirals, in travel order. Once a line, curve, spiral or segment table lists an alignment's segments, its segment labels in the drawing show just the tag.
- **Labelled segments only.** A table lists the segments that are labelled (`ALIGNMENTLABEL <name>, SEGMENTS` labels them all, `SEGMENT, <n>` one at a time). With none labelled it is refused, naming the label command.
- **Split Table.** `SPLIT=<rows>` puts at most that many data rows in each table and sets the parts side by side, each with the header row. In a dynamic split table the last part takes any rows an edit adds.
- **Table style.** The table uses the drawing's current data table style, or the one named with `STYLE=`. A point table style cannot hold alignment rows and is refused.

The Annotate tab's **Add Tables** button lists Add Point Table and, under Alignment, Add Line, Add Curve, Add Spiral, Add Segment and Add Station Equation, each for the selected alignment.

---

## Related

- [[Commands/alignmentlabel]]
- [[Alignments]]
- [[Command Reference]]
