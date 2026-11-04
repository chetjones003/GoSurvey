# REVERSEALIGNMENT

**Command:** `REVERSEALIGNMENT` — aliases: `REVERSEALIGNDIRECTION`
**Category overview:** [[Alignments]]

Reverse alignment direction and stationing: REVERSEALIGNMENT <name>

The alignment must be continuous (no gaps). Its geometry stays where it is: tangents, curves and spirals run the other
way, curves turn the other way, and a station S becomes start + length − S. Reversing twice gives back the original.

What hangs off the stationing follows, so it stays put on the ground:

- **Station / offset labels** move to the reversed station, and right becomes left.
- **Design speeds** apply from the other end. A stretch that had no speed takes the speed next to it (reported).
- **Linked offset alignments** reverse with it and keep their side.
- Station equations are cleared (reported); add them again with `ALIGNMENTSTATION`.

See [[Alignments]] for the full workflow, the station format, and what each phase includes.

---

## Related

- [[Alignments]]
- [[Command Reference]]
