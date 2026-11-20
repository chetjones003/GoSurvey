# TABLE

**Command:** `TABLE` — no alias
**Category overview:** [[Annotation]]

Insert an empty table in model space: a grid of cells you fill in later.

In the Qt editor, `TABLE` opens **Insert Table**. Choose the drawing's data-table style, column and row counts,
column width, row height and cell-style defaults. The preview updates as you change the counts. Press **OK**,
then click the table's upper-left insertion point. **Cancel** ends the command without adding a table.

Scripted and headless runs use the typed prompts below.

```
TABLE — table style (Standard) or Enter:
TABLE — number of columns <3>:
TABLE — number of rows <2>:
TABLE — specify the insertion point (upper-left corner):
```

1. **Style** — type a data-table style such as `Standard`, or press **Enter**.
2. **Columns** — a whole number from 1 to 50. Enter takes 3.
3. **Rows** — a whole number from 1 to 500. Enter takes 2.
4. **Insertion point** — click the upper-left corner of the table, or type `X,Y`.

The table takes its column width and row height from the Insert Table values. Text height comes from the style.

## Filling in the cells

**Double-click** a cell to type its text. Press **Enter** to keep the new text, or **Cancel** to leave the
cell as it was. One **Undo** restores the last cell you changed.

## Table styles

| Style | Kind | Use |
|---|---|---|
| `Standard` | Data | The default for TABLE |
| `NameNEZD` | Point | Used by [[Commands/addpointtable]] |
| `NameNEZD with Comments` | Point | Used by [[Commands/addpointtable]] |

Custom table-style editing is not available yet. GoSurvey's DWG export currently writes tables as visible,
editable line and text geometry; it does not write an AutoCAD-native `TABLE` entity or native `TABLESTYLE`
records. Open a DWG in AutoCAD to verify this limitation before relying on table-cell editing there.

## Related

- [[Commands/addpointtable]]
- [[Annotation]]
- [[Command Reference]]
