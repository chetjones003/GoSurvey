# ALIGNMENTINSERT

**Command:** `ALIGNMENTINSERT`
**Category overview:** [[Alignments]]

Insert a tangent, circular curve or clothoid spiral after an existing entity:

- `ALIGNMENTINSERT <name>, <after#>, TANGENT, <length>`
- `ALIGNMENTINSERT <name>, <after#>, CURVE, <radius>, <delta deg>, LEFT|RIGHT`
- `ALIGNMENTINSERT <name>, <after#>, SPIRAL, <length>, <end radius>, LEFT|RIGHT`
- `ALIGNMENTINSERT <name>, <after#>, SPIRAL, <length>, <start radius>, <end radius>, LEFT|RIGHT` (use **0** for a tangent/infinite radius)

The new entity is *Float (prev)* and re-solves the chain tangent-continuously.

See [[Alignments]] for the full workflow, the station format, and what each phase includes.

---

## Related

- [[Alignments]]
- [[Command Reference]]
