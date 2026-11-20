# CREATESAMPLELINES

**Command:** `CREATESAMPLELINES`
**Category overview:** [[Alignments]]

Create sample lines along an alignment: CREATESAMPLELINES <alignment>, <start station>, <end station>, <interval>, <left swath>, <right swath>[, <surface>]

Typed without arguments, the command opens the Sample Lines dialog. The stations are displayed stations, as in CREATEVIEWFRAMES. The interval and the swath widths are in drawing units. The optional `<surface>` is a TIN surface: each line's section is sampled from it when the lines are made. Leave it out to make lines with no sections.

One sample line is placed perpendicular to the alignment at each interval station from the start to the end station, and each is named by its station (for example `0+50`). The lines are drawn in plan. All the lines from one command are one undo step.

See [[Alignments]] for what a section is, the refusals, and what is not built yet.

---

## Related

- [[Alignments]]
- [[Command Reference]]
- [[Commands/createsectionsheets]]
