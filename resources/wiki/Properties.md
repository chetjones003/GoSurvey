# Properties

The **Properties** panel is docked on the left of the window, sharing space with **Reports**. It
shows and edits whatever is currently selected.

---

## With nothing selected

The panel shows the settings that new objects will inherit:

| Field | What it sets |
|---|---|
| **Layer** | Same as the ribbon layer dropdown |
| **Color** | Colour for new geometry. `ByLayer`, a named colour from the palette, or a custom colour |
| **Linetype** | Linetype for new geometry |
| **Lineweight** | Lineweight for new geometry |
| **Transparency** | Transparency for new geometry |
| **Current text style** | The text style new TEXT and MTEXT will use |
| **Default text height (in)** | Height **on the sheet, in inches**. Combined with the plot scale to give the model height of new text — see [[Annotation]] |

---

## General section

Present for every selected object. The panel header reports what is selected — `Selected: 1
object(s)` — and names the type below it.

| Field | Notes |
|---|---|
| **Layer** | The object's layer |
| **Layer list** | A dropdown of the layers present in the drawing, for picking one into **Layer** |
| **Color** | `ByLayer`, the named colour palette, or a custom colour picker |
| **Linetype** | From the bundled linetype libraries |
| **Lineweight** | Standard lineweights |
| **Transparency** | 0–100% |
| **Plot style** | Shown for reference |

Set a value to **ByLayer** to hand control back to the object's layer ([[Layers]]).

---

## Geometry by object type

### Line

| Field | Notes |
|---|---|
| Start X · Start Y | Editable — changing one moves that endpoint |
| End X · End Y | Editable |
| **Derived:** Length · Rotation rel. north | Read-only. Rotation is shown in the angle format set in `UNITS`, e.g. `270°0'0.0"` |

### Circle

| Field | Notes |
|---|---|
| Center X · Center Y · Center Z | Editable |
| Radius | Editable |
| **Derived:** Diameter · Circumference · **Area** | Read-only, computed from the radius |

This is the only place GoSurvey reports an area — there is no `AREA` command and no area for
polylines.

### Annotation — TEXT and MTEXT

| Field | Notes |
|---|---|
| **Insertion X / Y / Z** | The insertion point |
| **Content** | The text itself |
| **Rotation** | Degrees clockwise from north |
| **Height** | Text height in model units |
| **Box min X/Y**, **Box max X/Y**, **Box width**, **Box height** | The MTEXT frame. Width and height are *Derived* |
| **Edit text X / Y / Z** | Position of the editing box |

### Dimensions

`DIMALIGNED`, `DIMLINEAR`, and `DIMANGULAR` selections show their defining points and text
placement.

### Survey point

Selecting a survey marker shows *Survey — 1 point*:

| Field | Notes |
|---|---|
| **Point ID** | Must be a whole number. A duplicate is refused — *"Properties — duplicate point ID …"*; a non-integer gives *"Properties — point ID must be a whole number."* |
| **Northing (Y)** · **Easting (X)** | World coordinates |
| **Label style** | See below |
| **Label color** | Colour of the point's label |

**Label styles:**

- None
- Point number only
- Point number and description
- Description only
- Point number and elevation
- Point number, elevation, and description
- Northing and easting
- Point number, northing, and easting
- Point number, northing, easting, and elevation

Bulk editing of points is done in the Viewpoints table — the panel says
*Bulk editing: VIEWPOINTS (VWPTS)*. See [[Survey Points]].

### PDF underlay

| Field |
|---|
| File name and page number |
| Insert X · Insert Y |
| Scale · Rotation (deg) |
| Fade |
| **Object Snap** — Lines · Circles (per-underlay snap toggles) |

Selecting several PDFs shows *"N PDF underlays selected"* with a bulk **Fade** control. See
[[PDF Underlays]].

### Hatch

Hatch pattern, colour, transparency, layer, angle, and scale are edited from the contextual
**Hatch (selected)** ribbon section rather than this panel. See [[Drawing Tools]].

---

## Multiple selection

**Several objects of one type** — the shared fields are shown. Where values differ the field reads
`(mixed)` and the header says *Mixed — enter applies to all*. Typing a value applies it to every
selected object.

**A mixed-type selection** — a summary is shown instead of editable geometry, for example
`(Mixed: Line 4, Circle 2, Ann 1, PDF 0)`, along with the General section, which still applies to
all of them.

---

## Reports tab

The second tab of the same panel. Generated reports arrive here as tabs:

- **ALIGN** transformation reports — parameters and per-pair point errors
- **Traverse** results from the Traverse Editor
- **Exported points** reports from `EXPORTPOINTS`

> *Exported CSV files and other generated reports will appear here as tabs.*

---

## Related

[[Layers]] · [[Object Selection]] · [[Annotation]] · [[Survey Points]] · [[PDF Underlays]]
