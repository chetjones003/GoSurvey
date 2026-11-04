# ALIGNMENTREPORT

**Command:** `ALIGNMENTREPORT`
**Category overview:** [[Alignments]]

Report an alignment as text or CSV: ALIGNMENTREPORT [<name>][, TXT|CSV[, <path>]]

Prints the alignment's name, type, stations, length, bearings, station equations, the continuity check and each
tangent and curve in travel order. With `TXT` or `CSV` it writes a file instead (a file dialog opens when no path is
given). The CSV has one row per entity with a header row, and numbers are written with a `.` decimal point.

Design-criteria findings are not in the report yet; they arrive with design criteria (REQ-502).

---

## Related

- [[Alignments]]
- [[Command Reference]]
