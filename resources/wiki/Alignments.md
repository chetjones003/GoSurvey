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
| `ALIGNMENT <name>[, <description>[, <station>[, <layer>[, <style>]]]]` | Creates an empty alignment (names are unique, any capitalisation). The starting station defaults to `0+00`. |
| `ALIGNMENTADD <name>, TANGENT, <x>, <y>, <azimuth°>, <length>` | The first tangent: a start point (world coordinates), a direction in decimal degrees clockwise from north, and a length. |
| `ALIGNMENTADD <name>, TANGENT, <length>` | Continues straight on from the end. |
| `ALIGNMENTADD <name>, TANGENTTO, <x>, <y>` | A tangent from the end to a world point. |
| `ALIGNMENTADD <name>, CURVE, <radius>, <delta°>, LEFT\|RIGHT` | A circular curve that continues smoothly (tangent-continuous) from the end. |
| `ALIGNMENTADD <name>, CURVELEN, <radius>, <arc length>, LEFT\|RIGHT` | The same, given by arc length. |
| `CREATEALIGNMENTFROMOBJECTS <name>[, START=<station>][, FILLET=<radius>][, REVERSE][, TOL=<gap>]` | Converts the **selected** lines, arcs and polylines. A line becomes a tangent; an arc or a polyline's bulge segment becomes a true curve. `FILLET` rounds corners between tangents (a corner that is too tight stays a corner and is reported). Gaps larger than `TOL` (default 0.001) are refused, naming the object. |
| `ALIGNMENTLIST [<name>]` | Reports the type, stations, length, start/end bearings, every tangent, curve and **clothoid spiral**, and any gap, kink or curvature mismatch at the alignment's continuity level. |
| `GETSTATIONOFFSET <alignment>, <x>, <y>` | Station, offset and bearing of a world point. Offsets are **positive to the right** facing the alignment's end. A point beyond an end reports the nearest end. |
| `GETPOINTFROMSTATIONOFFSET <alignment>, <station>, <offset>` | X, Y and bearing of a station/offset. |
| `ALIGNMENTDELETE <name>` | Removes the alignment (undoable). |
| `EDITALIGNMENT [<name>]` | Opens the **Alignment Layout** table: one row per tangent, curve or spiral with stations, length, radius, delta/deflection, bearing, direction, constraint and continuity (C0/C1/C2 at spiral junctions). Edit a cell and the whole alignment re-solves; a refused edit changes nothing and says why. |
| `ALIGNMENTEDIT <name>, <entity#>, BEARING\|LENGTH\|RADIUS\|STARTRADIUS\|DELTA\|DIRECTION\|CONSTRAINT, <value>` | The same edit from the command line. Spiral **RADIUS** / **STARTRADIUS** use **0** for a tangent (infinite) side. Bearing is degrees clockwise from north and can only be edited on a **Fixed** tangent. |
| `ALIGNMENTLOCK <name>, <entity#>, RADIUS\|LENGTH\|BEARING, ON\|OFF` | A locked value refuses any edit that would change it. |
| `ALIGNMENTINSERT <name>, <after entity#>, TANGENT, <length>` / `CURVE, <radius>, <delta°>, LEFT\|RIGHT` / `SPIRAL, <length>, <end radius>, LEFT\|RIGHT` (optional start radius) | Inserts a tangent, curve or clothoid spiral that continues smoothly; what follows moves with it. |
| `ALIGNMENTREMOVE <name>, <entity#>` | Removes an entity; a following floating entity re-attaches to the one before. A remaining gap is reported. |
| `REVERSEALIGNMENT <name>` | Reverses direction and stationing of a continuous alignment (twice gives back the original). Station equations are cleared. |
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
- **Grips.** Select an alignment: the start point moves the whole alignment; the end of a tangent sets its
  length (and bearing, if Fixed); the middle of a curve sets its radius. Esc cancels a drag.
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

- Geometry is built from typed commands, the layout table, grips or selected objects; there is no on-screen layout toolbar yet.
- No widening yet.
- Offsets are not built at a corner where the parent changes direction without a curve, and not when the parent has spirals.

See also [[Command Reference]], [[Object Snaps]] and [[Properties]].
