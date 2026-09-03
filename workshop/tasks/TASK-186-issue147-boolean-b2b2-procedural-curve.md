# TASK-186 — Booleans B2b-2: the procedural intersection curve (REQ-314 increment 5 / ADR-046 B2b-2, GitHub issue #147)

## Status

**APPROVED 2026-09-02 (D-2026-09-02-i)** — Q1 Steinmetz (perpendicular equal-radius cylinders,
closed-form) first; Q2 ADR-045 amended so a face bounded by a procedural intersection curve is
integrated by adaptive numerical quadrature (only that face — every analytic face keeps closed
form). B2b-2 proper (the procedural curve) stays a later increment; this task's first deliverable is
the **Steinmetz / T-pipe coda to B2b-1**.

**PROGRESS 2026-09-02** — Steinmetz coda increment 1 shipped: **INTERSECT** of two equal-radius
cylinders whose axes cross at right angles now builds the bicylinder in closed form
(`BuildSteinmetzIntersection` / `TryBooleanSteinmetz` in `src/util/brep.cpp`) — volume `16 r³/3`,
area `16 r²`, four `CurveKind::Ellipse` edges, no procedural machinery, no `.gs` version bump.
`CylinderCutZExtent` gained a mid-`u` tiebreak so its two-ellipse branch can order two cut planes
that share an axis-intercept. **B2b-2 proper (the procedural curve) is unchanged and not started.**

**PROGRESS 2026-09-02 (2)** — Steinmetz coda increment 2 shipped: **SUBTRACT** `A − B` of the same
pair (`BuildSteinmetzSubtract`) — cylinder `A` with a clean perpendicular cylinder-`B` channel bored
through it. 6 vertices / 12 edges / 8 faces; volume `vol(A) − 16 r³/3`, exact.

**PROGRESS 2026-09-02 (3)** — Steinmetz coda **complete**: **UNION** `A ∪ B` of the same pair
(`BuildSteinmetzUnion`) — a T-pipe / cross-pipe. 10 vertices / 20 edges / 12 faces: each cylinder's
wall becomes two bands (top/bottom for `A`, `±x` for `B`) split into half-faces through the crossing
points, plus all four flat caps; the two intersection ellipses are the internal seam. Volume
`vol(A) + vol(B) − 16 r³/3`, exact. **The whole Steinmetz coda (INTERSECT / SUBTRACT / UNION) is now
closed-form; B2b-2 proper (the procedural curve) is unchanged and not started.**

## What is left

After B1, B2a and B2b-1, the curved-Boolean coverage is:

| operand pair | UNION | INTERSECT | SUBTRACT |
|---|---|---|---|
| axis-aligned cylinder × box | ✅ | ✅ | ✅ |
| tilted cylinder × box | ✅ | ✅ | ✅ |
| coaxial cylinder × cylinder | ✅ | ✅ | ✅ |
| **equal-radius cylinders, perpendicular crossing axes** | ✅ | ✅ | ✅ |
| sphere × box | ✅ | ✅ | ✅ |

Still refused **by name**: `cylinder − box` (a notch), and every pair whose intersection curve is
neither a line, an arc, nor an ellipse:

- **two non-coaxial cylinders that are not the equal-radius perpendicular case** — a **quartic**
  space curve (an oblique branch pipe, a lug boss);
- **sphere ∩ cylinder** — a quartic;
- a **cone** section that is a parabola or hyperbola (an open curve).

B2b-2 is the increment that handles these. ADR-046 calls the general analytic Boolean
*"commercial-CAD-kernel work — years of specialist effort"*, and the phasing exists precisely so it
is attempted last, on a kernel whose shape is by now well understood.

## The special case worth taking first — perpendicular equal-radius cylinders

Two cylinders of **equal radius** whose axes **cross at right angles** meet along **two ellipses**,
not a quartic (the quartic `x⁴ = …` factors into `x = ±z`). So the **Steinmetz bicylinder**
(`INTERSECT`) and the `T`-pipe (`UNION` / `SUBTRACT`) of equal-radius perpendicular cylinders are
still `CurveKind::Ellipse` — a fifth config recogniser, no procedural curve. `INTERSECT` volume is
the textbook `16 r³ / 3`. This could be a small closed-form PR ahead of the real B2b-2, if wanted.

## B2b-2 proper — the procedural curve

### Representation

`CurveKind` gains **`Intersection`**. Such an edge carries **two surface descriptions** (copies, not
indices — an edge must survive `Translate` and `.gs` on its own) and a **parameter interval**. It is
**evaluated by marching**: from `v0`, step along the tangent (the cross product of the two surface
gradients), correct back onto both surfaces with a 2-D Newton, repeat to `v1`. The tessellator walks
it to a chord tolerance like every other derived representation; nothing is stored but the two
surfaces and the interval.

### The hard part — face integration

A face bounded partly by a procedural curve **cannot be integrated in closed form**. ADR-045 (b)
says *"volume and surface area are integrated in closed form"*. B2b-2 needs a carve-out: a face
whose boundary loop contains a `CurveKind::Intersection` edge is integrated by **adaptive numerical
quadrature** over its parametric domain, to a tolerance well inside REQ-101 (±0.01 ft). The closed
form stays the path for every face that has one — only the genuinely non-analytic face falls back.
This is a **SPEC change to ADR-045** and the pivotal decision below.

### First slice — confirmed scope (D-2026-09-03-a)

**Operand pair:** a thinner cylinder crossing a thicker one, **axes perpendicular and intersecting**,
**unequal radius** `r < R` — the pipe-tee / branch pipe. (Equal-radius intersecting axes always meet
along two plane ellipses — the Steinmetz coda already covers the perpendicular case, and the same
factoring `z² = (x sinθ + z cosθ)²` handles every other angle — so the genuine quartic needs `r ≠ R`.)
`INTERSECT` first.

**The intersection curve.** Big cyl `B` axis `= Z`, radius `R`; small cyl `A` axis `= X`, radius `r`.
`A`: `y² + z² = r²`. `B`: `x² + y² = R²`. On both: parametrise by `A`'s angle `u` — `y = r cos u`,
`z = r sin u`, and `x = ±√(R² − r² cos² u)` (always real since `r < R`). One closed quartic loop per
sign of `x`: the branch pipe's entry curve and its exit curve.

**Delivery — PR A (this task's next PR):**

1. **`CurveKind::Intersection`** enum member. `Edge` gains `std::vector<Surface> isectSurfaces`
   (exactly two entries for an Intersection edge; empty otherwise — no size cost on Line/Arc/Ellipse
   edges) and reuses `frame.origin` as an on-curve **witness point** near the parametric middle (it
   disambiguates the marching direction and seeds the Newton step; `Translate` / `PlaceInFrame`
   already move `frame.origin`).
2. **Marching evaluator** (`EdgePointAt`): from `v0`, tangent `= normalize(n₀ × n₁)` (surface
   normals), sign picked toward the witness; step, then a 2-D Newton correction back onto both
   surfaces (`ClosestPointOnSurface` twice, alternating, is enough); accumulate chord length; report
   the point at fractional arc-length `t`. `ClosestPointOnEdge` reuses the same march + a nearest-
   sample polish. `SegmentsForEdge` counts marched chords to the tolerance.
3. **Plumbing:** `Translate` and `PlaceInFrame` map `frame.origin` **and both `isectSurfaces[i].frame`**;
   `ComputeBounds` samples the marched curve; `Validate` gains an Intersection branch (two finite
   surfaces; `v0`, `v1`, witness each on both surfaces within `1e-7·scale`).
4. **Numerical face integration.** `IntegrateFace`: when a face's boundary loop contains an
   Intersection edge, integrate over the surface's own `u` with **Gauss–Legendre (32-node)**, the
   `z`/`v` limits at each node found by bisecting the boundary curve in `u`. Reduces to the existing
   `CylinderPlaneCutIntegrals` shape but with numerically-evaluated limits. `Validate`'s
   closed-surface point-invariance check keeps its `1e-8·scale³` bound for all-analytic solids and
   relaxes to `1e-6·scale³` when the solid contains an Intersection edge (follows from D-2026-09-02-i;
   still far inside REQ-101's `±0.01 ft`).
5. **`BuildBranchPipeIntersection`** + recogniser: two cylinders, perpendicular intersecting axes,
   `r ≠ R`, the thin one fully crossing the thick one clear of its caps → the lens (small-cyl wall
   segment + two big-cyl cap patches, bounded by the four quartic half-curves).
6. **`.gs`**: an Intersection edge writes its two surfaces + witness; **`kGsFormatVersion` 2 → 3**
   with a no-op v2→v3 migration (a v2 drawing has no Intersection edge, so its resave is
   byte-identical).

PR A is delivered in two: **A1** — steps 1–3 (representation + marching evaluator + `Translate` /
`PlaceInFrame` / `ComputeBounds` / `SegmentsForEdge` / `ClosestPointOnEdge` / `Validate` edge
branch), tested at the brep level with a hand-built Intersection edge; **A2** — steps 4–6 (numerical
face integration + `Validate` tolerance relaxation + `BuildBranchPipeIntersection` + recogniser +
`.gs` `kGsFormatVersion` 2→3).

**PR B:** `SUBTRACT` (the branch bored out of the main). **PR C:** `UNION` (the pipe tee).

**PROGRESS 2026-09-03 — PR A1 done.** `CurveKind::Intersection` + `Edge::isectSurfaces` +
`frame.origin` witness; `SurfaceNormalGeom` / `SettleOntoIntersection` / `MarchIntersectionCurve` /
`PointAtArcFraction`; `EdgePointAt` marches then settles onto both surfaces; `ClosestPointOnEdge`,
`SegmentsForEdge`, `Translate`, `PlaceInFrame`, `ComputeBounds`, `Validate` all gained an
Intersection branch. Tested: a pipe-tee intersection curve marched to <1e-6 on both cylinders,
endpoints exact, follows curvature not the chord, survives `Translate` at survey magnitude, bounds
contain it. No Boolean and no numerical integration yet — a solid with an Intersection edge still
fails `Validate`'s volume check (A2).

### Deferred within B2b-2 (later sub-slices)

- sphere ∩ cylinder, torus ∩ anything, cone parabola / hyperbola sections.
- `cylinder − box` (a notch) — planar cuts of a cylinder; arguably a B2b-1 leftover, small.
- Skew (non-coplanar) cylinder axes.

## Verification

`build-project`, `testing` (full `BrepTests` + `.gs` migration + wider suite), `code-review`,
`architecture-review` (the procedural curve is the one kernel type ADR-046 anticipated; the
numerical-integration carve-out is the one place ADR-045's closed-form rule bends, and only there).

## Decisions for the user

### Q1 — Take the perpendicular equal-radius Steinmetz case first (closed-form, `CurveKind::Ellipse`), before the procedural curve?

It is a small PR reusing existing machinery and delivers the T-pipe / bicylinder — a common shape —
without any of B2b-2's risk. Recommend yes, as a B2b-1 coda.

### Q2 — Amend ADR-045: a face bounded by a procedural intersection curve is integrated numerically (adaptive quadrature to a tolerance inside REQ-101), not in closed form?

This is the one place ADR-045's "closed form always" has to give. Every analytic face keeps its
closed form; only the non-analytic face falls back. The alternative is to keep refusing every quartic
pair forever, which leaves `T`-pipes and lug bosses permanently unbuildable. Recommend the amendment.

### Q3 — First procedural pair: two non-coaxial **coplanar-axis** cylinders, `INTERSECT` first?

The most common real case (a branch pipe), the easiest to hand-check, and the tangent/Newton
marching is at its simplest when the axes are coplanar. Skew axes and sphere∩cylinder follow.
