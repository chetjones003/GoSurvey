# ALIGNMENTEDIT

**Command:** `ALIGNMENTEDIT`
**Category overview:** [[Alignments]]

Edit one stored parameter (the layout table uses the same rules):

`ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|STARTRADIUS|DELTA|DIRECTION|CONSTRAINT, <value>`

- **Spirals:** `LENGTH`, end **RADIUS** (0 = tangent / infinite), **STARTRADIUS** / **RS** (0 = tangent), **DIRECTION** LEFT|RIGHT. No **DELTA**.
- **Curves:** a curve keeps what it was entered with, as in Civil 3D. Editing the **RADIUS** of a curve entered by
  radius and delta (`ALIGNMENTADD … CURVE`, `ALIGNMENTINSERT … CURVE`, or made from objects) keeps its delta, so its
  length and every later station change. A curve entered by radius and length (`ALIGNMENTADD … CURVELEN`) keeps its
  length. A **Free** curve keeps the point where its two tangents meet. Editing **DELTA** or **LENGTH** keeps the radius.
- **Tangents:** **BEARING** only on **Fixed** rows.

See [[Alignments]] for constraints, locks, and continuity.

---

## Related

- [[Alignments]]
- [[Command Reference]]
