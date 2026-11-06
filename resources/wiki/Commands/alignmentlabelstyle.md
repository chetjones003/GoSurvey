# ALIGNMENTLABELSTYLE

**Command:** `ALIGNMENTLABELSTYLE`
**Category overview:** [[Alignments]]

Alignment label styles (text style, height, colour, precision, prefix/suffix, orientation): ALIGNMENTLABELSTYLE LIST|NEW|DELETE|SET, ...

An alignment label style says how one kind of alignment label is written: its **text style** (the font), its
**height** on paper, and its **colour**. Every drawing has a built-in **Standard** style: the Standard text style,
0.1" high, ByLayer (the alignment layer's colour).

| Form | What it does |
| --- | --- |
| `ALIGNMENTLABELSTYLE LIST` | Lists the styles with their height, colour, text style and how many label sets use each. |
| `ALIGNMENTLABELSTYLE NEW, <style>` | Creates a style, copied from Standard. |
| `ALIGNMENTLABELSTYLE SET, <style>, HEIGHT, <inches>` | Plotted text height, for example `0.12`. Drawing units = this × the plot scale. |
| `ALIGNMENTLABELSTYLE SET, <style>, COLOR, <colour>` | `ByLayer`, an index `1`–`255`, a name such as `red`, or `#RRGGBB`. |
| `ALIGNMENTLABELSTYLE SET, <style>, TEXTSTYLE, <text style>` | A text style made with `STYLE`; its font is used. |
| `ALIGNMENTLABELSTYLE SET, <style>, PRECISION, <0-8>\|DRAWING` | Decimals of the lengths, radii and stations the label writes. DRAWING (the default) uses the drawing's precision and station format. |
| `ALIGNMENTLABELSTYLE SET, <style>, PREFIX\|SUFFIX, <text>\|NONE` | Text written before or after each line of the label. Spaces at the ends of the typed text are trimmed. |
| `ALIGNMENTLABELSTYLE SET, <style>, ORIENTATION, OBJECT\|VIEW` | Civil 3D's orientation reference: OBJECT (the default) writes text along the alignment; VIEW keeps it level on the screen. |
| `ALIGNMENTLABELSTYLE DELETE, <style>` | Deletes a style. Standard, and a style a label set still uses, cannot be deleted. |

Assign a style to the labels of a label set with
`ALIGNMENTLABELSET STYLE, <set>, MAJOR|GEOMETRY|SPEEDS|EQUATIONS|SEGMENTS|OFFSETS, <style>`. SEGMENTS covers the
segment labels made with `ALIGNMENTLABEL <alignment>, SEGMENTS`; OFFSETS covers station/offset labels.

Text in an SHX text style (such as `romans.shx`) is drawn with the stroke font. Characters the stroke font does not
have, such as `°`, `Δ` and `∞`, are drawn in a TrueType font of the same height.

Labels that would overlap another label step to the other side of the alignment or one row further out. Geometry-point
and design-speed labels stay on their leaders.

Styles and assignments are saved with the drawing. If a label set names a style that is missing when the drawing
opens, Standard is used and the command line says so. Every change can be undone.

---

## Related

- [[Commands/alignmentlabel]]
- [[Alignments]]
- [[Command Reference]]
