# ADDPOINTTABLE

**Command:** `ADDPOINTTABLE` — no alias
**Ribbon:** Annotate → Labels & Tables → Add Tables
**Category overview:** [[Annotation]]

Make a table of survey points in a point table style. Each survey point is one row, below a header row.

## In the editor: the dialog

Choosing **Add Tables** (or typing `ADDPOINTTABLE`) opens **Add Point Table**:

- **Table style** — `NameNEZD` or `NameNEZD with Comments`.
- **Point group** — pick a group to check all of its points at once. The check boxes then show the group's members.
- **Survey point list** — tick the points you want. Each row shows the point number, northing, easting, elevation
  and description.

Press **OK** and then click where the table's upper-left corner goes. **Cancel** changes nothing.

## Typed prompts

Without the dialog (for example in a script), the command asks the same questions on the command line:

```
ADDPOINTTABLE — point table style (NameNEZD) or Enter:
ADDPOINTTABLE — points: numbers such as 1-5,8, a point group name, or Enter for all survey points:
ADDPOINTTABLE — 5 point(s). Specify the insertion point (upper-left corner):
```

Type point numbers such as `1-5,8`, a point group name, or press **Enter** for every survey point.

## What the table contains

- Rows are in point number order.
- The first row is a header with the column names.
- **NameNEZD** columns: Point Name, Northing, Easting, Elevation.
- **NameNEZD with Comments** adds a Description column with each point's description.

The table goes on the current layer. A drawing with no survey points is refused.

> **Check before relying on the layout.** The NameNEZD column list follows the point file layout used in
> common survey workflows. It has not yet been checked against the same table style in Civil 3D.

## Related

- [[Commands/table]]
- [[Survey-Points]]
- [[Annotation]]
- [[Command Reference]]
