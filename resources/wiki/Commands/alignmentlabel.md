# ALIGNMENTLABEL

**Command:** `ALIGNMENTLABEL`
**Category overview:** [[Alignments]]

Label an alignment's segments (needed for tables): ALIGNMENTLABEL <name>, SEGMENTS | CLEAR

Labels the segments of an alignment. Line, curve and segment tables need the segments labelled first: until then ALIGNMENTTABLE refuses and names this command. CLEAR removes the labels; existing dynamic tables then stop updating and say so.

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
