# ALIGNMENTEDIT

**Command:** `ALIGNMENTEDIT`
**Category overview:** [[Alignments]]

Edit one stored parameter (the layout table uses the same rules):

`ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|STARTRADIUS|DELTA|DIRECTION|CONSTRAINT|DEGREE|TANGENTLENGTH|CHORD|EXTERNAL|MIDORDINATE|A, <value>`
`ALIGNMENTEDIT <name>, <entity#>, THROUGH, <x>, <y>`

- **Spirals:** `LENGTH`, end **RADIUS** (0 = tangent / infinite), **STARTRADIUS** / **RS** (0 = tangent), **DIRECTION** LEFT|RIGHT. No **DELTA**.
- **Curves:** a curve keeps what it was entered with, as in Civil 3D. Editing the **RADIUS** of a curve entered by
  radius and delta (`ALIGNMENTADD … CURVE`, `ALIGNMENTINSERT … CURVE`, or made from objects) keeps its delta, so its
  length and every later station change. A curve entered by radius and length (`ALIGNMENTADD … CURVELEN`) keeps its
  length. A **Free** curve keeps the point where its two tangents meet. Editing **DELTA** or **LENGTH** keeps the radius.
- **Tangents:** **BEARING** only on **Fixed** rows.
- **Other curve definitions.** A curve can be sized by any of Civil 3D's parameters. `DEGREE` (degree of curve, arc
  definition: degrees per 100 units of arc), `TANGENTLENGTH`, `CHORD`, `EXTERNAL` and `MIDORDINATE` each set the radius
  that gives that value at the curve's delta; `LENGTH` and `DELTA` cover curve length and curve angle. The curve
  remembers how it was entered, and the layout table's Parameter constraint column says so (for example
  "Tangent length, delta").
- **Curve through a point.** `ALIGNMENTEDIT <name>, <entity#>, THROUGH, <x>, <y>` sizes a **Free** curve so it passes
  through the point while staying tangent to the tangents either side. A point no such curve reaches is refused.
- **Spiral by A.** `ALIGNMENTEDIT <name>, <entity#>, A, <value>` sets a spiral's length from its A
  (A² = length ÷ change of curvature), keeping its radii.

See [[Alignments]] for constraints, locks, and continuity.

---

## Related

- [[Alignments]]
- [[Command Reference]]
