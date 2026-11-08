# CREATESHEETS

**Command:** `CREATESHEETS`
**Category overview:** [[Alignments]]

Create paper sheets from view frames: CREATESHEETS <alignment>, <template layout>[, <frames>]

Typed without arguments, the command opens the Create Sheets dialog. The template is a landscape paper layout in this drawing, with the same sheet size as the frame group (for example ARCH D, 36 in x 24 in). `<frames>` is `ALL` (the default) or frame names separated by spaces, such as `VF-1 VF-3`.

Each chosen frame becomes one paper layout named after it (`VF-1`, `VF-2`, ...). The layout copies the template's paper content and has one viewport. The viewport is centred on the frame, at the frame's scale, and turned so the alignment runs left to right across the sheet. All the sheets from one command are one undo step.

See [[Alignments]] for the refusals, the name suffix for a name already in use, and what is not built yet.

---

## Related

- [[Alignments]]
- [[Command Reference]]
