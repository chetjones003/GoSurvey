# CREATESHEETS

**Command:** `CREATESHEETS`
**Category overview:** [[Alignments]]

Create paper sheets from view frames: CREATESHEETS <alignment>[, <frames>]

Typed without arguments, the command opens the Create Sheets dialog. `<frames>` is `ALL` (the default) or frame names separated by spaces, such as `VF-1 VF-3`.

Each chosen frame becomes one paper layout named after it (`VF-1`, `VF-2`, ...), built on the Generic 24in x 36in Title Block.dwg from the AutoCAD Template folder. The sheet's title block is filled from the frame (alignment, sheet, scale, size, date and station range), with a north arrow that turns with the view. The sheet has one locked viewport inside a 0.5 in margin. Because the viewport is smaller than the sheet, the frames are resized to the usable area (29.75 x 19.5 in) at the same scale when the sheets are made, so each sheet covers its whole frame; the frame count can change. The viewport is centred on the frame, at the frame's scale, and turned so the alignment runs left to right across the sheet. All the sheets from one command are one undo step.

See [[Alignments]] for the refusals, the name suffix for a name already in use, and what is not built yet.

---

## Related

- [[Alignments]]
- [[Command Reference]]
