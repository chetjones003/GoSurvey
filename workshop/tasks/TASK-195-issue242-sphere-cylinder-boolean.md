# TASK-195 — Booleans: sphere ∩ cylinder (issue #242, REQ-314 B2b-2 continued)

## Requirement authority

- **REQ-314** increment B2b-2 (ADR-046, D-2026-09-02-i, D-2026-09-03-a). Continues the plan in
  `workshop/tasks/TASK-186-issue147-boolean-b2b2-procedural-curve.md`.
- **Issue #242**, first checklist item: *"Sphere ∩ cylinder — a drilled hole through a sphere, a
  spherical seat on a pipe."*
- Constraints: REQ-101 (±0.01 ft), REQ-201 (a failed Boolean leaves both operands untouched and is
  reported by name), REQ-300 (in-tree kernel), REQ-100 profile (d).

## What exists

`TryBooleanCurved` (`src/util/brep.cpp`) recognises cylinder×cylinder (Steinmetz, branch-pipe,
coaxial), cylinder×box, and sphere×box. **There is no sphere×cylinder branch at all** — every
sphere×cylinder pair falls through to `Fail(Problem::BooleanCurvedFace)`.

The B2b-2 machinery from the branch-pipe slices is in place and reusable:
`CurveKind::Intersection` edges, `MarchIntersectionCurve` / `SettleOntoIntersection`, `IsectStrip`
+ `IntegrateCylinderFaceNumeric` (numerical integration of a **cylinder** face bounded by a
procedural curve), `.gs` `kGsFormatVersion` 3.

## The geometry — two distinct sub-cases

Cylinder radius `r`, sphere radius `Rs`, `r < Rs`.

### (1) Axis through the sphere centre — **the intersection is two circles, not a quartic**

`x²+y²=r²` and `x²+y²+z²=Rs²` ⟹ `z = ±√(Rs²−r²)`. Two plane circles of radius `r`. The result of
each operation is closed-form and uses only `CurveKind::Arc`:

| op | result |
|---|---|
| INTERSECT | a cylindrical mid-band capped by two spherical caps — a "ball with its sides milled flat off"… no: a barrel with spherical ends |
| SUBTRACT (sphere − cyl) | a sphere with a clean cylindrical hole straight through it (genus 1) |
| SUBTRACT (cyl − sphere) | a cylinder with a spherical bite from its middle (two stubs, or a waisted piece) |
| UNION | a sphere with a cylindrical boss out each side |

This is the common real case ("drill a hole through the middle of a ball", "spherical-nosed pin")
and carries none of B2b-2's risk. It is a **B2b-1-style** closed-form recogniser.

### (2) Axis offset from the sphere centre — **a genuine quartic**

The cylinder axis parallel to a sphere diameter but displaced by `d` (`0 < d`, `d + r < Rs` for a
clean hole). `x²+y²=r²` with centre `(d,0)`; on the sphere `z² = Rs² − (x²+y²+2dx… )` — a quartic
loop. This is the case that needs `CurveKind::Intersection` and a **numerically-integrated sphere
face** (new — A2 only did cylinder faces; `IntegrateFace`'s `Sphere` branch has no
intersection-edge path). Skew axis is a third, harder case.

## Proposed slicing

- **Slice A (this task) — centred sphere ∩ cylinder, all three ops, closed-form.**
  `ClassifyCylinder` + `ClassifySphere`, detect the axis passing within `1e-7·scale` of the sphere
  centre and `r < Rs` and the cylinder clear of nothing (it always crosses cleanly when centred).
  New builders `BuildSphereCylinder{Intersection,Subtract,Union}` following the Steinmetz builders'
  shape (arcs + sphere caps + cylinder band; `Surface::inward` for the bored wall in SUBTRACT).
  Volumes checked closed-form: spherical cap `πh²(3Rs−h)/3`, cylinder band `πr²·2√(Rs²−r²)`, etc.
  `.gs`: **no version bump** — arcs and primitive faces only.
- **Slice B — offset-axis sphere ∩ cylinder (the quartic), INTERSECT then SUBTRACT then UNION.**
  Adds the intersection-edge branch to `IntegrateFace`'s `Sphere` case (an `IsectStrip`-analogue
  over the sphere's `v`/latitude at each `u`/longitude) and a `BuildSphereCylinderOffset…` family.
  `.gs` stays v3.
- **Slice C — skew axis.** Deferred; the hardest marching case, shared with the cylinder-cylinder
  skew slice.

## Open question for the user

**Take Slice A (centred, closed-form) first, or go straight to Slice B (the offset quartic)?**

Slice A mirrors the project's established phasing — B2b-1 (ellipse, closed-form) preceded B2b-2
(quartic); the Steinmetz coda (closed-form) preceded the branch-pipe quartic. It ships the common
"hole through the middle of a ball" case with low risk and leaves the quartic marching for a
follow-up. Recommend **Slice A first**.

## Test approach

`BrepTests` per op: volume vs the closed-form value (cap + band formulae) on-axis, on a tilted
survey-magnitude frame, and after `Translate`; topology (genus for the bored cases); reversed
operand order; the non-recognised configs still refused by name. A headless `.gs` round-trip is
deferred pending a `CYLINDER` axis option (same note as the branch-pipe slices).

## Status

**Decision (user, this session): Slice A first — centred, closed-form.**

**Slice A / INTERSECT — implemented.** `src/util/brep.cpp`:

- `BuildSphereCylinderIntersection(fr, r, Rs, ...)` — a cylindrical band (`z ∈ [−h, h]`,
  `h = √(Rs²−r²)`) capped by the two spherical zones the cylinder encloses. 6 vertices, 10 edges,
  6 faces; band winding ported from `BuildCoaxialStack`, cap winding from `BuildSphericalCap` (top
  cap direct, bottom cap the top loop reflected through `z = 0`). Every edge a `CurveKind::Arc`,
  every face closed-form — **no `.gs` version bump**.
- `TryBooleanSphereCylinder(S, C, op, ...)` — recognises the centred case: sphere centre on the
  cylinder axis line (`1e-7·scale`), `r < Rs`, and **both cylinder caps clear of the sphere**
  (`along ≥ Rs` and `length − along ≥ Rs`, else the result would keep a flat disk piece). Only
  `INTERSECT` is `*handled`; `SUBTRACT` / `UNION` and every offset/skew config fall through to the
  existing `BooleanCurvedFace` refusal.
- Wired into `TryBooleanCurved` after the cylinder×box branches.

Tests — `BrepTests` "Curved B2b-2 first pair: sphere INTERSECT cylinder": closed-form volume
`2πr²h + 2·π(Rs−h)²(2Rs+h)/3` and area `4πrh + 4πRs(Rs−h)` to 1e-12; `RequireWindingMatchesNormals`
on the tessellation; a tilted survey-magnitude frame + `Translate` to 1e-9; reversed operand order;
`SUBTRACT`/`UNION` refused; an offset axis refused; a cap-inside-sphere config refused. Full suite
green.

**Next:** Slice A / SUBTRACT (sphere − cylinder = drilled hole, genus 1; and cylinder − sphere =
waisted piece), then Slice A / UNION. Then Slice B (the offset quartic).
