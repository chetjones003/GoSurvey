# ALIGNMENTREPORT

**Command:** `ALIGNMENTREPORT`
**Category overview:** [[Alignments]]

Report an alignment as text or CSV: ALIGNMENTREPORT [<name>][, TXT|CSV[, <path>]]

Prints the alignment's name, type, stations, length, bearings, station equations, the continuity check and each
tangent and curve in travel order. With `TXT` or `CSV` it writes a file instead (a file dialog opens when no path is
given). The CSV has one row per entity with a header row. Station values use Civil 3D-style notation such as
`1+23.45`; numeric columns retain the full available precision and use a `.` decimal point.

Design-criteria findings are not in the report yet; they arrive with design criteria (REQ-502).

---

## Related

- [[Alignments]]
- [[Command Reference]]
