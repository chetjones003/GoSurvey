# TASK-184 — Booleans B2a: inward-facing analytic faces, so a curved SUBTRACT can bore a hole (REQ-314 increment 5 / ADR-046 B2, GitHub issue #147)

## Status

**APPROVED 2026-09-02 (D-2026-09-02-c)** — Q1 split B2a/B2b, Q2 explicit `bool inward`, Q3 amend
ADR-045. SPEC updated (ADR-045 (d), ADR-046 (c), REQ-314, decision log).

**IMPLEMENTED 2026-09-02 (first slice — the cylinder bore).** `Surface::inward` added; normal
evaluators / tessellation winding / volume integrand flip on it; `.gs` gains the additive `inward`
key (no version bump). `BuildBore` in `brep.cpp`; `TryBooleanCylinderThroughPlanar` grows a
`cylIsMinuend` argument and drills the hole for `box − cylinder` (through **and** blind). Full suite
**1005/1005**.

### Delivered in this slice

- **`SUBTRACT(planar solid, cylinder)`** — a round **through-hole** (`BuildBore` through) or a
  **blind round pocket** (cylinder stops inside; a planar floor closes it). Volume `vol(P) − π r² L`
  exact, tessellation winding matches the flipped normal, `.gs` + `Translate` carry the flag.

### Slice 2 — the spherical dimple (2026-09-02)

`SUBTRACT(planar solid, sphere)` where one face cuts the sphere: a **spherical dimple**.
`BuildSphereBoss` gained a `bool inward` — the cap faces are marked `inward` and every half-face
loop is reversed, with the bored face taking the opposite circle winding. `TryBooleanSpherePlanar`
gained a `sphIsMinuend` argument. Volume `vol(P) − π h²(3r−h)/3` exact.

### Deferred to follow-up B2a slices (still refused `BooleanCurvedFace`, by name)

- `SUBTRACT(cylinder, planar solid)` — a box notch cut from a cylinder (`cylIsMinuend`).
- `SUBTRACT(sphere, planar solid)` — a box corner off a sphere (`sphIsMinuend`).
- `SUBTRACT(coaxial cylinder, coaxial cylinder)` — a **counterbore / tube** (`BuildCoaxialStack`
  needs a non-zero inner radius per band).
- A cylinder whose base is inside the solid (a floating pocket).

## The problem this solves

Every curved **SUBTRACT** is refused today (`BooleanCurvedFace`), because subtracting a cylinder or
a sphere leaves a wall that faces **inward** — material on the far side, void on the near side — and
`brep::Surface` was deliberately built with no way to say that (ADR-045: "There is no separate
'reversed' flag"; `Problem::ProfileArcReflex` refuses the extrude version of the same shape). That
was recorded as SPEC GAP **D-2026-09-02-b**, deferring curved SUBTRACT to B2.

"Subtract a cylinder to drill a round hole" is the single most common operation in solid modelling,
so this is the highest-value remaining piece of REQ-314.

## Key observation — this needs NO new curve type

ADR-046's B2 has two independent parts:
1. **inward-facing curved faces** (this task, B2a);
2. a **general analytic intersection curve** (`CurveKind::Intersection` — ellipse, quartic) for
   oblique / non-coaxial operands (B2b, a later task).

The drill-a-round-hole case's intersection curves are **full circles** — already `CurveKind::Arc`,
already computed by TASK-178/179's recognisers. Only part (1) is missing. So B2a is a contained,
high-value slice that lifts the biggest refusal without touching the hard geometry.

## Scope of B2a

Add an inward form to `brep::Surface` and lift the curved-SUBTRACT refusal for exactly the operand
pairs TASK-178/179 already recognise geometrically:

- **`SUBTRACT(planar solid, cylinder)`** where the cylinder axis is perpendicular to two faces and
  its footprint is clear — a **round through-hole** (genus rises by 1) or, if it stops inside, a
  **blind round pocket**. Bored faces get an inner circle; the hole wall is a cylinder face marked
  `inward`.
- **`SUBTRACT(cylinder, planar solid)`** — a box notch cut from a cylinder (the remaining cylinder
  wall is still outward; only the new planar cut faces are added — actually representable **without**
  the inward flag, so this comes along for free).
- **`SUBTRACT(planar solid, sphere)`** — a **spherical dimple / cavity** in a face; the dimple wall
  is a sphere face marked `inward`.
- **`SUBTRACT(coaxial cylinder, coaxial cylinder)`** — a **tube** or a **counterbore** (annular
  faces + an inner cylinder wall marked `inward`). Extends `BuildCoaxialStack` with a non-zero inner
  radius per band (the `rIn` the struct was already sketched for in TASK-178).

Still deferred to **B2b**: oblique plane ∩ cylinder (ellipse), non-coaxial cylinder ∩ cylinder
(quartic), sphere clipped by more than one face, and any pair whose intersection curve is not a
line or an arc.

## Representation — proposed

`brep::Surface` gains **`bool inward = false`**. When true, the face's material is on the
**−normal** side of its analytic surface (−radial for cylinder/cone/sphere/torus). Plane faces never
set it — a plane's normal already points wherever it needs to.

Touch points (each a small, local change):

| Site | Change |
|---|---|
| `Surface` struct + `Translate` | new field; `Translate` unaffected (not a coordinate) |
| `ConicalNormal` / `SphericalNormal` / `ToroidalNormal` | negate when `inward` |
| `Tessellate` (curved-face triangle emission) | reverse winding when `inward`, so it still matches the (flipped) stored normal |
| `IntegrateFace` | negate `volTerm` when `inward` (area stays positive) — the face then correctly *subtracts* the void it bounds |
| `Validate` | unchanged — the closed-/positive-volume checks already handle a shell with inward faces; `radius > 0` still holds |
| `ClosestPointOnSurface` | unchanged (orientation-independent) |
| `SelfIntersects`, isolines | unchanged / lighting-only |
| `.gs` (`GsIo.cpp`) | write `sf["inward"]`; read `sf.value("inward", false)` — **additive, tolerant-key, no `kGsFormatVersion` bump**, a pre-B2a drawing round-trips byte-identically |

Then in `brep.cpp`: lift the `op == Subtract` early-refusal in `TryBooleanCylinderThroughPlanar` /
`TryBooleanSpherePlanar` / `TryBooleanCoaxialCylinders` and build the bored result — a `BuildBore`
helper in the spirit of `BuildBoss`, with the wall face carrying `inward = true`.

## SPEC changes required

- **ADR-045 amendment** — reverse the "no separate 'reversed' flag" decision for curved surfaces,
  with the rationale that the Booleans have now forced the general answer exactly as ADR-045
  anticipated ("Supported once the booleans force a general answer to inward-curving faces").
  `Problem::ProfileArcReflex` **stays** for now — a reflex profile arc in an *extrude* is a separate
  feature and out of scope here; note that it is now unblocked for a future task.
- **ADR-046 amendment** — B2 is delivered as **B2a** (inward faces, this task) then **B2b** (general
  intersection curve). Update the delivery order.
- **REQ-314 acceptance** — the curved-SUBTRACT lines deferred by D-2026-09-02-b are met in B2a.
- **Decision log** — a new `D-2026-09-0N` recording the split and the `bool inward` representation.

## Test approach (`tests/BrepTests.cpp`, `[brep][req314]`)

- **Round hole through a box** — `SUBTRACT`: `Validate` Ok, `SelfIntersects` false, volume
  `1000 − π r² h` to 1e-9, `EulerCharacteristic == 0` (a tunnel), tessellation winding matches
  normals (the wall shades as a concave surface), `.gs` round-trip preserves the `inward` face.
- **Blind round pocket** — volume `1000 − π r² d`, `EulerCharacteristic == 2` (a pocket).
- **Spherical dimple** in a box face — volume `boxVol − cap`, valid, winding.
- **Counterbore** — coaxial `SUBTRACT`, a stepped bore: annular faces + inner wall, hand volume.
- **A subtracted result's mass properties** still closed-form (the inward face's negative volume
  term is what makes the hole subtract exactly).
- Regression: every existing `[brep]` test still green (no primitive sets `inward`, so nothing moves).

## Verification

`build-project`, `testing` (full `BrepTests` + wider suite + `.gs` transcripts), `code-review`,
`architecture-review` (the flag is one field with a handful of one-line call-site changes; it does
not add an abstraction, it removes a limitation the SPEC anticipated removing).

## Decisions for the user

### Q1 — Split B2 into B2a (this) then B2b (general intersection curve)?

B2a lifts the round-hole refusal now with no new curve type; B2b (ellipse/quartic) is a much larger,
later task. Recommend the split — it matches ADR-046's own phasing philosophy (ship the verifiable
thing before the hardest geometry).

### Q2 — Represent an inward curved face as `bool inward` on `Surface`?

The alternative is a negative-radius convention (no new field, but a "radius" that is sometimes a
sign carrier — the kind of overloaded meaning ADR-045 avoided elsewhere). Recommend the explicit
`bool`.

### Q3 — This reverses an ADR-045 decision. Confirm.

ADR-045 chose *no reversed flag* on purpose, for simplicity. B2 is the point where that choice has
to give — every curved SUBTRACT depends on it, and ADR-045 itself named this as the trigger.
Recommend accepting the amendment.
