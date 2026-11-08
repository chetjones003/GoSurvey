# CREATEVIEWFRAMES

**Command:** `CREATEVIEWFRAMES`
**Category overview:** [[Alignments]]

Create view frames along an alignment: CREATEVIEWFRAMES <alignment>, <start station>, <end station>, <sheet>, <scale or AUTO>, <overlap>[, CENTRED|LEFT|RIGHT]

Type `AUTO` for the scale to let the command pick the best fit: the most detailed named scale on which the whole alignment fits one sheet's usable area (the sheet less the title block and margins, 29.75 x 19.5 in on ARCH D). If no scale fits the alignment on one sheet, the coarsest scale is used. The log says the scale was chosen automatically.

Typed without arguments, the command opens the Create View Frames dialog. The sheet is a Page Setup paper size (for example ARCH D). The scale is one of the drawing's named scales (for example `1" = 50'`). The overlap is in drawing units. The placement defaults to CENTRED.

See [[Alignments]] for how frames follow the alignment, what each frame stores, and what is not built yet.

---

## Related

- [[Alignments]]
- [[Command Reference]]
