# TASK-277 — a layout viewport draws a section's curves, its closing span, and its tilted curves

- Type:    fix
- Status:  review
- Opened:  2026-09-23
- Owner:   Workshop
- GitHub:  — (found testing #531, asked for from the app: "see if when doing a section it shows in
  paper space through a viewport correct")

## Requirement authority

REQ-036 (floating viewports) / REQ-046 (per-viewport layer state) — the paper-space draw of model
geometry. REQ-201: what is drawn must be what is there.

## Why

Asked to check whether a section shows correctly in paper space. It did not, in three ways, all in
the viewport's own model-geometry draw in `CadUi.cpp`:

1. **Bulges were ignored.** The draw walked vertices and joined them with straight lines, so a level
   section of a cylinder — two vertices, each carrying a half-turn bulge — appeared through a
   viewport as **one straight line between the two points**. Every curved polyline was drawn as its
   chords.
2. **A closed polyline was drawn open.** The loop ran `k < end` with no wrap, so the closing span of
   every closed outline — which is every section — was missing.
3. **A tilted curve was drawn flat.** A tilted arc (REQ-312) and the tilted ellipse a tilted section
   now creates (#531) were sampled with world-XY arithmetic at one elevation, which is a curve
   neither of them has.

Model space drew all three correctly; only the paper-space viewport did not, which is why it survived
this long.

## What changed

`src/ui/CadUi.cpp`, the viewport's model-geometry draw:

- polylines are walked **span by span**: a span with a bulge is sampled through the shared
  `BulgeArc` (ADR-047, the same helper model space uses), with the height walked from one end of the
  span to the other, so a bulged span of a vertical section rises as it should; a closed polyline
  includes its closing span;
- a tilted **arc** is sampled through `CurvePlane` / `CurvePointAt`;
- a tilted **ellipse** is sampled through `EllipseWorldPointAt`.

A flat, straight polyline, a flat arc and a flat ellipse take exactly the arithmetic they took
before.

## Tests

Paper-space drawing is ImGui, so it has no headless assertion: this is verified by construction (the
shared helpers) and by eye in the app. The geometry the draw now follows is itself covered —
`[issue531]` for the tilted ellipse, `[issue522]`/`[sectioncoverage]` for the outlines, and
`BulgeArcTests` for the bulge helper.

Full suite after the change: 1776/1784. The 7 failures are `beta`'s own, plus one more that is also
`beta`'s: `PIPERUN END with only a start point refuses — a run needs two vertices` fails **through
ctest only** because its `TEST_CASE` name contains an em dash; the test itself passes when the binary
is run directly (38 of 38 `[piperun]` cases).

## Not in scope

- The paper-space entity draws (`paperEllipses`, `paperArcs`) are 2D by ADR-025 (g) and are untouched.
- PDF plotting of a tilted curve through a viewport (`PdfPlot.cpp`) is not part of this.
