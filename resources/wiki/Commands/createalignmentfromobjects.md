# CREATEALIGNMENTFROMOBJECTS

**Command:** `CREATEALIGNMENTFROMOBJECTS` — aliases: `CREATEALIGNMENTENTITIES`
**Category overview:** [[Alignments]]

Makes one alignment from connected lines, arcs, 2D polylines and feature lines (Home ▸ Create Design ▸ Alignment ▸
Create Alignment from Objects):

1. **Select the first line/arc/polyline.** The alignment starts at the end nearest where you click.
2. **Select the rest**, then press Enter. If objects were already selected, the command uses them.
3. GoSurvey reports where the alignment starts and which way it heads. Press Enter to accept, or `R` to reverse it.
4. The **Create Alignment from Objects** dialog asks for the name, type, description, starting station, style, layer,
   label set and design criteria, and the conversion options:
   - **Add curves between tangents** with a **Default radius** (100 ft, or 30 m in a metric drawing): each corner
     between two tangents is rounded. Where that radius would use more than 40 % of the shorter of the two
     tangents, the corner gets the radius that uses 40 %, as Civil 3D does, and the smaller radius is reported.
   - **Erase existing entities**: the objects are removed. One `UNDO` removes the alignment and brings them back.

A line becomes a tangent, an arc or a polyline's bulge segment becomes a true curve — never a chain of short lines.
Elevations are ignored (an alignment is plan geometry). Gaps up to the tolerance (0.001, or `TOL=` up to 1.0) are
joined. Larger gaps, objects that overlap or branch, vertical lines, tilted arcs, 3D polylines and anything that is
not linework (splines, circles, text) are refused, naming the first object, and nothing is created.

From the command line, on the selected objects:

`CREATEALIGNMENTFROMOBJECTS <name>[, START=<station>][, FILLET=<radius>][, REVERSE][, TOL=<gap>][, ERASE][, PICK=<x> <y>][, TYPE=][, DESCRIPTION=][, LAYER=][, STYLE=][, LABELSET=][, CRITERIA=][, SPEED=]`

---

## Related

- [[Alignments]]
- [[Command Reference]]
