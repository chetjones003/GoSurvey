# PROFILELIST

**Command:** `PROFILELIST`
**Category overview:** [[Alignments]]

List the design profiles in the drawing: PROFILELIST

Each profile is listed with its alignment and its number of points. A profile whose alignment has been deleted is listed as `alignment missing`, and a profile whose points are not a valid profile (stations out of order, a curve at an end, overlapping curves, or a grade too steep to represent) is marked `geometry not valid` with the reason. Such a profile is kept, not removed; delete it with PROFILEDELETE if you no longer need it.

---

## Related

- [[Alignments]]
- [[Command Reference]]
- [[Commands/createdesignprofile]]
