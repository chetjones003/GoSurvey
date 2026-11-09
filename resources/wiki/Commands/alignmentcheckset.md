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
- `ALIGNMENTCHECKSET SET, <name>, SUPERELEVATION, <rate>` sets the maximum superelevation rate (for example 0.06). A curve whose required superelevation is above it is reported. 0 switches it off.
- `ALIGNMENTCHECKSET SET, <name>, FRICTION, <factor>` sets the side friction factor used for that check (default 0, never negative).
- `ALIGNMENTCHECKSET SET, <name>, SIGHT, <feet>` sets the minimum sight distance on a curve.
- `ALIGNMENTCHECKSET SET, <name>, CLEARANCE, <feet>` sets the lateral clearance from the inside lane centreline to a sight obstruction. The sight check runs only when both SIGHT and CLEARANCE are above 0.
- `ALIGNMENTCHECKSET SAVE, <name>, <file>` writes a set to a criteria file; `ALIGNMENTCHECKSET LOAD, <file>` reads the
  sets in a file into the drawing, so the values you enter once can be used in other drawings.

The radius table applies from each speed upward, so a curve is checked against the largest tabled speed that does not exceed its design speed. GoSurvey ships no regional standard; the values are yours to enter. A compound spiral (two spirals in a row) is not checked for length. Superelevation and sight distance are checked on circular curves only. The formulas are e + f = V² / gR (V in ft/s, g = 32.174 ft/s²) and S = 2R·acos(1 − m / R), capped at the curve's arc length.

---

## Related

- [[Alignments]]
- [[Command Reference]]
