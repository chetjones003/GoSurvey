# Survey Points

Survey points are **not ordinary CAD geometry**. They live in their own database inside the
drawing, each with an ID, coordinates, an elevation, a description, a layer, and a label style.
Deleting a point removes its label with it; importing a DXF keeps them.

**World X = Easting, World Y = Northing, World Z = Elevation.**

| Command | Alias | What it opens |
|---|---|---|
| `CREATEPOINTS` | `CRTPTS` | The Create points panel |
| `VIEWPOINTS` | `VWPTS` | The Viewpoints table |
| `IMPORTPOINTS` | `IMPPTS` | CSV import |
| `EXPORTPOINTS` | `EXPPTS` | CSV export |

---

## What a point holds

| Field | Notes |
|---|---|
| **ID** | The point number. A whole number, unique in the drawing |
| **Easting** · **Northing** · **Elevation** | World coordinates |
| **Description** | The office description. Editable |
| **Raw description** | The field code exactly as the crew collected it. **Never rewritten when you edit the description** — so a point group keyed on the raw code keeps matching after the office edits the description |
| **Layer** | Per point |
| **Label style** | Which fields the on-drawing label shows |

Points are drawn as a screen-facing **X** marker, sized from the plot scale so it stays readable at
any zoom and any view orientation.

---

## Creating points

**Ribbon:** Survey → Points **Command:** `CREATEPOINTS`, `CRTPTS`

> *Click in the drawing to place points. Clicks on existing markers select them.*

The panel stays open and **every click in the drawing places a point** — there is no separate
toggle to arm. Clicking an existing marker selects it rather than stacking a new point on it.

![Create points panel](wiki-img:07-create-points.png)

### Panel fields

| Field | What it does |
|---|---|
| **Next point ID** | The number the next point gets. Increments as you place |
| **Layer** | Layer for new points |
| **Description** | Default description for new points |
| **Elevation** | Default elevation for new points |
| **If ID exists** | The duplicate-ID policy — see below |
| **File** · **Save** / **Load** | Save or load the point database as JSON (`gosurvey_points.json`) |

### Duplicate-ID policy

Used when a new or moved point would collide with an existing ID:

| Policy | Effect |
|---|---|
| **Notify (skip)** | Leave the existing point alone and report it |
| **Renumber (next free)** | Give the incoming point the next unused number |
| **Merge (update coords)** | Keep the existing point, update its coordinates |
| **Overwrite** | Replace the existing point |

The same policy applies when copying or moving points.

Press **Esc** to close the panel — the log says `Selection cleared; CREATEPOINTS closed.`

---

## Viewing and editing points — `VIEWPOINTS` (`VWPTS`)

Opens **Viewpoints — survey database**, a table of every point with a footer count
(`N point(s)`).

![Viewpoints table](wiki-img:02-viewpoints.png)

| Column | Editable |
|---|---|
| **ID** | Yes — must be a whole number, and unique |
| **Easting** | Yes |
| **Northing** | Yes |
| **Elev** | Yes |
| **Layer** | Yes |
| **Description** | Yes |
| **Del** | Deletes that row |

Coordinates are shown and edited in **world** values.

Rejected edits are reported: `VIEWPOINTS — ID must be a whole number (no spaces or extra text).`
and `VIEWPOINTS — duplicate ID …`.

**Save** and **Load** write and read the whole database as JSON, defaulting to
`gosurvey_points.json`.

---

## Point labels

Each point can carry a label linked to it. The label follows the point when it moves, and is
removed when the point is deleted.

### Label styles

Set per point in the Properties panel:

- None
- Point number only
- Point number and description
- Description only
- Point number and elevation
- Point number, elevation, and description
- Northing and easting
- Point number, northing, and easting
- Point number, northing, easting, and elevation

Labels using `{north}` and `{east}` placeholders display **world** coordinates.

### Label templates and placement

**Settings → Drafting** carries the point-display settings:

| Setting | What it controls |
|---|---|
| **Show point ID in viewport** | Whether the number is drawn next to the marker |
| **Coordinate display precision (decimals)** | Decimals for survey-point coordinates and labels, independent of the general display precision |
| **Cross span (plotted inches)** | Horizontal span of the marker X **on paper**. World size = span × model units per plotted inch |
| **Label center east of point (plotted in)** | Label offset, in plotted inches |
| **Label center north of point (plotted in)** | Label offset, in plotted inches |
| **Leader arrow** — Color, Arrow half-width (px) | The leader shown when a label is dragged away from its point |

Each label style has an **editable template**. Press **Edit Template** to open the editor, which
offers:

- **Insert attribute** — Number, Northing, Easting, Elevation, Description
- **Formatting** — Bold on/off, Italic on/off, colour tags
- Tags: `[[b]]`, `[[i]]`, `[[u]]`, `[[color:RRGGBB]]`, `[[/color]]`

**Apply to all points** / **Apply all templates to survey points** pushes the edited templates onto
the existing points.

Because labels are positioned in **plotted inches**, changing the plot scale repositions every
label so the sheet keeps looking the same. See [[Plotting]].

---

## Importing points from CSV — `IMPORTPOINTS` (`IMPPTS`)

### The dialog

1. **Browse…** to the CSV file.
2. **Column order** — pick the layout that matches the file:

| Preset | Columns |
|---|---|
| `P,N,E,Z,D` | Point ID, northing, easting, Z, description |
| `P,E,N,Z,D` | Point ID, easting, northing, Z, description |
| `N,E,Z` | Northing, easting, Z — **IDs assigned on import** |
| `E,N,Z` | Easting, northing, Z — **IDs assigned on import** |

3. **First row is header (skip)** — tick if the file has a header line.
4. Check the **File preview** and the **Validation summary**. **Refresh preview** re-reads the file.
5. **Import**. If some rows are bad you are asked to confirm:
   *Import N valid row(s) and skip M bad row(s)?*

Per-row problems are reported in the command log with the line number and reason — for example
`E (easting) is not a valid number` or `duplicate ID …`. If the list is long, the log says
*(Additional problem lines omitted from this list.)*

### Precision

CSV coordinates are **world** (state-plane) values. GoSurvey converts them into the drawing's
internal frame in double precision before storing, so imported points land exactly on matching DXF
geometry rather than a fraction of a foot away.

### Getting points out of Civil 3D

Civil 3D COGO points are custom objects and are **not** written as standard DXF entities, so they
cannot be recovered from a plain DXF. Export them from Civil 3D as a **PNEZD or PENZD CSV** and
import that; use DXF for the linework. Either order works — see [[Import and Export]].

---

## Exporting points to CSV — `EXPORTPOINTS` (`EXPPTS`)

1. Choose the output path (defaults to `points.csv`).
2. **Column order** — the same four presets.
3. **Write header row** — optional.
4. **Export.**

Export writes **world** coordinates and adds a tab to the **Reports** panel with a summary.

Failures are reported plainly: `EXPORTPOINTS — could not write <path>` or
`EXPORTPOINTS — no file path.`

---

## Points and DXF

- **Export** writes each point as a native `POINT` / `AcDbPoint` at world coordinates with its own
  layer and ByLayer colour, plus `$PDMODE` / `$PDSIZE` in the header so it displays as an X. Point
  identity — ID, label style, description — travels in `GOSURVEY` XDATA, so a GoSurvey DXF
  round-trips with full survey identity while any other reader still sees a valid POINT.
- **Import** rebuilds a point from that XDATA. A `POINT` **without** it imports as a plain snappable
  cross marker.
- **Importing a DXF keeps the survey points already in the session** and replaces only the CAD
  geometry. Reconstructed points are merged with the existing ones; a colliding ID prompts you to
  **overwrite** or **offset** the imported IDs.

Full detail: [[Import and Export]]

---

## Points in other commands

| Command | Behaviour with points |
|---|---|
| **Object snap** | The **Survey point** snap targets marker positions |
| `MOVE` / `COPY` | Points move with the selection; duplicate IDs trigger the policy dialog |
| `DELETE` | Deletes selected points **and** their labels in one step |
| `ALIGN` | Tags source points ` ADJ` and destination points ` CON` — see [[Coordinate Alignment]] |
| `INVERSE` | Snap to two points for distance and bearing |
| **Point groups** | Rule-based selections of points — see [[Point Groups and Surfaces]] |
| **Surfaces** | TIN surfaces are triangulated from point groups |

---

## Common problems

| Problem | Cause | Solution |
|---|---|---|
| Import brings in nothing | Column order does not match the file, or every row failed validation | Check the **File preview**, change **Column order**, tick **First row is header** |
| Points land far from the linework | The CSV and the drawing are in different coordinate systems | Use `ALIGN` with control pairs |
| Point IDs collide after import | Two sources numbered from 1 | Choose **Renumber** in the duplicate policy, or **offset** at the DXF merge prompt |
| Labels overlap at some zoom levels | Labels are placed in plotted inches, so their spacing follows the plot scale | Set the plot scale you will actually plot at, then adjust the label offsets in Settings → Drafting |
| A point group stopped matching after editing descriptions | The group is keyed on **description**, not raw description | Key the rule on **Raw description matches** instead |
| Civil 3D points did not come through the DXF | They are custom objects and are not in the DXF | Export a PNEZD CSV from Civil 3D |

---

## Related

[[Point Groups and Surfaces]] · [[Coordinate Alignment]] · [[Traverse Editor]] · [[Import and Export]] · [[Inquiry Commands]]
