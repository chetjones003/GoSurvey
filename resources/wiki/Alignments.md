# Alignments

An **alignment** is the horizontal path of a road, pipe, utility or baseline: straight pieces
(**tangents**) joined by circular **curves**, with a mile-marker system called **stationing** along it.
GoSurvey stores the exact tangent and curve numbers — never a many-sided polyline — so a station, an
offset and a curve's radius are always exact.

> **What alignments can do.** Tangents, circular curves and clothoid spirals; stationing with station equations;
> station/offset queries; creation from typed input or selected objects; editing with constraints, the Alignment
> Layout table and grips; move, copy, rotate, scale, mirror and reverse; offset alignments and intersections;
> continuity checks; styles, labels, label styles and tables; reports; ground elevations from a surface and feature
> lines; COGO commands; and design criteria with design-check warnings.

## Station format

`12+45.32` means 1 245.32 units from the zero station. Type stations the same way (`12+45.32`,
`STA 12+45.32` or just `1245.32`). The first two digits after the `+` are the part below 100.

## Commands

| Command | What it does |
|---|---|
| `CREATEALIGNMENT` (Home ▸ Create Design ▸ Alignment ▸ Alignment Creation Tools) | Opens **Create Alignment - Layout**: name, type, description and starting station; the style, layer and label set; an optional starting point and direction; and on the Design Criteria tab the starting design speed and a check set. OK creates the alignment and opens the **Alignment Layout Tools** window. |
| `ALIGNMENT <name>[, <description>[, <station>[, <layer>[, <style>]]]][, TYPE=][, LABELSET=][, CRITERIA=][, SPEED=]` | The same from the command line: creates an empty alignment (names are unique, any capitalisation). The starting station defaults to `0+00`. |
| `ALIGNMENTLAYOUT <name>[, CURVES\|NOCURVES][, FROM=<x> <y>][, DIRECTION=<azimuth°>]` | **Tangent-Tangent** layout, like Civil 3D's: click or type each PI (`X,Y` or `@dx,dy`). With curves, every corner gets a curve of the Curve and Spiral Settings radius (and spirals when set); a corner too tight for it stays a PI and is reported. `U` takes back a PI; Enter or Esc finishes and keeps what was built; one `UNDO` removes the session. An alignment with geometry is continued from its end. The Alignment Layout Tools window runs it: Tangent-Tangent (No curves / With curves), Curve and Spiral Settings, Delete Sub-entity, Alignment Grid View, Undo and Redo. |
| `ALIGNMENTCURVESETTINGS [RADIUS=<r>\|DEFAULT][, SPIRALIN=<length>\|OFF][, SPIRALOUT=<length>\|OFF]` | The Curve and Spiral Settings the layout uses: the curve radius (default 100 ft, or 30 m in a metric drawing) and clothoid spiral lengths before and after it. |
| `ALIGNMENTADD <name>, TANGENT, <x>, <y>, <azimuth°>, <length>` | The first tangent: a start point (world coordinates), a direction in decimal degrees clockwise from north, and a length. |
| `ALIGNMENTADD <name>, TANGENT, <length>` | Continues straight on from the end. |
| `ALIGNMENTADD <name>, TANGENTTO, <x>, <y>` | A tangent from the end to a world point. |
| `ALIGNMENTADD <name>, CURVE, <radius>, <delta°>, LEFT\|RIGHT` | A circular curve that continues smoothly (tangent-continuous) from the end. |
| `ALIGNMENTADD <name>, CURVELEN, <radius>, <arc length>, LEFT\|RIGHT` | The same, given by arc length. |
| `CREATEALIGNMENTFROMOBJECTS` (Home ▸ Create Design ▸ Alignment ▸ Create Alignment from Objects) | Pick the first line, arc, polyline or feature line (the alignment starts at the end nearest the pick), pick the rest, press Enter, then Enter to accept the direction or `R` to reverse it. The **Create Alignment from Objects** dialog then asks for the name, style, layer, label set, criteria and the conversion options: **Add curves between tangents** (with a default radius) and **Erase existing entities** (both on by default). |
| `CREATEALIGNMENTFROMOBJECTS <name>[, START=<station>][, FILLET=<radius>][, REVERSE][, TOL=<gap>][, ERASE][, PICK=<x> <y>]` | The same from the command line, on the **selected** objects. A line becomes a tangent; an arc or a polyline's bulge segment becomes a true curve. `FILLET` rounds corners between tangents; where that radius would use more than 40 % of the shorter tangent, the corner gets the radius that uses 40 % (as Civil 3D does) and the smaller radius is reported. Gaps up to `TOL` (default 0.001, at most 1.0) are joined; larger gaps, overlapping or branching objects, vertical lines, tilted arcs, 3D polylines and other objects (splines, circles, text) are refused by name and nothing is created. Elevations are ignored. With `ERASE` one `UNDO` removes the alignment and brings the objects back. |
| `ALIGNMENTLIST [<name>]` | Reports the type, stations, length, start/end bearings, every tangent, curve and **clothoid spiral**, and any gap, kink or curvature mismatch at the alignment's continuity level. |
| `GETSTATIONOFFSET <alignment>, <x>, <y>` | Station, offset and bearing of a world point. Offsets are **positive to the right** facing the alignment's end. A point beyond an end reports the nearest end. |
| `GETPOINTFROMSTATIONOFFSET <alignment>, <station>, <offset>` | X, Y and bearing of a station/offset. |
| `ALIGNMENTDELETE <name>` | Removes the alignment (undoable). |
| `EDITALIGNMENT [<name>]` | Opens the **Alignment Layout** table, docked below the drawing like Civil 3D's Panorama. It stays open while you work and follows undo, grips and edits. One row per tangent, curve or spiral: stations, length, radius, start radius and A (spirals), delta/deflection, bearing, direction, the **tangency constraint** (Fixed, Float, Free), the **parameter constraint** (how the entity is defined, such as "Radius, delta"), locks, continuity (C0/C1/C2), the **Criteria** the design check set requires (such as `R ≥ 120.00'`), and any failed check, which is marked with a warning icon. Only the values an entity is defined by can be edited; derived values are greyed. Edit a cell and the whole alignment re-solves; a refused edit changes nothing and says why. |
| `ALIGNMENTEDIT <name>, <entity#>, BEARING\|LENGTH\|RADIUS\|STARTRADIUS\|DELTA\|DIRECTION\|CONSTRAINT, <value>` | The same edit from the command line. Spiral **RADIUS** / **STARTRADIUS** use **0** for a tangent (infinite) side. Bearing is degrees clockwise from north and can only be edited on a **Fixed** tangent. |
| `ALIGNMENTLOCK <name>, <entity#>, RADIUS\|LENGTH\|BEARING\|STATION, ON\|OFF` | A locked value refuses any edit that would change it. **STATION** keeps the entity's start where it is along the alignment: an edit, insert or delete that would move it is refused, naming every locked entity that would move. Changing the starting station only relabels and is allowed. |
| `ALIGNMENTINSERT <name>, <after entity#>, TANGENT, <length>` / `CURVE, <radius>, <delta°>, LEFT\|RIGHT` / `SPIRAL, <length>, <end radius>, LEFT\|RIGHT` (optional start radius) | Inserts a tangent, curve or clothoid spiral that continues smoothly; what follows moves with it. |
| `ALIGNMENTREMOVE <name>, <entity#>` | Removes an entity; a following floating entity re-attaches to the one before. A remaining gap is reported. |
| `REVERSEALIGNMENT <name>` | Reverses direction and stationing of a continuous alignment (twice gives back the original). Station equations are cleared. Station labels, design speeds and linked offset alignments follow, so each stays where it was on the ground. |
| `ALIGNMENTSTATION <name>, START, <station>[, CLEAR]` / `EQ, ADD, ...` | Starting station and station equations. |
| `ALIGNMENTCONTINUITY <name>, C0\|C1\|C2` / `LIST` | Sets which continuity level is checked at junctions (default **C1**). **LIST** reports warnings at that level. |
| `OFFSETALIGNMENT <parent>, R12, L12` | Creates real **offset alignments** (right/left of the parent, facing its end). They follow the parent when it is edited or moved. Refused where the offset would collapse a curve, at a kink in the parent, or when the parent contains a **spiral**. |
| `ALIGNMENTDETACH <name>` | Makes a linked offset independent so it can be edited. |
| `ALIGNMENTINTERSECT <a>, <b>` / `<a>, LINE, x1, y1, x2, y2` / `<a>, CIRCLE, cx, cy, r` | Every intersection with station and crossing angle; coincident tangents are reported as a range. |
| `ALIGNMENTSTYLE LIST` / `NEW` / `DELETE` / `ASSIGN` / `SHOW\|HIDE` / `COLOR` | Drawing-wide **display styles** for tangent, curve and spiral components (color, visibility). Each alignment uses a style name (default **Standard**). |
| `ALIGNMENTLABELSET LIST` / `NEW` / `ASSIGN` / `MAJORINTERVAL` / `MINORINTERVAL` / `SHOW\|HIDE <set>, MAJOR\|MINOR\|EQUATIONS\|GEOMETRY\|SPEEDS` | Drawing-wide **label sets**. Each alignment uses a label set name (default **Standard**, with every item on). In the viewport, major stations get their station text and minor stations a tick. **GEOMETRY** labels the geometry points (`BP: 5+00.00`, `PC`, `Mid`, `PT`, `PI`, `TS`, `SC`, `EP` and so on) on a leader to the left of travel. **SPEEDS** labels each design speed (`STA 5+00.00 DESIGN SPEED = 40 mi/h`, km/h in a metric drawing) on a leader to the right. Labels are drawing text, so ZOOM EXTENTS takes them in. **STYLE** gives an item its label style. |
| `ALIGNMENTLABELSTYLE LIST` / `NEW` / `DELETE` / `SET <style>, HEIGHT\|COLOR\|TEXTSTYLE, <value>` | Drawing-wide **label styles**: the text style (font), plotted height and colour labels are written in. **Standard** is 0.1", the Standard text style, in the alignment layer's colour. See [[Commands/alignmentlabelstyle]]. |
| Design check marker | An alignment that fails its design check shows an amber warning triangle with "!" at the middle of each failing entity, as Civil 3D does. |

## Selecting, editing and saving

- Click an alignment, window-select it, or choose it in the Prospector tree (**Alignments**). The
  Properties panel shows its name, type, stations, length, start/end coordinates and bearings, and
  entity, curve and station-equation counts. `LIST` and `ERASE` work; `UNDO` restores it.
- Object snaps work on the exact geometry: **Endpoint** snaps to the beginning, end, PC, PT, PI and
  spiral junctions (TS, SC, CS, ST, SS), **Midpoint** to the middle of each tangent and curve,
  **Center** to a curve's centre, **Quadrant** on cardinal tangents and spirals (not on circular
  curve segments), **Nearest** to the closest point on the true curve, **Intersection** where an
  alignment meets another alignment or a line or arc, and **Station** when you type
  `STATION 12+45.32` at a point prompt (or enable Station OSNAP after a station value is latched).
- **Grips.** Select an alignment to see grips like Civil 3D's. Each drag goes through the same rules as the
  Alignment Layout table, so a locked value does not move, and Esc cancels a drag.
  - **Triangle at a PI** (a corner, or where the tangents meet over a curve): moves the PI. Both tangents turn to it;
    a curve between them keeps its radius and stays tangent. Dashed lines show each curve's tangents to its PI.
  - **Circles at a curve's start (PC) and end (PT):** slide along the tangent; the radius changes so the curve
    stays tangent. On a curve that follows the one before it, the end changes the curve's length.
  - **Circle at a curve's middle:** the curve passes through the point (its radius changes, its tangents stay).
  - **Circle at a curve's centre:** moves the centre along the line from the PI; the radius follows.
  - **Square at a tangent's middle:** moves the tangent sideways, keeping its bearing; corners slide along the
    tangents next to it and curves re-fit.
  - **Squares at the ends:** the start moves the whole alignment; the end of a tangent sets its length (and bearing,
    if Fixed). Spirals have grips at the end (length) and middle (end radius).
  - While you drag, the status bar shows the cursor's station, offset and bearing on the alignment as it was when
    you picked the grip.
- **MOVE, COPY, ROTATE, SCALE, MIRROR, ERASE** work on alignments (rotation and mirroring in plan). A copy is a new
  independent alignment named `<name> (2)`.
- **Constraints.** A new tangent or curve is *Float (prev)*: it stays tangent to what precedes it. *Fixed* keeps its
  own values. *Free* is a curve that re-fits between two tangents.
- Alignments are saved with the drawing as exact numbers. A saved DWG also contains ordinary
  AutoCAD lines and arcs (and short line segments approximating **spirals** for display) on the alignment's layer so other software can draw it.

## Design criteria

- A **check set** is a named list of limits kept in the drawing: a minimum curve radius for each design speed, a minimum tangent length, and the minimum and maximum spiral A and length. Make one with `ALIGNMENTCHECKSET NEW`, and enter its values with `ALIGNMENTCHECKSET SET`.
- Apply a set with `ALIGNMENTCRITERIA <alignment>, <set>`. Set the design speed from a station with `ALIGNMENTSPEED`. The speed that applies to a curve is the highest one set anywhere across it.
- `ALIGNMENTCHECK <alignment>` lists what fails. The same findings appear in `ALIGNMENTREPORT`. A failure is reported; it does not stop the alignment being saved or used.
- Road sets apply to centreline, offset and curb-return alignments. Rail sets apply only to rail alignments. Miscellaneous alignments take no criteria.
- GoSurvey does not ship a regional standard. The numbers are the ones you enter.
- Not yet built: superelevation and sight-distance checks, and a check of a compound spiral's length.

## Limits so far

- Geometry is built from typed commands, the Alignment Layout Tools (Tangent-Tangent), the layout table, grips or
  selected objects; Civil 3D's other sub-entity layout tools are not built.
- No widening yet.
- Offsets are not built at a corner where the parent changes direction without a curve, and not when the parent has spirals.

See also [[Command Reference]], [[Object Snaps]] and [[Properties]].
