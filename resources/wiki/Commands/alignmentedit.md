# ALIGNMENTEDIT

**Command:** `ALIGNMENTEDIT`
**Category overview:** [[Alignments]]

Edit one stored parameter (the layout table uses the same rules):

`ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|STARTRADIUS|DELTA|DIRECTION|CONSTRAINT, <value>`

- **Spirals:** `LENGTH`, end **RADIUS** (0 = tangent / infinite), **STARTRADIUS** / **RS** (0 = tangent), **DIRECTION** LEFT|RIGHT. No **DELTA**.
- **Curves:** editing **RADIUS** keeps arc length; editing **DELTA** keeps radius.
- **Tangents:** **BEARING** only on **Fixed** rows.

See [[Alignments]] for constraints, locks, and continuity.

---

## Related

- [[Alignments]]
- [[Command Reference]]
