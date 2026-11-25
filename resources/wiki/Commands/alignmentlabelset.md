# ALIGNMENTLABELSET

**Command:** `ALIGNMENTLABELSET`
**Category overview:** [[Alignments]]

Manages alignment label sets: which labels an alignment shows, and their styles and intervals.

| Usage | Effect |
|---|---|
| `ALIGNMENTLABELSET LIST` | List sets and how many alignments use each |
| `ALIGNMENTLABELSET NEW <set>` | Create a set (copied from Standard) |
| `ALIGNMENTLABELSET DELETE <set>` | Delete an unused set (`Standard` cannot be deleted) |
| `ALIGNMENTLABELSET ASSIGN <alignment>, <set>` | Use a set on an alignment |
| `ALIGNMENTLABELSET MAJORINTERVAL <set>, <interval>` | Major station interval |
| `ALIGNMENTLABELSET MINORINTERVAL <set>, <interval>` | Minor station interval |
| `ALIGNMENTLABELSET SHOW\|HIDE <set>, <kind>` | Kind: `MAJOR`, `MINOR`, `EQUATIONS`, `GEOMETRY`, `SPEEDS`, `PROFILE`, `SUPERELEVATION` |
| `ALIGNMENTLABELSET STYLE <set>, <kind>, <label style>` | Kind: `MAJOR`, `GEOMETRY`, `SPEEDS`, `EQUATIONS`, `SEGMENTS`, `OFFSETS` |
| `ALIGNMENTLABELSET GEOMETRY <set>, CODES, <codes>\|ALL` | Which geometry points are labelled |
| `ALIGNMENTLABELSET GEOMETRY <set>, ADD, <label style>, <codes>` | Add a geometry-point label entry |
| `ALIGNMENTLABELSET GEOMETRY <set>, REMOVE, <n>` | Remove entry *n* |

See also [[Commands/alignmentlabel]] and [[Commands/alignmentlabelstyle]].

---

## Related

- [[Alignments]]
- [[Command Reference]]
