# TASK-172 — ISOLINES: making a curved solid read as curved (issue #146, REQ-313 amended)

## Requirement authority

- **REQ-313** amended — the wireframe of a curved solid.
- **D-2026-09-01-g** — the recorded decision (a `.gs` settings key and a new command).
- REQ-064 (2D Wireframe is the default style), REQ-100, REQ-201, REQ-301.
- GitHub issue #146. Second of three pieces sequenced with the user; follows TASK-171.

## The report

> "check all of the wireframe for these objects and make them look similar to autocad and cleaner
> than the current model in gs"

Fair. A solid's **topological edges alone are a poor picture of it**:

| Solid | What its edges are | What that draws as |
|---|---|---|
| Cylinder | 2 rims + 2 seams | two circles joined by two lines |
| Sphere | 2 meridians | a lens, not a ball |
| Cone | base rim + 2 slant seams | a triangle on a circle |
| Torus | 4 rings | flat concentric circles |

Every CAD package adds curves *across* the curved faces for exactly this reason. AutoCAD calls the
count `ISOLINES`.

## What it does

`brep::TessellateIsolines(solid, count, chordTolerance, out)` emits those curves from the **same
analytic surface evaluator the shaded triangles use**, so an isoline and the shading beside it cannot
disagree about where the surface is.

The count is per **full turn** (AutoCAD's semantics), defaults to 4, and is set by an `ISOLINES`
command in the report-or-set shape `VS` and `PERSPECTIVE` use. Clamped to 0..256; **zero is legal**
and means edges only.

## The three choices worth recording

**Directions are per surface kind, not a blanket rule.** A cylinder and a cone get rulings *along*
the axis only; a sphere gets meridians *and* latitude circles; a torus gets tube *and* ring circles;
a plane gets none. Applying one rule everywhere would put a horizontal ring part way up a cylinder,
which AutoCAD does not draw and which reads as an edge that is not there — a seam, or the join of two
stacked solids.

**The grid is fixed to the surface's own frame, and sampled strictly inside each face's span.** Every
curved primitive here is split into half-faces at a seam (ADR-045 (d)). A per-face grid would bunch
the lines where two faces meet; a non-strict test would drop an isoline exactly on a seam edge that
is already drawn. Both are pinned by tests.

**Isolines share the edge buffer.** They are the same colour and weight as the object — AutoCAD draws
them as part of it — so a separate batch would be a second thing to keep in step for no visible
difference. **The renderer needed no change at all**, which is the clearest evidence the seam was
chosen in the right place.

## Test approach

`BrepTests [brep][req313]` for the geometry, because that is where it can be checked exactly:

- a cylinder gets **two** grid rulings (the other two fall on the seams, which are already edges) —
  four vertical lines on screen, which is what AutoCAD shows — and each is a **single straight
  segment**, since a ruled surface makes a chord exact;
- a sphere gets meridians **and** latitude circles, and every emitted point is at the radius;
- a torus's points are all at the minor radius from the ring's centre circle;
- a box gets **none**;
- zero is legal and yields nothing;
- more isolines means more curves, and **never one on a seam** — asserted over every curve;
- a bad tolerance and an invalid solid are refused like the other tessellators.

`headless.req313-solid-isolines` for the half the kernel cannot see: that the setting reaches the
**display**, that changing it redraws the wireframe **and nothing else** (the triangle count is
asserted unchanged), that bad values are refused with the setting left standing, and that it survives
a `.gs` round trip. New count verb `EXPECT SOLIDEDGESEGS`.

The segment counts there are exact rather than approximate, and can be: the tessellation is
deterministic, so the same solid at the same tolerance gives the same segments every time.

## Verification

- `ctest`: **971/971 green.**
- **Negative-tested:** suppressing the isoline call so only edges reach the cache reports
  `SOLIDEDGESEGS: expected 104, got 102` — the two rulings, exactly.

## Assumptions

- **ASSUMPTION-1 (stated):** four is the right default. It is AutoCAD's, it is enough for a cylinder
  to read as round, and it is few enough that a drawing full of solids stays legible.
- **ASSUMPTION-2 (stated):** a sphere's latitude count is half the meridian count. Its `v` is a
  latitude over `[-pi/2, pi/2]` rather than a full turn, so the global-grid rule does not apply —
  half of that grid's lines would fall outside the surface. Evenly spaced interior latitudes at half
  the count reads as a net rather than a cage.

## Technical debt / stated boundaries

- **DEBT-1 — no silhouette curves.** AutoCAD also draws the true silhouette of a curved surface,
  which moves as the view orbits. Isolines are fixed to the object and do not. That is a
  view-dependent render pass, not geometry, and it is its own work.
- **DEBT-2 — the wireframe is verified as geometry, not pixels.** That the right curves reach the
  renderer is asserted; that they are drawn is a GUI pass, the same category REQ-064's styles sit in.
- **DEBT-3 — POLYSOLID still does not exist.** The last of the three pieces, and the one needing a
  sweep operation (#120 Phase 4), its own REQ and its own ADR.

## Status

Complete and verified. Goes to review, not done; the issue is not closed here.
