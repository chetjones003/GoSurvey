# TASK-197 — paper text is bounded by the box it occupies

## Origin

Raised as a follow-up by TASK-196 and asked for by the user (2026-09-04): *"now fix the paper text
glyph box."* TASK-196 left text as the one paper type still selected by an approximate rectangle,
noting the estimate `0.6 · height · length` was a guess shared with the click pick.

## Requirement authority

**REQ-039**, purpose clause and acceptance (1): paper objects support *"the **same** interaction
surface as model-space objects"*, and *"a window box (L→R) selects paper objects fully inside it and
a crossing box (R→L) selects any it touches, **for every paper object type**"*.

Paper text failed that on its own terms, not merely by being approximate: for MTEXT and Table it
reported a rectangle in the wrong PLACE entirely. Not a SPEC GAP.

## What was wrong

`PaperTextBoundsIn` carried its own copy of the text-bounds rule, and it differed from model space's
in three ways that each reached the user:

1. **It ignored the stored box.** An MTEXT and a Table carry `boxMinX/boxMaxX/boxMinY/boxMaxY` — the
   rectangle the user dragged, and the one the renderer wraps, anchors and clips to
   (`CadUi.cpp`'s paper MTEXT branch reads exactly those four fields). `PaperTextBoundsIn` ignored
   them and substituted a one-line estimate sitting at the insertion point. Since an MTEXT's
   insertion point is not inside its box, a sheet MTEXT was picked and box-selected **nowhere near
   where it is drawn**. Model space has returned the stored box since it had one
   (`CadAnnotationHasTextBox`); paper never did.
2. **It ignored `rotationRad`.** A rotated string's box stayed axis-aligned around where the string
   would have been unrotated. Model space rotates the four corners.
3. **It measured width with `text.size()` — a BYTE count.** Every non-ASCII character is 2–4 bytes,
   so any accented or CJK label got a box two to four times too wide. Model space had this defect
   too, and it is fixed in both by the shared helper.

Plus a fourth, found while unifying: model space floored the width at `2 · h`, so a one- or
two-character label reported an extent up to three times the glyphs it draws.

## The fix

One shared rule, `CadTextAnnotationBounds` in `src/commands/CadEntities.hpp` — the dependency-free
header both spaces already include. `PaperTextBoundsIn` and `CadAnnotationRoughBounds` now both call
it, so the two cannot drift again. Alongside it:

- `kCadTextAdvanceFactor` — one advance-width constant (0.55), replacing model's 0.55 and paper's
  0.6. A string of a given height now reports the same width on a sheet as in the model, which is
  the parity REQ-039 asks for and the one place it was measurably absent.
- `CadTextGlyphCount` — UTF-8 code points, counting lead bytes only.
- `CadTextEstimatedWidth` — the estimate, **with the `2 · h` floor removed**. That floor is a pick
  APERTURE, and both pick sites already apply one: `PickPaperEntityAt` expands by `tolIn` and
  `PickCadAnnotationAt` by `tol`. Kept in the bounds it was applied a second time, and applied to the
  box FENCE as well — where an overstated extent selects a label the box never touched, which is the
  defect TASK-196 spent its whole length removing from every other type.

The dimension branch of `CadAnnotationRoughBounds` routes its label quad through the same estimate,
so the label of a dimension is measured like any other string.

### What is still an estimate, and why

The width of a single-line TEXT. The true value comes from the resolved font's advances —
`Shx::MeasureWidthPx` for a stroke font, `ImFont::CalcTextSizeA` for a TrueType one — and neither is
reachable from a pure header in this layer: the SHX path reads font files off disk (and returns
nothing when that font is not installed), and the TTF path lives above this layer in the UI. A hit
test that answered differently depending on which Autodesk fonts happen to be installed on the
machine would be worse than one that is consistently approximate, so the estimate stays, is now
named rather than a literal, and is documented at its definition. Recorded as debt below rather than
decided here — moving font metrics into the command layer is an architectural change (CLAUDE.md §4),
not a bug fix.

MTEXT and Table are exempt from that limitation entirely: they carry a real box, and it is now used.

## Files

| file | change |
|---|---|
| `src/commands/CadEntities.hpp` | `kCadTextAdvanceFactor`, `CadTextGlyphCount`, `CadTextEstimatedWidth`, `CadTextAnnotationBounds`; `<algorithm>` include |
| `src/commands/PaperSpace.hpp` | `PaperTextBoundsIn` delegates to the shared rule |
| `src/commands/CadCommands.cpp` | `CadAnnotationRoughBounds` delegates its text and text-box branches; the dimension label quad uses the shared estimate |
| `tests/PaperSpaceTests.cpp` | new case with 6 sections + a box-select case; one existing assertion updated (below) |

Net effect on `CadCommands.cpp`: −24 lines, one duplicated corner-rotation loop gone.

## Test approach

`tests/PaperSpaceTests.cpp`:

- **"Paper text bounds: the box it occupies, not a guess at the insertion point"** — six sections:
  MTEXT returns its stored box; a Table does too; a box stored with reversed corners is normalized;
  rotation turns the box with the glyphs (a quarter turn maps `[0,2.2]×[-1,0]` to `[0,1]×[0,2.2]`);
  width counts characters not bytes, and the same three characters in ASCII and in UTF-8 measure
  identically; a one-character label reports the width it draws with no minimum padding.
- **"Paper box-select follows an MTEXT's real box"** — the bounds are what the fence consumes, so the
  fix has to be visible through `SelectPaperEntitiesInBox`: a crossing box over the text as drawn
  selects it, one over the insertion point (where the old estimate put the rectangle) selects
  nothing, and window mode needs the real box inside rather than the insertion point.

**Negative-tested, not asserted.** The old `PaperTextBoundsIn` body was restored behind the current
signature and the suite re-run: **8 assertions failed** across 3 cases (lines 262, 525, 540, 554,
573, 587, 608, 628). Restoring the fix returns them to green.

## One existing assertion changed, deliberately

The REQ-039 bug #2 anchor case asserted `x1 == 3 + 0.6` for `"AB"` at `h = 0.5` — paper's own 0.6
advance factor. It now asserts `3 + 0.55`, the single shared factor. The test's actual subject (the
insertion point is the TOP-left, so the glyphs descend below it) is unchanged and still asserted.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean.
- **testing** — PASS. `ctest`: **1117/1117** on `beta` @ `d158da7` with TASK-196.
- **architecture-review** — PASS. The rule moves DOWN, into the dependency-free entity header both
  spaces already include — no new dependency, no layer crossed, and the deliberate refusal to reach
  UP into the UI for font metrics is recorded above and as debt below.
- **code-review** — PASS. Two definitions of one rule became one; nothing added without two callers.
- **performance-review** — PASS by inspection. `CadTextGlyphCount` is one pass over a short string,
  replacing `size()`, in code that already ran per annotation per box selection.

## Not covered by test, stated plainly

Model-space callers of `CadAnnotationRoughBounds` are covered only by the existing suite staying
green (1117/1117), not by a new assertion aimed at them. The behaviour change there is real though
small: non-ASCII labels get a narrower, correct box, and one- or two-character labels lose the
`2 · h` floor. Both make the reported extent match the glyphs; neither was pinned by a test, which is
itself worth noting.

## Technical debt

- **Measured glyph widths.** The single-line TEXT width remains an estimate in both spaces. Doing
  better needs a decision about where font metrics live — thread a measurement callback down into the
  bounds helper, or cache a measured extent on `CadAnnotation` (a persisted struct, so a `.gs` and
  DXF question too). Architectural; belongs to the SPEC layer, and no accepted requirement currently
  asks for exact text bounds.
- **Paper dimensions are bounded as text.** A dimension annotation lives in `paperTexts` and falls
  through `PaperTextBoundsIn`'s plain-TEXT branch, so it is picked as a short string at its insertion
  point rather than by its geometry — model space routes dimensions to `CadDimAnyGeometry` instead.
  Related: nothing populates `boxMinX/boxMaxX` on a dimension, so the paper renderer's own dimension
  selection rectangle is degenerate at the origin. Out of scope here (this task is about text), and
  the two are one fix.
- **`PaperTextBoundsIn` ignores hard line breaks.** A paper TEXT containing `\n` reports one line of
  height. MTEXT is unaffected (it uses its box), and the single-line TEXT command does not produce
  them, so this is latent rather than live.
