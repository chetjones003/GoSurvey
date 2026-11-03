# Alignments

An **alignment** is the horizontal path of a road, pipe, utility or baseline: straight pieces
(**tangents**) joined by circular **curves**, with a mile-marker system called **stationing** along it.
GoSurvey stores the exact tangent and curve numbers — never a many-sided polyline — so a station, an
offset and a curve's radius are always exact.

> **Status (Phases 1 and 2 of 6).** Tangents, circular curves, stationing with station equations,
> station/offset queries, creation from typed input or selected objects, display, selection,
> properties, object snaps, undo and save/reopen (Phase 1); editing with constraints, the Alignment
> Layout table, grips, move/copy/rotate/scale/mirror, reverse, offset alignments and intersections
> (Phase 2). **Clothoid spirals** and C2 continuity checks arrive in Phase 3 (issue #121). Labels, tables, reports, surface and feature-line links and design checks arrive
> in later phases. See `spec/requirements.md` REQ-480 to REQ-503.

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

## Selecting, editing and saving

- Click an alignment, window-select it, or choose it in the Prospector tree (**Alignments**). The
  Properties panel shows its name, type, stations, length, start/end coordinates and bearings, and
  entity, curve and station-equation counts. `LIST` and `ERASE` work; `UNDO` restores it.
- Object snaps work on the exact geometry: **Endpoint** snaps to the beginning, end, PC, PT, PI and
  spiral junctions (TS, SC, CS, ST, SS), **Midpoint** to the middle of each tangent and curve,
  **Center** to a curve's centre, **Quadrant** on cardinal tangents and spirals (not on circular
  curve segments), **Nearest** to the closest point on the true curve, and **Station** when you type
  `STATION 12+45.32` at a point prompt (or enable Station OSNAP after a station value is latched).
- **Grips.** Select an alignment: the start point moves the whole alignment; the end of a tangent sets its
  length (and bearing, if Fixed); the middle of a curve sets its radius. Esc cancels a drag.
- **MOVE, COPY, ROTATE, SCALE, MIRROR, ERASE** work on alignments (rotation and mirroring in plan). A copy is a new
  independent alignment named `<name> (2)`.
- **Constraints.** A new tangent or curve is *Float (prev)*: it stays tangent to what precedes it. *Fixed* keeps its
  own values. *Free* is a curve that re-fits between two tangents.
- Alignments are saved with the drawing as exact numbers. A saved DWG also contains ordinary
  AutoCAD lines and arcs (and short line segments approximating **spirals** for display) on the alignment's layer so other software can draw it.

## Limits so far

- Geometry is built from typed commands, the layout table, grips or selected objects; there is no on-screen layout toolbar yet.
- No station labels, tables, reports, widening, profiles or surface elevations yet.
- Offsets are not built at a corner where the parent changes direction without a curve, and not when the parent has spirals.

See also [[Command Reference]], [[Object Snaps]] and [[Properties]].
