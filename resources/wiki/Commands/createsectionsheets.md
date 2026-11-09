# CREATESECTIONSHEETS

**Command:** `CREATESECTIONSHEETS`
**Category overview:** [[Alignments]]

Create section sheets from sample lines: CREATESECTIONSHEETS <alignment>, <sample lines>, <sections per sheet>, <axis elevation>

Typed without arguments, the command opens the Create Section Sheets dialog. `<sample lines>` is `ALL` or sample line names separated by spaces, such as `0+00 0+50`. `<sections per sheet>` is a whole number from 1 to 10. `<axis elevation>` is the elevation each section's horizontal axis is drawn at.

Each sheet is an ARCH D paper layout named `SEC-1`, `SEC-2`, ..., built on the Generic 24in x 36in Title Block.dwg from the AutoCAD Template folder. The chosen sections go on the sheets from left to right in station order. Each section is drawn as paper geometry: its ground line, its axis, its station label beneath, and a `no data` label where the surface gave no ground. All the sections on a sheet share one horizontal and one vertical scale, printed on the sheet.

The command needs at least one chosen line that has a section; lines made without a surface do not count. All the sheets from one command are one undo step.

See [[Alignments]] for the scale rules, the refusals, and what is not built yet.

---

## Related

- [[Alignments]]
- [[Command Reference]]
- [[Commands/createsamplelines]]
