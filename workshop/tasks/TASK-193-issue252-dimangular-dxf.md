# TASK-193 — DIMANGULAR exports its geometry to DXF (issue #252)

## Requirement authority

**This is the honest position, and it is not clean.** There is **no accepted REQ covering dimensions
at all** — not their commands, not their display, not their export. Grepping `spec/requirements.md`
for a dimension requirement returns only **REQ-111** (*Associative DIMENSION entity*), which is
`proposed`, not accepted, and describes a future object this task does not build.

So this task cannot cite an accepted requirement that says "an angular dimension exports its
geometry", because none exists for any dimension kind.

What justifies it anyway:

- The correct behaviour is **not in doubt**. GoSurvey already draws an angular dimension in the
  viewport and already plots it to PDF; DXF alone dropped it. This restores parity between three
  output paths that are meant to show one thing, rather than deciding anything new.
- **REQ-201** (*No silent failures*) is the nearest accepted authority in spirit — geometry was lost
  on export with nothing logged — though it is written about failure paths rather than missing
  branches, so it is cited as adjacent rather than as a clean fit.
- **REQ-057** (a dimension's strokes sit on the dimension's own plane) is preserved unchanged.

**Raised for the SPEC layer rather than resolved here:** the absence of a dimensions requirement is a
real gap, and it is what let this defect sit unnoticed — with no acceptance list naming the three
output paths, nothing was going to catch one of them silently dropping a kind. Recorded in the PR for
the repo owner; deliberately not invented here, per CLAUDE.md §4 (Workshop does not author SPEC).

- GitHub issue **#252**. Filed from a parked finding, re-verified against `beta` @ `c40d2cf` first.

## What was wrong

`src/io/DxfIo.cpp` rebuilt dimension geometry by hand, in two places, behind this test:

```cpp
an.kind == CadAnnotation::Kind::DimAligned || an.kind == CadAnnotation::Kind::DimLinear
```

An angular dimension matched neither and fell through to the plain-annotation path, exporting as a
floating label with no arc, no extension rays and no vertex. Measured, one of each kind exported
together:

| entity | before | after |
|---|---|---|
| `LINE` | 6 | **68** |
| `TEXT` | 2 | **3** |
| `MTEXT` | 1 | **0** |

Six lines is three apiece for the linear and aligned dimensions; the angular contributed the `MTEXT`
alone. A recipient opening that file saw `90°0'0.0"` in space with nothing to say which angle it
measured, and no geometry from which to recover it.

## What it does now

Both sites call **`CadDimBuildWorldStrokes`** — the same module `CadUi.cpp` (viewport) and
`PdfPlot.cpp` (plot) already use. Adding a fourth hand-rolled branch would have fixed the symptom and
kept the shape of the bug: three consumers of one concept, two sharing a module and the third
reimplementing a subset of it.

The duplicated geometry is gone — `CadDimAnyGeometry` now has **zero** references in `DxfIo.cpp` —
and any dimension kind added later exports for free rather than silently not.

## Design notes

**Arrowheads are emitted as outlines, three `LINE`s per head.** Not decoration: the stroke module
insets the dimension line by the arrow length at each end, because a head is meant to occupy that
space. Emitting the strokes without the heads would have left a visible gap at both ends where the
old hand-rolled path drew a full-length line — a regression traded for a fix. A filled head would
want a DXF `SOLID`, which this writer emits nowhere else; an outline reads correctly at every scale
and keeps the export to the two entity kinds it already produces.

**Linear and aligned output changes, deliberately.** The extension lines are byte-identical — the
stroke module computes `gap` and `over` with the same formulas the hand-rolled code did, which is
worth knowing because it means the common case is provably unchanged. What differs is the dimension
line (now inset) and the arrowheads (now present). Both bring DXF into agreement with what the
viewport draws, which is the point.

**The entity-handle count and the emit must agree**, or every later handle in the file is wrong.
They now call one shared lambda, `dxfDimEntityCount`, rather than each carrying its own arithmetic —
the old code hard-coded `+= 4  // three LINE + one TEXT`, which was only correct while the geometry
was fixed-size.

**The label is placed from `strokes.labelX/labelY/labelRotRad`**, not from `an.insX/insY`. For linear
and aligned those are the same value; for an angular dimension they are not, the label riding the arc.

## Files affected

| File | Change |
|---|---|
| `src/io/DxfIo.cpp` | both dimension sites routed through `CadDimStroke.hpp`; ~60 lines of duplicated geometry removed |
| `tests/headless/transcripts/issue252-dimangular-dxf-geometry.txt` | new regression transcript |
| `spec/requirements.md` | REQ-111's Purpose corrected — it claimed only the aligned case exported as exploded lines |

## Test approach

`headless.issue252-dimangular-dxf-geometry` draws **only** an angular dimension, so the assertion
cannot be satisfied by another entity's geometry: before the fix, the `ENTITIES` section of that
export contained no `LINE` at all.

**Negative-tested, which is the whole point of it.** With the fix reverted the transcript fails at the
intended step with the intended message:

```
FAIL [expect] step 11 (line 45): FILECONTAINS: ...issue252-angular.dxf does not contain: LINE
```

Deliberately **not** asserted: how many segments the arc tessellates into. `EXPECT LINEXYZ` could pin
it, but only by hard-coding one tessellated vertex, and that would fail the day the chord tolerance
changes — for a refinement, not a regression.

## Verification

- `./dev/test` — **1063/1063**.
- The behaviour change was **measured, not assumed**: the before/after entity counts above come from
  running the same transcript against `beta`'s `DxfIo.cpp` and against the fix, not from reading the
  code.
- No existing test changed behaviour, which is itself a finding: the whole DXF dimension path had no
  coverage that could see this, which is why the bug shipped.

## Assumptions

- **Arrowhead outlines are acceptable in an exploded export.** The export is already exploded
  linework; a filled head would need a new entity type for one caller. If a reviewer wants filled
  heads, `SOLID` is the mechanism and the handle count already routes through one function.

## Technical debt

- **DEBT-1 — `PdfPlot.cpp` draws `strokes.segs` and ignores `strokes.arrows`.** So the PDF plot has
  the same inset dimension line with no arrowhead — the gap this task closes for DXF is still open
  for PDF. Pre-existing, out of scope here, and worth its own issue.
- **DEBT-2 — no dimensions requirement.** See *Requirement authority*.
