# ALIGNMENTSURFACE

**Command:** `ALIGNMENTSURFACE`
**Category overview:** [[Alignments]]

Ground elevation along an alignment from a surface: ALIGNMENTSURFACE <alignment>, <surface>, STATION=<sta>[, OFFSET=<off>] | INTERVAL=<n>

Reads the ground under an alignment from an existing surface (REQ-497). Elevations are shown on screen and are not
saved with the alignment. A point outside the alignment or the surface has no ground: it is reported as
`no ground`, never as zero.

- `STATION=12+45.32` with an optional `OFFSET=-15.42` reports the ground at one point (`EG = ...`).
- `INTERVAL=10` lists the ground every 10 units, plus the start, the end and every curve and tangent junction.

---

## Related

- [[Alignments]]
- [[Command Reference]]
