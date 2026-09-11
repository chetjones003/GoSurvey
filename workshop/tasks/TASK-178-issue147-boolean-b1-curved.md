# TASK-178 — Booleans B1, first curved slice: a cylinder drilled through a planar-faced solid (REQ-314 increment 4 / ADR-046 B1, GitHub issue #147)

## Status

**APPROVED 2026-09-02** — recogniser approach (Q1); coaxial cylinder ∩ cylinder folded in (Q2).

**SPEC GAP resolved (D-2026-09-02-b):** curved **SUBTRACT** (a round hole / bore) leaves a
cylinder wall facing inward, which ADR-045's `Surface` cannot express. Deferred to B2; refused
`BooleanCurvedFace` in B1. REQ-314 acceptance, ADR-046 delivery order, and decision log updated.

**IMPLEMENTED 2026-09-02.** Kernel: `ClassifyCylinder`, `BuildCoaxialStack`, `BuildBoss`,
`TryBooleanCurved` (+ two config recognisers) in `brep.cpp`; `BooleanObliqueCylinder` added to the
`Problem` enum; `Tessellate` gained an annular-strip path for a 2-loop plane face. `BooleanPlanar`
tries the curved recognisers before its planar refusal. 5 new `[brep][req314]` cases + transcript
`req314-boolean.txt` updated (curved UNION boss + INTERSECT plug). Full suite **1002/1002**.

Delivered:
- **INTERSECT** — cylinder ∩ planar solid (axis ⟂ two faces, footprint clear) and coaxial
  cylinder ∩ cylinder → the trimmed cylinder / `BooleanEmptyResult`.
- **UNION** — cylinder ∪ planar solid → a boss (the two faces bored open, stubs added); coaxial
  cylinder ∪ cylinder → merged cylinder (equal radius), stepped stack (`BuildCoaxialStack`), or two
  solids (disjoint along the axis).
- **Refusals by name** — `BooleanObliqueCylinder` (tilted cylinder → ellipse); `BooleanCurvedFace`
  for any curved SUBTRACT, a cone / sphere / torus operand, offset-parallel cylinders, and a
  cylinder whose footprint crosses a face edge (partial penetration).

Carried forward to later B1 sub-slices / B2: curved SUBTRACT, sphere ∩ plane, partial penetration
(arc+line intersection), offset-parallel cylinders (quartic).

Note: a bored / annular plane face is not a topological disk, so naive `V−E+F` is not 2 for a boss
or a stack — `Validate` (manifold, orientable, closed, positive volume) is the invariant that holds.

## Requirement authority

- **REQ-314** — Feature operations on the solid kernel (accepted 2026-09-02, D-2026-09-02-a).
  Acceptance names, in as many words:
  - "Union, subtract and intersect of two overlapping boxes, **a box and a coaxial cylinder**, and
    two coaxial cylinders each produce a solid whose volume matches the hand-computed value to
    within REQ-101, and which passes `brep::Validate`."
  - Degenerate case: "**a box and a cylinder tangent along a line**" — no spurious edge.
  - "**An operand pair that would produce an intersection curve outside `{Line, Arc}`** (a box and an
    obliquely-oriented cylinder …) is **refused with a specific reason** naming the surface pair, and
    stores nothing."
- **ADR-046 (c)** — Booleans are analytic B-rep, phased by intersection-curve difficulty. **B1**
  covers operand pairs whose every intersection curve is already a line or an arc: *"box ∩ box, box ∩
  axis-aligned cylinder, coaxial cylinder ∩ cylinder, sphere ∩ plane, and the like."* B2 adds the
  general analytic intersection-curve type.
- Builds on TASK-177 (B1 planar) and TASK-176 (slice), REQ-313 / ADR-045, REQ-201, REQ-101, REQ-301.

## Why this task exists

TASK-177 shipped B1 for **planar-faced operands only** — every curved operand is refused
`BooleanCurvedFace`. REQ-314's acceptance list is therefore **not yet met**: the "box and a coaxial
cylinder" and "box and a cylinder tangent along a line" cases have no implementation. This task is
the first curved slice of B1, chosen as the smallest one that clears an acceptance bullet.

## Scope of THIS task — a right circular cylinder whose axis is perpendicular to two faces of the other operand

The one curved Boolean configuration whose result provably stays inside `{Line, Arc}` and needs **no
general face-splitting machinery**:

- One operand is a **right circular cylinder** `C` (recognised from its faces: one `Cylinder`
  surface carried by two half-faces at a seam, plus two planar disk caps — *not* from the recipe, so
  an extruded circle qualifies too).
- The other operand `P` is any **valid planar-faced solid** (box, wedge, extruded rectilinear
  profile, an L-block — convex or not).
- `C`'s axis is **perpendicular** (within `1e-6` on the direction cosine) to exactly two of `P`'s
  planar faces — an "entry" face and an "exit" face — and `C` spans **clear past both** of them.
- `C`'s circular footprint projects **wholly inside** each of those two faces' loops, clear of every
  edge by more than tolerance (so the intersection curve on each is a *full circle*, never a circle
  clipped by a face edge — that clipped case produces arc **and** line edges and is deferred).

Under those conditions the three operations have closed-form B-rep results:

- **SUBTRACT (`P − C`)** — `P` with a **round through-hole**: each of the two faces gains an inner
  loop (a full circle, expressed as two half-circle arc edges at a seam, matching how the primitives
  express a rim); the hole is walled by a cylinder face equal to `C`'s side trimmed to the distance
  between the two planes, its normals facing **into** the hole. Genus rises by 1 (a tunnel) unless
  `P` was already multiply-connected. Volume `= vol(P) − π r² · t` where `t` is the plane spacing.
- **UNION (`P ∪ C`)** — `P` with the two stubs of `C` that stick out past the entry/exit faces added
  as cylinder + cap; the two faces keep their outer loop and gain the same inner circle, but the
  disk inside it is removed and replaced by the stub walls. Volume `= vol(P) + π r² · (len(C) − t)`.
- **INTERSECT (`P ∩ C`)** — just the segment of `C` between the two planes: a fresh cylinder of
  `C`'s radius and height `t`. Trivial. Volume `= π r² · t`.

### Deliberately deferred (still refused by name, as today)

- **Partial penetration** — `C` enters `P` through one face and stops inside, or its footprint
  crosses a face edge. Intersection curve is a circle *arc* joined to line segments on `P`'s side
  faces; needs face-splitting with mixed edge kinds. Next B1 sub-slice.
- **Sphere ∩ plane** — named in ADR-046 (c) as B1, own sub-slice.
- **(NOW IN SCOPE — Q2)** Coaxial cylinder ∩ cylinder: a second recogniser, below.
- **Cylinder axis not perpendicular to a face of `P`** (oblique) — ellipse, **B2**. Refused
  `BooleanObliqueCylinder` (new, see below) — this is the acceptance bullet that must name the pair.
- Cone, sphere, torus operands — refused `BooleanCurvedFace`.

## Implementation approach

A **recogniser** in `brep.cpp`, in the spirit of `SliceCurvedPrimitive` (TASK-176): `BooleanPlanar`
first tries `BooleanDrillThrough(a, b, op, …)`; if it does not recognise the configuration it returns
`handled = false` and `BooleanPlanar` falls through to today's planar path (which then refuses any
curved operand as now). No change to the planar path itself.

`BooleanDrillThrough`:

1. Identify which operand is a lone right circular cylinder (`ClassifyAsCylinder(s) -> {axis frame,
   radius, length}` — new small helper reading faces). If neither or both, `handled = false`.
2. Find the two faces of the planar operand perpendicular to the axis that the cylinder passes
   through; verify full-span and footprint-clear-of-edges. Any failure that means "curved but not
   this pattern" → `handled = true` + `Fail(BooleanObliqueCylinder or BooleanCurvedFace)`; a clean
   non-match → `handled = false`.
3. Detect **tangent / non-overlapping**: footprint touches a face edge exactly, or cylinder does not
   reach `P` → the acceptance "tangent along a line" case: `handled = true`, treat as
   non-overlapping (UNION → two solids, SUBTRACT → `P` unchanged, INTERSECT → `BooleanEmptyResult`).
4. Build the result topology directly (no welding of curved faces): reuse `MakeCylinder` for the
   INTERSECT and for the UNION stubs; for the hole, construct the two bored faces by taking `P`'s
   face loops and adding the inner circle, plus the trimmed cylinder wall (two half-faces + seam +
   two rim edges shared with the inner loops). Prove the whole result with `Validate` **and**
   `SelfIntersects` before writing anything (REQ-201 / ADR-046 (d)).

### Tessellation — one real sub-change

`Tessellate` today refuses a plane face with an inner loop (`PlaneFaceNotSimple`). The two bored
faces have exactly one inner loop (a circle). Smallest fix: in the plane-face path, when there is
one inner loop, triangulate the ring-between-loops by connecting each outer-loop sample to the
nearest inner-loop sample (an annulus strip). This is the only case B1 produces and matches the
"add the general polygon triangulation when a boolean first needs it" note already in `brep.hpp`.
No renderer change — feature results already tessellate through REQ-313's cached path.

### New `Problem` value

- `BooleanObliqueCylinder` — "A cylinder set at an angle to the other solid's faces would meet it
  along an ellipse, which needs the general Boolean (increment B2)." Satisfies the acceptance bullet
  that the oblique case is "refused with a specific reason naming the surface pair."

`BooleanNonConvex` stays unreachable (planar path lifted it in TASK-177); `BooleanCurvedFace` stays
for cone/sphere/torus operands and for a cylinder that meets the pattern's other preconditions but
whose footprint crosses a face edge.

## Test approach (`tests/BrepTests.cpp`, `[brep][req314]`)

- **Drill a round hole through a box** — `SUBTRACT(box 10³, cylinder r=2 through the full height)`:
  `Validate` Ok, `SelfIntersects` false, volume `1000 − π·4·10` to 1e-9, `EulerCharacteristic == 0`
  (a tunnel), tessellated volume within 1e-4, winding matches normals.
- **Boss through a box** — `UNION`, cylinder longer than the box: volume `1000 + π·4·(L−10)`, valid,
  genus 0.
- **Plug** — `INTERSECT`: a clean cylinder, volume `π·4·10`, and its faces/edges match `MakeCylinder`
  to 1e-9 on volume and area (ties the path to the primitive, like the revolve tests).
- **Coaxial cylinder ∩ cylinder** *(if in scope per Q2 — else its own task)*.
- **Tangent along a line** — cylinder footprint exactly touching a box edge: `UNION` returns two
  solids, no spurious edge; `SUBTRACT` leaves the box unchanged.
- **Oblique cylinder refused by name** — `BooleanObliqueCylinder`, nothing stored, operands
  bit-identical before/after.
- **Cone / sphere operand** — still `BooleanCurvedFace`.
- `.gs` round-trip of a drilled box (topology counts exact, volume/area to 1e-6).
- Survey-magnitude drilled box (easting 3.5e6) — volume to 1e-6.

## Verification

`build-project`, `testing` (full `BrepTests` + wider suite), `code-review`, `architecture-review`
(kernel stays graphics-free and directly unit-tested; recogniser adds no new abstraction with fewer
than two uses — `SliceCurvedPrimitive` is the precedent pattern).

## Commands

**No command change in this task.** `UNION` / `SUBTRACT` / `INTERSECT` already route two-or-more
solids through `BooleanUnion/Subtract/Intersect`; a curved pair that now succeeds simply stops being
refused. A follow-up task can add curved solids to the pre-selection filter if any exists.

## Decisions for the user

### Q1 — Recogniser vs. general machinery

A **special-case recogniser** (this plan) mirrors how curved SLICE shipped: low risk, no new kernel
representation, delivers the acceptance bullet now. The alternative is to build **general analytic
face-splitting** (split any face by any surface, carry mixed line/arc boundary loops, weld curved
faces) — that is most of B2's machinery and much higher risk. Recommend the recogniser.

### Q2 — Does this task also cover coaxial cylinder ∩ cylinder, or is that a separate task?

Coaxial cylinder ∩ cylinder is also inside `{Line, Arc}` (the shared boundary is a circle) and is
named in the same acceptance bullet. It is a *different* recogniser (annular cap faces, stepped
sides). Bundling it roughly doubles this task. Recommend a **separate follow-up task** so each lands
as a small, verifiable PR — the acceptance bullet is met incrementally across two PRs, same as the
planar B1 landed across four.
