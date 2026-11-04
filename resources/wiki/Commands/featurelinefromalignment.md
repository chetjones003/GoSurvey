# FEATURELINEFROMALIGNMENT

**Command:** `FEATURELINEFROMALIGNMENT`
**Category overview:** [[Alignments]]

Feature line from an alignment: FEATURELINEFROMALIGNMENT <alignment>[, INTERVAL=<n>][, TOL=<n>][, SURFACE=<name> | Z=<value>]

Makes a feature line from an alignment (REQ-498). It includes the start, the end, every tangent and curve junction (so the
PC and PT are vertices), a station every `INTERVAL` (default 10), and enough points on each curve that the straight
chord stays within `TOL` (default 0.01) of the true arc. Z comes from `SURFACE=` (the ground under each station, REQ-497)
or from `Z=` (one constant). A station with no ground on the surface makes nothing and says where.

The feature line stays linked to its alignment. Editing the alignment rebuilds it. If the alignment is erased, the line
stays as an ordinary feature line and the log says so. Feature lines can also be used as the source of
`CREATEALIGNMENTFROMOBJECTS`; their Z is ignored there.

---

## Related

- [[Alignments]]
- [[Command Reference]]
