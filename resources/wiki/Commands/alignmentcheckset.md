# ALIGNMENTCHECKSET

**Command:** `ALIGNMENTCHECKSET`  
**Category overview:** [[Alignments]]

Design check sets: ALIGNMENTCHECKSET NEW|DELETE|LIST|SET, ...

## Forms

- `ALIGNMENTCHECKSET NEW, <name>[, ROAD|RAIL]` makes an empty check set. Road is the default.
- `ALIGNMENTCHECKSET DELETE, <name>` removes a set. Alignments that used it lose their criteria.
- `ALIGNMENTCHECKSET LIST` lists the sets in the drawing.
- `ALIGNMENTCHECKSET SET, <name>, RADIUS, <speed>, <minRadius>` sets the minimum radius for a design speed. A radius of 0 removes that row.
- `ALIGNMENTCHECKSET SET, <name>, MINTANGENT|MINSPIRAL|MINA|MAXA, <value>` sets a limit. 0 switches that check off.

The radius table applies from each speed upward, so a curve is checked against the largest tabled speed that does not exceed its design speed. GoSurvey ships no regional standard; the values are yours to enter. A compound spiral (two spirals in a row) is not checked for length.

---

## Related

- [[Alignments]]
- [[Command Reference]]
