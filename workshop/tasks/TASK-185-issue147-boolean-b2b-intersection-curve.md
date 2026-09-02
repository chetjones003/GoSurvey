# TASK-185 — Booleans B2b: the general analytic intersection curve, starting with the ellipse (REQ-314 increment 5 / ADR-046 B2b, GitHub issue #147)

## Status

**APPROVED 2026-09-02 (D-2026-09-02-h)** — Q1 `CurveKind::Ellipse` first, Q2 SLICE first, Q3 bump
`kGsFormatVersion`. SPEC updated.

**SLICE IMPLEMENTED 2026-09-02.** Kernel: `CurveKind::Ellipse` + `Edge::radius2`; `AddEllipse`,
`EdgePointAt` / `ClosestPointOnEdge` / `Translate` / `PlaceInFrame` / `ComputeBounds` /
`PlaneLoopSignedArea` / `Validate` ellipse branches; `IntegrateTrigProduct` +
`CylinderPlaneCutIntegrals` + `CylinderCutZExtent` — a cylinder face whose z-extent is a plane
(`α + β cos u + γ sin u`) integrates in **closed form** (volume exact to 1e-9); `Tessellate` slopes
the cylinder band and fans the elliptical cap; `SegmentsForEdge` subdivides an ellipse edge.
`SliceCylinderOblique` builds the two elliptical-ended pieces, wired into `Slice` after
`SliceCurvedPrimitive`. `.gs`: ellipse edge write/read + **`kGsFormatVersion` 1 → 2** with a no-op
`MigrateV1ToV2` (a v1 drawing has no ellipse edge → byte-identical resave). Full suite **1024/1024**.

### Delivered

- `Slice(cylinder, oblique plane)` → two solids, each a cylinder with one circular and one
  elliptical rim; volumes sum to `π r² h` exact; elliptical cap area `π a b`. A cut steep enough to
  run a cap is reported `SliceResultComplex`.

### Boolean slice (INTERSECT), 2026-09-02

`CylinderCutZExtent` extended for **two** ellipse edges (a segment between two plane cuts).
`BuildObliqueCylinderPlug` (two oblique elliptical caps, cap normal = the *cutting-plane* normal,
not the tilted axis — the bug that made the first attempt 1.5% off). `TryBooleanCylinderThrough
Planar` grows a tilted branch: the axis crosses two planar faces at an angle, footprint clear →
**INTERSECT** builds the plug, volume `π r² · axialGap` exact.

### Tilted SUBTRACT — an elliptical-mouthed bore (2026-09-02)

`BuildTiltedBore`: each crossed face bored with an ellipse (arcs built about the face's outward
normal so the hole winds right), an **inward** cylinder wall spanning the two ellipse planes
(`CylinderCutZExtent`'s 2-ellipse branch gives its z-bounds). `TryBooleanCylinderThroughPlanar`'s
tilted branch now routes `box − tilted cylinder`. Volume `vol(P) − π r² · axialGap` exact.

### Deferred to the next PR / B2b-2

- **UNION** with a tilted cylinder (`BooleanObliqueCylinder` still refused) — the elliptical-mouthed
  *boss* (stub with one elliptical and one circular end).
- `cylinder − box` tilted (a notch).
- Isolines on a sliced cylinder face still draw full height (visual only).
- Everything B2b-2: cylinder ∩ cylinder (quartic), sphere ∩ cylinder, non-elliptical cone sections.

## What B2b is

B1 and B2a refuse any operand pair whose intersection curve is not a line or an arc, by name. The
big remaining case: an **oblique plane cutting a cylinder gives an ellipse**, and two non-coaxial
cylinders give a **quartic** space curve. ADR-046 (c): *"Increment B2 adds a general analytic
intersection-curve type — a parametric procedural curve evaluated from its two surfaces, tessellated
on demand — and the refusals are lifted pair by pair."*

## Key observation — the ellipse is closed-form, and it is 90% of the value

An oblique plane ∩ cylinder is **exactly an ellipse** — centre, a semi-major axis, a semi-minor
axis, a parameter range. No procedural evaluation, no marching, no Newton. It is the case behind:

- **SLICE a cylinder with a tilted plane** (currently `SliceCurvedFace`, with a test that names it:
  *"an OBLIQUE cut through a cylinder — an ellipse the kernel cannot hold"*).
- **`SUBTRACT` / `UNION` / `INTERSECT` a tilted cylinder with a box** (currently
  `BooleanObliqueCylinder`).

The genuinely general procedural curve (cylinder ∩ cylinder quartic, sphere ∩ cylinder, cone
sections that are parabolas/hyperbolas) is a **later sub-slice** — it needs the marching evaluator
and a face-integration scheme for a non-analytic boundary. This task does the ellipse first.

## Proposed representation

`CurveKind` gains **`Ellipse`**. `Edge` gains one field, **`double radius2`** (the semi-minor axis;
`radius` becomes the semi-major). An ellipse edge:

- `frame.origin` = ellipse centre; `frame.xAxis` = semi-major direction; `frame.yAxis` = semi-minor
  direction; `frame.zAxis` = the ellipse plane normal (the cutting plane's normal).
- `radius` = semi-major `a`; `radius2` = semi-minor `b`; `sweep` = signed parameter span (a full
  ellipse rim that was not split at a seam has `v0 == v1` and `|sweep| == 2π`, mirroring the
  full-circle arc convention already in `Edge`'s docs).
- Point at parameter `t`: `centre + a·cos(t)·xAxis + b·sin(t)·yAxis`.

This is the same `frame + radius + sweep` shape `Arc` already uses, plus one field — not a new
procedural type. The procedural `CurveKind::Intersection` is deferred to B2b-2.

## Scope of this task (B2b-1) — oblique plane ∩ right circular cylinder

### Kernel

- `CurveKind::Ellipse`, `Edge::radius2`, `EdgePointAt` ellipse branch, `Validate` degenerate-ellipse
  check (`a`, `b` both `> lenEps`; `|sweep| > eps`), `ClosestPointOnEdge` ellipse branch (nearest
  point on an ellipse — Newton in one variable), `Bounds` for an ellipse edge, `Translate` (the
  frame origin moves; axes and radii do not — already the `Arc` rule).
- **Face integration for a cylinder face cut by an ellipse.** The oblique cut makes the side face's
  `z`-upper-bound a function of longitude: `z_cut(u) = α + β·cos u + γ·sin u` (linear in the plane
  equation). The `z`-integral is elementary; the remaining `u`-integral is of trig polynomials and
  stays closed-form — a new `ConicalFaceIntegrals` variant taking `(α, β, γ)` for the top bound
  instead of a constant `h`. The **new planar elliptical cut face** integrates by the existing plane
  formula (its area is `π·a·b`, its `PlaneLoopSignedArea` needs an ellipse term — the analogue of
  the arc bulge term already there).
- `Tessellate`: the elliptical planar cut face (fan / ear-clip from the ellipse polyline); the
  cylinder side face with one curved `z`-bound.
- `.gs`: `Edge` serialization writes `r2` and the frame for an `Ellipse` edge — **additive**, and
  `kGsFormatVersion` **is** bumped this time (a reader that does not know `CurveKind::Ellipse` would
  mis-read the edge — unlike every prior B1/B2a change, this one is not backward-tolerable). ADR-045
  (e) / ADR-046 (e) said the bump lands "in the increment that first writes a recipe, **if any
  does**" — this is the first increment that adds a stored geometry *kind*, so the bump is here.

### Operations

- **`Slice`** — an oblique plane through a cylinder → two valid closed solids, volumes summing to
  the original within REQ-101. Lifts `SliceCurvedFace` for this case.
- **Booleans** — a tilted cylinder combined with a planar-faced solid, where the cylinder enters and
  exits through planar faces of the solid, cutting an ellipse on each. `TryBooleanCylinderThrough
  Planar` stops refusing `BooleanObliqueCylinder` when it can build the elliptical bores/bosses
  (`inward` ellipse-and-arc-bounded cylinder faces reuse B2a's `Surface::inward`).

### Deferred to B2b-2 (still refused by name)

- cylinder ∩ cylinder (quartic) — needs the procedural `CurveKind::Intersection` + marching.
- sphere ∩ cylinder, torus ∩ anything.
- cone sections that are not ellipses (parabola / hyperbola — open curves needing clipping).
- an ellipse cut that crosses a cylinder seam or a cap rim in more than the two clean points.

## SPEC changes required

- **ADR-046 (c) / delivery order** — B2b splits into **B2b-1** (`CurveKind::Ellipse`, oblique
  plane ∩ cylinder) and **B2b-2** (procedural `CurveKind::Intersection`, the rest).
- **ADR-045 (b)** — amend: `CurveKind` is now `{Line, Arc, Ellipse}`; an ellipse edge carries a
  second radius. Note the `kGsFormatVersion` bump.
- **REQ-314 acceptance** — the "obliquely-oriented cylinder … refused with a specific reason" line
  is now *met for SLICE and Boolean* rather than a permanent refusal; the quartic case keeps its
  refusal.
- **Decision log** — a new `D-2026-09-0N`.

## Test approach (`tests/BrepTests.cpp`, `[brep][req314]`)

- SLICE a cylinder by a plane tilted 30° off perpendicular: two valid solids, `SelfIntersects`
  false, volumes sum to `π r² h` within 1e-9, each side's elliptical cut face area `== π a b`.
- The ellipse edge: `EdgePointAt(0)` and `EdgePointAt(1)` are `v0` / `v1`; a full-ellipse rim
  round-trips through `.gs` with `r`, `r2`, `sweep` intact.
- Mass properties of an obliquely-capped cylinder vs. the textbook value (a cylinder cut by a plane
  through its axis at angle θ has volume `π r² · z̄` where `z̄` is the mean cut height) to 1e-9.
- Boolean: a 20°-tilted cylinder through a box → `SUBTRACT` removes `∫` the elliptical-section
  volume, hand-checked; `Validate` Ok; winding matches normals.
- Still-refused: two crossed cylinders → `BooleanCurvedFace`; a parabolic cone cut → `SliceCurvedFace`.
- `.gs` `kGsFormatVersion` bump: a drawing with an ellipse-edge solid fails to load on the old
  version number and loads clean on the new one; a drawing with **no** ellipse edges still
  serializes byte-identically (the bump is a number change, not a format change, for those).

## Verification

`build-project`, `testing` (full `BrepTests` + `.gs` migration transcripts + wider suite),
`code-review`, `architecture-review` (a new `CurveKind` is the one anticipated kernel addition —
ADR-046 named it; the ellipse is its smallest, closed-form first member).

## Decisions for the user

### Q1 — Add `CurveKind::Ellipse` (closed-form) now, and defer the general procedural `CurveKind::Intersection` to B2b-2?

The ellipse is exact and needs no marching evaluator; it covers oblique plane ∩ cylinder, which is
the entire "tilted cylinder" story. The procedural curve is a much larger, separate step for the
quartic cases. Recommend the split.

### Q2 — First operation: SLICE, then Boolean, as two PRs?

SLICE (one cut, two pieces, volume-sum check) is the cleaner place to build and test the ellipse
machinery; the Boolean reuses it. Recommend SLICE first.

### Q3 — `kGsFormatVersion` bump

Unlike every B1/B2a change, an `Ellipse` edge is not backward-tolerable — an old reader mis-reads it.
Confirm the bump lands here (a drawing with no ellipse edges still round-trips identically; only its
version number moves).
