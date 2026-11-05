# ALIGNMENTCHECKSET

**Command:** `ALIGNMENTCHECKSET`

**Category overview:** [[Alignments]]

Design check sets: ALIGNMENTCHECKSET NEW|DELETE|LIST|SET|LOAD|SAVE, ...

## Forms

- `ALIGNMENTCHECKSET NEW, <name>[, ROAD|RAIL]` makes an empty check set. Road is the default.
- `ALIGNMENTCHECKSET DELETE, <name>` removes a set. Alignments that used it lose their criteria.
- `ALIGNMENTCHECKSET LIST` lists the sets in the drawing.
- `ALIGNMENTCHECKSET SET, <name>, RADIUS, <speed>, <minRadius>` sets the minimum radius for a design speed. A radius of 0 removes that row.
- `ALIGNMENTCHECKSET SET, <name>, MINTANGENT|MINSPIRAL|MINA|MAXA, <value>` sets a limit. 0 switches that check off.
- `ALIGNMENTCHECKSET SET, <name>, MAXDEFLECTION, <degrees>` checks the tangent intersections: a corner between two
  tangents with no curve that turns more than this is reported. 0 switches it off.
- `ALIGNMENTCHECKSET SAVE, <name>, <file>` writes a set to a criteria file; `ALIGNMENTCHECKSET LOAD, <file>` reads the
  sets in a file into the drawing, so the values you enter once can be used in other drawings.

The radius table applies from each speed upward, so a curve is checked against the largest tabled speed that does not exceed its design speed. GoSurvey ships no regional standard; the values are yours to enter. A compound spiral (two spirals in a row) is not checked for length.

---

## Related

- [[Alignments]]
- [[Command Reference]]
