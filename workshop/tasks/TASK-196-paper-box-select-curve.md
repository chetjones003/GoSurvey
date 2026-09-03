# TASK-196 — paper-space box selection tests the object, not a box drawn round it

## Origin

Follow-up audit asked for by the user against TASK-195 (model-space fence, PR #257): *"check the
paper space box selection for the same bug."* It has it — in **every** type, and worse. Then:
*"go ahead and fix paper space now."*

## Requirement authority

**REQ-039** acceptance (1), quoted exactly:

> in a paper layout — (1) a window box (L→R) selects paper objects **fully inside it** and a
> crossing box (R→L) selects **any it touches**, for every paper object type

That is the whole specification for this task, and the implementation did not meet it for six of the
seven types. REQ-039's purpose clause asks for parity with model space ("the user edits a sheet's
native geometry with the **exact** UX they use in model space"), and model space has tested lines by
their segment and polylines per-segment since before REQ-039 existed — so this is a shortfall
against an accepted requirement, not a new one. Not a SPEC GAP.

Related: **REQ-316 / ADR-047** established "test the curve it draws" for polyline bulges, and
**TASK-195** applied it to model-space circles, arcs and ellipses. This is the paper-space half.

## What was wrong

`SelectPaperEntitiesInBox` (`src/commands/PaperSpace.hpp`) tested **every** paper type by an
axis-aligned bounding box — which is REQ-039's rule only for an object that IS a filled rectangle,
i.e. text alone. Seven probes, all seven wrong:

| type | what it tested | probe (crossing box) | got |
|---|---|---|---|
| Line | bbox of the endpoints | box `[8,1]-[9,2]`, ~5 in clear of the line `(0,0)-(10,10)` | selects |
| Circle | enclosing square `cx ± r` | box `[7.5,7.5]-[8.5,8.5]`, 10.6 from an r=10 circle | selects |
| Circle | same, and solid | box `[-1,-1]-[1,1]` in the hollow middle | selects |
| Arc | square of the **whole circle**, sweep ignored | 90° arc in the upper-right quadrant, box in the lower-**left** | selects |
| Ellipse | square of side 2·major, **`ratio` ignored** | semi-minor 1, box at `y ∈ [5,6]` | selects |
| Polyline | bbox of all vertices | L-shape `(0,0)→(10,0)→(10,10)`, box in the empty upper-left | selects |
| Block | the insertion **point** only | box over the block's geometry, not its insertion point | **misses** |

Two things made it worse than TASK-195's model-space case:

- **Lines and polylines were affected.** Model space never had this — `SegIntersectsAABB` and
  `ChainHitsRect` were always exact there. On a sheet, a diagonal line was selected from anywhere in
  its bounding square, and an L-shaped polyline from the empty quadrant it does not occupy.
- **Arc and ellipse ignored their own shape outright.** Not an approximation of the curve — the arc
  used the full circle's square whatever its sweep, and the ellipse ignored `ratio` entirely.

The **window** direction was wrong too, in the opposite sense: an arc's window test wanted the whole
CIRCLE's square inside the box, so a window box drawn snugly round a quarter arc selected nothing.
Same for a flat ellipse.

Blocks are the mirror-image defect: too small, not too big.

Not wrong, and left alone: the click path (`PickPaperEntityAt`) is already curve-correct, and
viewports are already handled correctly inside `closePaperSelBox` (the hollow-rectangle rule, issue
\#4). The box path was the outlier.

## The fix

`src/commands/PaperSpace.hpp` — each type tested against what it actually draws:

| type | now |
|---|---|
| Line | `PointInsideClosedRect` on both ends (window) / `SegIntersectsAABB` (crossing) |
| Text | unchanged — a bounding box IS its footprint |
| Circle | `CircleFullyInsideRect` / `CircleIntersectsAABB` — the exact analytic tests model space uses in plan view. A circle on a sheet is always flat, so nothing forces a tessellation |
| Arc | sampled over its own sweep, then `PaperChainHitsBox` |
| Ellipse | sampled with `ratio` honoured, then `PaperChainHitsBox` |
| Polyline | its own vertices, per segment, honouring `paperPolyClosed` |
| Block | `CadBlockWorldAabb` — the same call model-space box-select already makes |

Three new header-only helpers: `PaperChainHitsBox` (window = every point inside, crossing = any
segment touching), `PaperCurveSegments` (π/24 per step — `ChainHitsRect`'s own choice, so the paper
and model fences cannot disagree about where a curve goes), and `SamplePaperArc` /
`SamplePaperEllipse`. A sheet is 2D (ADR-025 (g)), so these need none of the plane-frame handling
REQ-312 gives their model-space counterparts.

**Signature change.** `SelectPaperEntitiesInBox` takes a trailing
`const std::vector<CadBlockDefinition>* blockDefs = nullptr`, mirroring `PickPaperEntityAt`'s own
optional-defs shape. Null keeps the historical insertion-point test, so existing callers and tests
compile and mean the same thing; the two real call sites in `CadUi.cpp` pass `&cmd.blockDefs`.

`PaperSpace.hpp` now includes `util/geom2d.hpp`. That is a declaration-only include; `geom2d.cpp` is
already in both the main library and the `GoSurveyTests` source list, so no CMake change and no new
link dependency for any target. Reusing those tests rather than re-deriving them in the header is
deliberate — two implementations of "does this segment hit this box" that agree only approximately
is the defect class this whole task is about.

## Files

| file | change |
|---|---|
| `src/commands/PaperSpace.hpp` | `PaperChainHitsBox`, `PaperCurveSegments`, `SamplePaperArc`, `SamplePaperEllipse`; all six broken types rewritten; optional `blockDefs`; `geom2d.hpp` include |
| `src/ui/CadUi.cpp` | both `SelectPaperEntitiesInBox` call sites pass `&cmd.blockDefs` |
| `tests/PaperSpaceTests.cpp` | new `[paperspace]` case, 7 sections; one existing assertion corrected (below) |

## Test approach

`tests/PaperSpaceTests.cpp` — "Paper box-select tests the object, not a box round it (REQ-039,
TASK-196)". Seven sections, one per defect, and **every MISS is paired with a HIT on the same
entity** so a box test that selected nothing could not pass. The closed-polyline section pairs the
other way round: the hypotenuse hit is paired with the same chain marked OPEN, which must NOT be
selected there — that pairing is what proves the closing leg is what hit, since the bounding box
would have hit either way.

**Negative-tested properly, not asserted.** The pre-fix bounding-box bodies were restored behind the
post-fix signature and the suite re-run: **8 assertions failed**, one per defect (Catch2 stops a
section at its first failure, so that is one per section plus the amended existing case). Restoring
the fix returns them all to green. Lines: 320, 359, 379, 397, 420, 442, 464, 485.

## One existing assertion changed, deliberately

`tests/PaperSpaceTests.cpp` (the REQ-039 bug #1 case) read:

> `// The same box as a crossing selection (R→L) overlaps only the circle → exactly the circle.`
> `SelectPaperEntitiesInBox(L, 9.f, 9.f, 11.f, 11.f, false, out); REQUIRE(out.size() == 1);`

The circle is at `(10,10)` with `r = 2`; the box's farthest corner is `sqrt(2) = 1.41` from the
centre, so the box lies **entirely inside** the circle and touches no drawn geometry. It selected
only because the test was the circle's solid bounding square — the defect, written down as the
expected behaviour, exactly like TASK-195's STRETCH transcript. It now requires `out.empty()`, and a
new box at `[11,11]-[13,13]` — genuinely straddling the rim — carries the original intent. No
assertion was weakened and nothing was skipped.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean.
- **testing** — PASS. `ctest`: **1115/1115** on `beta` @ `d158da7`. New cases fail before, pass after.
- **architecture-review** — PASS. No layer moved. The helper stays in the paper header beside the
  function it serves, and reuses `util/geom2d` rather than re-deriving box tests — the same shared
  module the model-space fence uses.
- **code-review** — PASS. The one signature change is defaulted, so no caller is silently altered;
  the block fallback preserves the old behaviour when definitions are unavailable.
- **performance-review** — PASS by inspection. Runs once per box selection, not per frame; ≤96
  sampled points per curve, and one `CadBlockCollectWorldLines` per block reference — the same call
  the model-space fence already makes per block.

## Not covered by test, stated plainly

The two `CadUi.cpp` call-site edits (passing `&cmd.blockDefs`) are UI wiring with no headless route —
paper box selection lives in the viewport's own mouse handler, which no transcript verb reaches. The
selection logic they call is fully covered above. GUI verification only.

## Follow-ups (not fixed here)

- **Paper text** still selects by an approximate glyph box (`PaperTextBoundsIn` estimates width as
  `0.6 · height · length`). Correct in kind — text is a filled rectangle — but the rectangle is a
  guess, and it is shared with the click pick, so both are off by the same amount.
- **Blocks in both spaces** are tested by their world AABB. For a block whose content is a small
  circle in a large empty extent this is the same shape of problem the rest of this task fixed; no
  accepted requirement currently says which answer is wanted. Same note as TASK-195's.
