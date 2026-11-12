# BTABLE

**Command:** `BTABLE`
**Category overview:** [[Import and Export]]

In the block editor, open the Block Properties Table for the block being edited. Columns refer to existing block parameters. Each row stores a valid combination of values; numeric values must respect parameter limits and visibility values must match a visibility state.

Add or remove columns and rows, edit a cell by double-clicking it, or clear the table. Adding a row with no entered values uses each column's current parameter value. `BSETROW <row>` applies one row to selected block inserts.

The table is saved in GoSurvey drawing data. It is not written as a native AutoCAD block properties table during DWG export; the export loss summary reports this.

See [[Commands/bedit]] and [[Command Reference]] for related tools.

---

## Related

- [[Commands/bedit]]
- [[Commands/bsetrow]]
- [[Commands/parameters]]
- [[Command Reference]]
