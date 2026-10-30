# TABLE

**Command:** `TABLE` — no alias
**Category overview:** [[Annotation]]

Insert an empty table in model space: a grid of cells you fill in later.

```
TABLE — table style (Standard) or Enter:
TABLE — number of columns <3>:
TABLE — number of rows <2>:
TABLE — specify the insertion point (upper-left corner):
```

1. **Style** — type `Standard` (the only data style), or press **Enter**.
2. **Columns** — a whole number from 1 to 50. Enter takes 3.
3. **Rows** — a whole number from 1 to 500. Enter takes 2.
4. **Insertion point** — click the upper-left corner of the table, or type `X,Y`.

The table takes its column width and row height from its style. The text height comes from the style too.

## Filling in the cells

**Double-click** a cell to type its text. Press **Enter** to keep the new text, or **Cancel** to leave the
cell as it was. One **Undo** restores the last cell you changed.

## Table styles

| Style | Kind | Use |
|---|---|---|
| `Standard` | Data | The default for TABLE |
| `NameNEZD` | Point | Used by [[Commands/addpointtable]] |
| `NameNEZD with Comments` | Point | Used by [[Commands/addpointtable]] |

Style editing and saving custom styles are not available yet.

## Related

- [[Commands/addpointtable]]
- [[Annotation]]
- [[Command Reference]]
