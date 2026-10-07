# TASK — Convert pasted content across coordinate systems and units (issue #722)

- Branch: `feat/paste-convert`
- Authority: **REQ-383** clauses 3 and 7 (revised 2026-10-07); **D-2026-10-07-a** (the user chose option A on both
  questions); GitHub #722. Survey points through Copy/Paste are split out to #782.
- Boundary check: the convert is domain code, with no window: `ConvertDrawing.{hpp,cpp}` gains a clipboard overload
  next to the whole-drawing one and both share the per-kind move helpers (two concrete uses); the rules that decide
  whether a paste may be converted are in `ProjectWarnings.{hpp,cpp}`; the dialog (`CadUi_Projects.cpp`) only adds a
  **Convert and paste** button. No new dependency.

## Delivered

1. `CadClipboard` remembers the source drawing's document origin (`srcOriginX/Y`), set by `TagClipboardOrigin`.
2. `ConvertDrawing.cpp`: the per-kind moves (lines, circles, arcs, ellipses, polylines, fills, annotations, tables,
   block references) are now shared helpers used by `ApplyDrawingConversion` (unchanged behaviour) and the new
   `ApplyClipboardConversion`. `ClipboardWorldExtents` and `UnconvertibleKinds(CadClipboard)` complete the set.
3. `ConvertClipboardForPaste` plans with the same `geo::PlanConversion` as Add Drawing (extents of the copy), refuses
   above 0.02 m, a tilted arc/ellipse across a coordinate-system change, and a paper-space copy; on success it moves
   the copy to the same ground position expressed in the destination's own origin.
4. `PasteCheck::canConvert`; a coordinate-system block now says whether it can be converted and why not; a units
   warning offers the same convert. The dialog shows **Convert and paste** when it can.
5. **Regression restored.** The merge of PR #680 (`241e241d`) dropped the P9 paste wiring from `CadCommands.cpp`
   (`TagClipboardOrigin` in both copy functions and the `PasteWaitsForUser` guard in `PASTE` / `PASTEORIG`), so on
   `beta` no paste warning ever appeared. Restored here because convert is unreachable without it.

## Assumptions

- ASSUMPTION: Convert is also offered for a units-only mismatch (it is the same transform; D-2026-10-07-a).
- ASSUMPTION: after Convert the cross-project note is not shown again (the user has just answered the dialog).
- ASSUMPTION: a clipboard of polylines with arcs is treated as flat (+Z), as Paste creates it.

## Technical debt

- An unconverted paste still ignores the two drawings' document origins (existing behaviour); only a converted paste
  is origin-exact.
- The same merge (#680) also removed the PROJECTSETTINGS / PROJECTHEALTH / PACKPROJECT / TURNOVER / OPENPACK /
  ADDDRAWING / PDFVIEW / PDFSPLIT command handlers from `CadCommands.cpp`; reported separately, not part of #722.

## Tests

`GoSurveySnapTests "[issue722]"` (`tests/ProjectWarningsTests.cpp`): zone convert lands within the CS-MAP answer and
keeps shapes; units convert scales lengths, heights, radii and origin; tilted arc refuses across zones but not for units;
oversize copy refused and untouched; cancel changes nothing; paper copy not convertible; Copy records the origin.
