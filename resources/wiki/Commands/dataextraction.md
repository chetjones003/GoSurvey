# DATAEXTRACTION

**Command:** `DATAEXTRACTION` — no alias
**Ribbon:** Annotate → Tables → Extract Data
**Category overview:** [[Annotation]]

Write a table of the selected objects to a spreadsheet file: one row per object.

## How it works

1. **Select the objects first.** If nothing is selected, the command stops and asks you to select them.
2. Run `DATAEXTRACTION`. It shows the number of objects and asks for the columns:

```
DATAEXTRACTION — 3 object(s). Columns: TYPE, LAYER, TEXT, X, Y, Z separated by commas, or Enter for all:
```

3. Type the columns you want, in the order you want them — for example `TYPE,X,Y` — or press **Enter** for all six.
4. Choose a file. Its name ending in `.xlsx` writes an Excel workbook; `.csv` writes comma-separated text. A name
   with neither ending gets `.xlsx`.

## Columns

| Column | Value |
|---|---|
| `TYPE` | The object type: `LINE`, `CIRCLE`, `POLYLINE`, `POINT`, `TEXT`, `MTEXT`, `DIMENSION` and so on |
| `LAYER` | The object's layer |
| `TEXT` | The text of a text object or dimension; blank for other objects |
| `X`, `Y`, `Z` | The object's defining point (see below); blank when the object has none |

**Defining point:** the insertion point of text, annotations and dimensions; the point itself for `POINT`; the
centre for a circle; the first vertex for a line or polyline.

Rows follow the selection order. Values are in drawing units.

## Not yet available

- Writing the table into a drawing table instead of a file.
- Excel files that stay linked to the drawing (`DATALINK`).
- Paper-space selections (the command refuses them and says so).

## Related

- [[Commands/table]]
- [[Annotation]]
- [[Command Reference]]
