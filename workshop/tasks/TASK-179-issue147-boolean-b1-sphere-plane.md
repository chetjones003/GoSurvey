# TASK-179 — Booleans B1: sphere ∩ plane (REQ-314 increment 4 / ADR-046 B1, GitHub issue #147)

## Status

**IMPLEMENTED 2026-09-02.** Follows TASK-178 (cylinder curved B1). One focused PR.

## Requirement authority

- **REQ-314** acceptance names "**sphere ∩ plane**" among B1's line/arc-intersection pairs; ADR-046
  (c) lists "sphere ∩ plane" explicitly as B1.
- **D-2026-09-02-b** — curved SUBTRACT is deferred to B2 (an inward-facing curved face). Applies
  here too: scooping a sphere out of a box needs an inward sphere face.
- REQ-201, REQ-101, REQ-301. Builds on TASK-178's recogniser pattern.

## Scope

A **sphere** combined with a **planar-faced solid** where exactly **one** planar face of the solid
cuts the sphere and the cut circle lies wholly inside that face:

- **INTERSECT** → the spherical **cap** on the solid's material side (`BuildSphericalCap`): two
  longitude half-faces of the sphere with a raised latitude span, plus one planar disk closing the
  cut. Volume is the exact cap integral (`SphericalFaceIntegrals` over the shrunk `v`-span).
- **UNION** → a **boss**: the solid's face is bored open at the cut circle and the sphere cap that
  pokes out the other side is added, no disk (`BuildSphereBoss` — the `BuildBoss` pattern with
  sphere-cap faces). Volume = `vol(P)` + the outside cap.
- **SUBTRACT** → refused `BooleanCurvedFace` (→ B2).

Trivial routing: sphere wholly inside `P` (INTERSECT → the sphere, UNION → `P`); disjoint
(INTERSECT → `BooleanEmptyResult`, UNION → both).

### Refused by name (carried to a later B1 sub-slice / B2)

- The cut circle **crosses a face edge** — a mixed arc/line intersection curve (`messy` →
  `BooleanCurvedFace`).
- The sphere is clipped by **more than one** face (a corner) — a spherical triangle the parametric
  span cannot express (`BooleanCurvedFace`).
- **sphere ∩ sphere** (offset spheres meet in a circle — also `{Line, Arc}`, a natural next slice).
- **sphere − anything** curved SUBTRACT.

## Implementation

`brep.cpp`: `SphereShape` / `ClassifySphere` (2 sphere half-faces, 2 poles, 2 meridians);
`BuildSphericalCap(frame, radius, cutZ)` — keeps the `+frame.zAxis` cap, built canonically then
placed; `BuildSphereBoss(planar, faceIdx, centre, axis, radius, cutZ)`; `TryBooleanSpherePlanar`
wired into `TryBooleanCurved` after the cylinder recognisers.

The cap's sphere frame Z is chosen so the kept cap is always "above" — INTERSECT uses `axis = -n`
(into `P`), UNION uses `axis = +n` with the plane at `-d`, so one keep-above builder serves both.

No new `Problem` value. No command change — `UNION`/`INTERSECT` already route any solid pair.

## Tests (`tests/BrepTests.cpp`, `[brep][req314]`)

- Sphere cut by one box face: INTERSECT cap volume vs. `π h²(3r−h)/3`, tessellation winding;
  UNION boss volume = box + outside cap, winding; SUBTRACT refused.
- Sphere straddling a box corner → `BooleanCurvedFace`.
- Sphere wholly inside a box → INTERSECT is the sphere, UNION is the box.
- Sphere far from the box → two solids.

Full suite **1004/1004**.

## Verification

`build-project`, `testing`, `code-review`, `architecture-review` — kernel stays graphics-free and
directly unit-tested; the sphere recognisers reuse TASK-178's shape (no new abstraction).
