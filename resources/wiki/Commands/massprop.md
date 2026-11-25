# MASSPROP

**Command:** `MASSPROP` — aliases: `MASSPROPERTIES`, `SOLIDMASSPROP`, `MASSP`
**Category overview:** [[Inquiry Commands]]

Reports mass properties for every solid in the drawing, at unit density (mass = volume).

For each solid the command log lists:

- volume, surface area and centroid
- inertia tensor about the centroid and about the world origin
- principal moments and principal axes

A self-intersecting or invalid solid is listed, but its values are not reported. Inertia is marked
unavailable for face shapes not yet covered.

---

## Related

- [[Inquiry Commands]]
- [[Command Reference]]
