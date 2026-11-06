# ALIGNMENTLOCK

**Command:** `ALIGNMENTLOCK`
**Category overview:** [[Alignments]]

Lock a parameter: ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING|STATION, ON|OFF

A locked value refuses any edit that would change it, and the refusal names the entity. **STATION** keeps the entity's
start where it is along the alignment: editing an earlier entity, inserting before it or deleting it is refused,
naming every locked entity that would move and where it would move to. Changing the alignment's starting station only
relabels the stations and is allowed.

See [[Alignments]] for the full workflow and the station format.

---

## Related

- [[Alignments]]
- [[Command Reference]]
