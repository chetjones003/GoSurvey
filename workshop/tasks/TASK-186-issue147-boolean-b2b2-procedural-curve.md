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
that share an axis-intercept. **UNION / SUBTRACT of this pair (the T-pipe)** are still refused
`BooleanCurvedFace` — the next sub-slice. B2b-2 proper (the procedural curve) is unchanged and not
started.

## What is left

After B1, B2a and B2b-1, the curved-Boolean coverage is:

| operand pair | UNION | INTERSECT | SUBTRACT |
|---|---|---|---|
| axis-aligned cylinder × box | ✅ | ✅ | ✅ |
| tilted cylinder × box | ✅ | ✅ | ✅ |
| coaxial cylinder × cylinder | ✅ | ✅ | ✅ |
| sphere × box | ✅ | ✅ | ✅ |

Still refused **by name**: `cylinder − box` (a notch), and every pair whose intersection curve is
neither a line, an arc, nor an ellipse:

- **two non-coaxial cylinders** — a **quartic** space curve (a `T`-junction pipe, a lug boss);
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

### Scope of the first B2b-2 slice (proposed)

- `CurveKind::Intersection` + marching evaluator + tessellator + numerical face integration.
- **One operand pair**: two **non-coaxial cylinders**, axes coplanar, `INTERSECT` first (the
  bicylinder lens — hand-checkable against a fine numerical reference), then `UNION` / `SUBTRACT`.
- `.gs`: the new edge kind serialises its two surfaces + interval; **`kGsFormatVersion` 2 → 3**
  (a v2 reader cannot evaluate it), with a no-op v2→v3 migration.

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
