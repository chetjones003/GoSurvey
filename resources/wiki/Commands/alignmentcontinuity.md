# ALIGNMENTCONTINUITY

**Command:** `ALIGNMENTCONTINUITY`
**Category overview:** [[Alignments]]

Set or report geometric continuity checks (REQ-482):

- `ALIGNMENTCONTINUITY <name>, C0|C1|C2` — sets the level used for warnings at entity junctions (default **C1**).
- `ALIGNMENTCONTINUITY <name>, LIST` — prints the current level and any junction warnings at that level.

**C2** applies at junctions where a **spiral** is involved (curvature must match). Tangent→curve junctions are not flagged for C2 even when the level is C2.

See [[Alignments]] for the full workflow.

---

## Related

- [[Alignments]]
- [[Command Reference]]
