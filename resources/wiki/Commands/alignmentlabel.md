# ALIGNMENTLABEL

**Command:** `ALIGNMENTLABEL`
**Category overview:** [[Alignments]]

Label an alignment's segments (needed for tables): ALIGNMENTLABEL <name>, SEGMENTS | SEGMENT, <entity#> | CLEAR

`SEGMENTS` labels every segment of an alignment (Civil 3D's Multiple Segment). `SEGMENT, <entity#>` labels one segment
(Single Segment); the label stays on that segment when entities before it are inserted or removed, and follows it when
the alignment is reversed. Line, curve, spiral and segment tables list the labelled segments: with none labelled,
ALIGNMENTTABLE refuses and names this command. CLEAR removes every segment label; existing dynamic tables then stop
updating and say so.

Once a table lists the segments, each segment label shows just its tag (`L1`, `C1`, `S1`, …), as Civil 3D converts
labels to tag labels when a table is made.

Labelled segments show their values at their middle. A tangent shows its bearing on one side and its length on the
other. A curve stacks `R`, `Δ`, `L` and `T` on the inside of the turn. A spiral stacks `A`, `Ls` and its start and end
radius (`∞` for a tangent end). Bearings, angles and lengths follow the drawing's unit and precision settings.

---

## Related

- [[Alignments]]
- [[Command Reference]]

## Station/offset labels

`ALIGNMENTLABEL <name>, STATIONLABEL, FIXED, <x>, <y>` puts a label on a point. The point stays where it is. If the
alignment moves, the label reads the station and offset that point now has on it.

`ALIGNMENTLABEL <name>, STATIONLABEL, RELATIVE, <station>, <offset>` puts a label at a station and offset. It moves with
the alignment and keeps its station and offset.

Labels belong to the alignment: erasing the alignment removes them. Equation labels and the MAJOR/MINOR rules are set with
`ALIGNMENTLABELSET`. Minor stations need the major stations, so hiding MAJOR while MINOR shows is refused.

`ALIGNMENTLABEL <name>, STATIONLABEL, LIST` shows each station label with its number.
`ALIGNMENTLABEL <name>, STATIONLABEL, MOVE, <number>, <x>, <y>` drags a label to a new point. A fixed label takes the point
as it is; a relative label takes the station and offset of the nearest point on the alignment. Undo restores it.
