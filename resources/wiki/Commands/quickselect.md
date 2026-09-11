# QUICKSELECT


![QUICKSELECT command overview](wiki-img:commands/quickselect.png)
**Command:** `QUICKSELECT` — aliases: `QS`
**Category overview:** [[Object Selection]]

**Command:** `QUICKSELECT`, `QS` **Menu:** Right-click ▸ Quick Select…

Builds a selection by **property** instead of by picking. Useful when you want every line on one
layer, or every survey point above a given elevation.

```
QUICKSELECT — filter entities by type and property.
```

![Quick Select](wiki-img:06-quick-select.png)

### The dialog

| Field | Choices |
|---|---|
| **Apply to:** | Entire drawing, or Current selection |
| **Object type:** | Line, Polyline, Circle, Arc, Ellipse, Text, MText, Dim (Aligned), Dim (Linear), Dim (Angular), PDF Underlay, survey point |
| **Properties:** | The properties that apply to the chosen type — Layer, Color, ID, Elevation, Easting, Northing, Description, Closed, and so on |
| **Operator:** | `= Equals`, `<> Not Equal`, `> Greater Than`, `< Less Than` |
| **Value:** | A dropdown where the property has known values (the named colour palette, the layers present in the drawing, Yes/No) or a typed value otherwise |
| **How to apply:** | Include in new selection · Exclude from new selection · Append to current selection |

Press **Select All** to apply, or **Cancel** to close without changing the selection.

### Example — select every line on the `TOPO` layer

1. `QS`
2. **Apply to:** Entire drawing
3. **Object type:** Line
4. **Properties:** Layer · **Operator:** `= Equals` · **Value:** `TOPO`
5. **How to apply:** Include in new selection
6. **Select All**

---

---

## Related

- [[Object Selection]]
- [[Command Reference]]