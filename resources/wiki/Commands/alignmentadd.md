# ALIGNMENTADD

**Command:** `ALIGNMENTADD`
**Category overview:** [[Alignments]]

Add geometry to an alignment (continues tangent-continuously when the chain already has entities):

- `TANGENT, <x>, <y>, <azimuth deg>, <length>` — first tangent (world coordinates).
- `TANGENT, <length>` — continue a tangent.
- `TANGENTTO, <x>, <y>` — tangent to a world point.
- `CURVE, <radius>, <delta deg>, LEFT|RIGHT` — circular curve by central angle.
- `CURVELEN, <radius>, <arc length>, LEFT|RIGHT` — circular curve by arc length.
- `SPIRAL, <length>, <end radius>, LEFT|RIGHT` — clothoid from tangent (start radius infinite).
- `SPIRAL, <length>, <start radius>, <end radius>, LEFT|RIGHT` — use **0** for infinite / tangent.

See [[Alignments]] for the full workflow and station format.

---

## Related

- [[Alignments]]
- [[Command Reference]]
