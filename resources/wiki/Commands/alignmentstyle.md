# ALIGNMENTSTYLE

**Command:** `ALIGNMENTSTYLE` — no alias  
**Category overview:** [[Alignments]]

Manage **alignment display styles** (REQ-491): tangent, curve and spiral components with color,
linetype, lineweight and visibility. Each alignment references a style by name (default **Standard**).

```
ALIGNMENTSTYLE LIST
ALIGNMENTSTYLE NEW <style>
ALIGNMENTSTYLE DELETE <style>
ALIGNMENTSTYLE ASSIGN <alignment>, <style>
ALIGNMENTSTYLE SHOW|HIDE <style>, <tangent|curve|spiral>
ALIGNMENTSTYLE COLOR <style>, <tangent|curve|spiral>, <color>
```

**Standard** cannot be deleted. A style in use by any alignment cannot be deleted until those
alignments use another style. Missing style names on load resolve to **Standard** and are logged.

Styles are saved with the drawing. The viewport draws each alignment entity using its style's
component settings (same pattern as surface styles).

See [[Alignments]] for creation and editing commands.

---

## Related

- [[Alignments]]
- [[Command Reference]]

## Appearance items (SET)

`ALIGNMENTSTYLE SET, <style>, TICKS|MARKERS|ARROWS|LABELS, ON|OFF` turns an item on or off.
`ALIGNMENTSTYLE SET, <style>, TICKSIDE, BOTH|LEFT|RIGHT` picks the side the station ticks sit on.
`ALIGNMENTSTYLE SET, <style>, MAJORTICK|MINORTICK|MARKERSIZE|ARROWLENGTH, <n>` sets a length or size in drawing units.

Station ticks cross the alignment at the stations of its label set. Geometry markers are small crosses at the TS, SC, PC,
PT and PI points. A direction arrow sits at the middle of the alignment and is off by default. LABELS hides the station,
equation and station/offset labels while leaving the lines and ticks in place. A style that an alignment uses cannot be
deleted; if a saved alignment names a style that is missing, it uses Standard and the load log says so.

## Style editor

`EDITALIGNMENTSTYLE [<style>]` opens the Alignment Style dialog. It has five tabs: Information (the name and the alignments
that use the style), Design (a placeholder until design criteria arrive, REQ-502), Markers (geometry markers and station
ticks), Display (tangent, curve and spiral visibility and colour, the direction arrow and label visibility) and Summary.
Each control runs the same command as typing it, so undo and refusals match. New and Delete work the same way as
`ALIGNMENTSTYLE NEW` and `DELETE`.
