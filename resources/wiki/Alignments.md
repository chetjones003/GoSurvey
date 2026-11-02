# Alignments

An **alignment** is the horizontal path of a road, pipe, utility or baseline: straight pieces
(**tangents**) joined by circular **curves**, with a mile-marker system called **stationing** along it.
GoSurvey stores the exact tangent and curve numbers — never a many-sided polyline — so a station, an
offset and a curve's radius are always exact.

> **Status (Phase 1 of 6).** Tangents, circular curves, stationing with station equations,
> station/offset queries, creation from typed input or selected objects, display, selection,
> properties, object snaps, undo and save/reopen are available. Spirals, the layout editor,
> offsets, labels, tables, reports, surface and feature-line links and design checks arrive in later
> phases. See `spec/requirements.md` REQ-480 to REQ-503.

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
| `ALIGNMENTLIST [<name>]` | Reports the type, stations, length, start/end bearings, every tangent and curve (radius, delta, length, tangent, chord) and any gap or kink. |
| `GETSTATIONOFFSET <alignment>, <x>, <y>` | Station, offset and bearing of a world point. Offsets are **positive to the right** facing the alignment's end. A point beyond an end reports the nearest end. |
| `GETPOINTFROMSTATIONOFFSET <alignment>, <station>, <offset>` | X, Y and bearing of a station/offset. |
| `ALIGNMENTDELETE <name>` | Removes the alignment (undoable). |

## Selecting, editing and saving

- Click an alignment, window-select it, or choose it in the Prospector tree (**Alignments**). The
  Properties panel shows its name, type, stations, length, start/end coordinates and bearings, and
  entity, curve and station-equation counts. `LIST` and `ERASE` work; `UNDO` restores it.
- Object snaps work on the exact geometry: **Endpoint** snaps to the beginning, end, PC, PT and PI
  (the tangent intersection), **Midpoint** to the middle of each tangent and curve, **Center** to a
  curve's centre, and **Nearest** to the closest point on the true curve.
- Alignments are saved with the drawing as exact numbers. A saved DWG also contains ordinary
  AutoCAD lines and arcs on the alignment's layer so other software can draw it.

## Limits in Phase 1

- Geometry is built from typed commands or selected objects; there is no on-screen layout toolbar yet.
- No spirals, station labels, tables, reports, offset alignments, profiles or surface elevations yet.
- Moving, copying, rotating or scaling an alignment is not available yet (erase and re-create).

See also [[Command Reference]], [[Object Snaps]] and [[Properties]].
