# PROFILEPVI

**Command:** `PROFILEPVI`
**Category overview:** [[Alignments]]

Add or edit a vertical point of a design profile: PROFILEPVI <profile>, <station>, <elevation>[, <curve length>]

The station is a displayed station on the profile's alignment, such as `2+50`. If a point already sits at that station it is changed to the new elevation and curve length, and it keeps its station. Leaving out the curve length means no curve. A curve is a symmetric parabola of that length centred on the point, so it runs half its length either side of the point. The first and last points cannot carry a curve, and two curves on one tangent cannot overlap; a change that breaks either rule is refused and the profile is not changed. Add the next point after a point before you give that point a curve, because a curve needs a point on both sides.

---

## Related

- [[Alignments]]
- [[Command Reference]]
- [[Commands/createdesignprofile]]
