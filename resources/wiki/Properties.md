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

The panel identifies each selected object by type. For a single selection, **Object details** also
shows its object index and reports read-only geometry and model data for object types that do not
use the editable line, circle, or annotation fields. Examples include polyline
vertex count, closure and elevation range; arc center, radius and sweep; ellipse center, major radius
and axis ratio; feature-line name, description and vertex count; and pipe-run name, nominal size and
route vertex count. TIN surfaces and alignments have their own detail sections. These values come
from the drawing model and are recalculated when geometry changes.

### Line

| Field | Notes |
|---|---|
| Start X · Start Y | Editable — changing one moves that endpoint |
| End X · End Y | Editable |
| **Derived:** Length · Rotation rel. north | Read-only. Rotation is shown in the angle format set in `UNITS`, e.g. `270°00'00.0"` |

### Circle

| Field | Notes |
|---|---|
| Center X · Center Y · Center Z | Editable |
| Radius | Editable |
| **Derived:** Diameter · Circumference · **Area** | Read-only, computed from the radius |

A circle's area is also listed here. `AREA` reports typed points or a closed polyline or circle.

### Polyline, arc and ellipse

These objects show read-only geometry in **Object details**: polylines report vertex count, closure
elevation range, planar and spatial lengths, and closed area; arcs report center, radius, sweep and
length; ellipses report center, major and minor radii, major-axis rotation, sweep, curve length, and
area (or arc-segment area for a partial ellipse).

### Feature line, surface and alignment

Feature lines report their name, description, vertex count, closure, elevation range, and plan and
spatial lengths, plus grading criteria when present. TIN surfaces report point and triangle counts,
elevation range and definition state. Alignments
report station range, length, endpoints, bearings and design criteria where configured.

### 3D and reference objects

Pipe runs report their name, nominal size, route vertices and length, pressure class, and stated wall
thickness. Solids show topology counts and XYZ bounds; meshes show source, vertex and triangle
counts, and bounds; block references show definition, insertion, scale, rotation, and attribute
count; tables show rows, columns, insertion, dimensions, and style; filled regions show loops,
vertices, and pattern; multileaders show branches, path vertices, text, and style; PDF underlays show
file, page, insertion, scale, rotation, and page size. Point clouds show source and total/preview
counts. A single selected survey point shows ID, easting, northing, elevation, description, and raw
code. Values are read-only; use the relevant contextual tools or manager to edit the object.

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

`DIMALIGNED`, `DIMLINEAR`, and `DIMANGULAR` selections show their label insertion point, text
content, and per-dimension size overrides:

| Field | Notes |
|---|---|
| **Text height** | Optional one-off text height in **model units** (*By style* when blank). Stays that exact size in the drawing at every annotation scale. Clear the field (or enter `0`) to follow the dimension style again |
| **Arrow size** | Optional one-off arrowhead length in **model units** (*By style* when blank). Wins over the dimension style, annotation scale, Overall scale, and the drawing-wide **Dimension arrow size** multiplier. Clear the field (or enter `0`) to follow the style again |
| **Model text height** · **Model arrow size** *(Derived)* | Live drawn sizes at the current annotation scale, with any overrides applied |

### Section plane

Selecting the section plane shows whether clipping is active, whether the plane follows the current
UCS or a picked face, its origin and normal, offset, flip state, and whether its extent follows the
model bounds or has a custom width and height.

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

### Editing text in the Qt editor

Double-click TEXT or MTEXT to open its editor. MTEXT offers font, bold, italic, underline and color
controls for selected characters. Save applies the edit in one undo step; Cancel leaves the text
unchanged. Both TEXT and MTEXT expose rotation in Properties. MTEXT rotation retains the box's local
width and height so its wrapping stays stable.

Properties input fields select their contents when focused. Layer and annotative-scale dropdowns
use scrolling lists; the open list stays alive while it handles a selection.
