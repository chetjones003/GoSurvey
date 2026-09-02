# TASK-175 — Revolve in the B-rep kernel (REQ-314 increment 2a, GitHub issue #147)

## Requirement authority

- **REQ-314** — Feature operations on the solid kernel (accepted 2026-09-02, D-2026-09-02-a).
- **ADR-046** — delivery order: extrude → **revolve** → slice → Booleans.
- Builds on REQ-313 / ADR-045 (kernel), REQ-311 (`ucs::Ucs`), REQ-101, REQ-201, REQ-301.
- GitHub issue #147, Phase 4 of #120.

## Scope of this task — the kernel half, straight-edged profiles

Mirrors the extrude split (TASK-174): `brep::Revolve` and its tests only — no command, no viewport,
no `.gs`, no renderer change. Those are increment 2b.

Revolve a **single closed planar profile of straight edges** about an axis **that lies in the
profile plane**, through a full or partial angle. Each straight edge sweeps one of:

- **Cylinder** — edge parallel to the axis.
- **Cone** (frustum, or with an apex) — edge skew to the axis.
- **Plane** (a disk or a washer/annulus) — edge perpendicular to the axis.

A partial revolve (`angle < 2*pi`) closes with two planar cap faces — the profile itself, rotated
into the start and end positions. A full revolve closes on itself and is seamed into two angular
halves so every edge is still used exactly twice (the cylinder builder's own trick).

**Arcs in the profile are refused for now** (`RevolveArcInProfile`): a revolved arc sweeps a portion
of a sphere or a torus, which is real analytic geometry the kernel has — but the topology
bookkeeping for it is increment 2b, and REQ-201 wants the boundary stated, not hit as a puzzling
validity failure.

## Implementation approach

- New public `bool Revolve(const Profile&, const Vec3& axisPoint, const Vec3& axisDir, double
  angleRad, Solid*, Problem*)`. Reuses the `Profile` / `ProfileEdge` types from TASK-174.
- Work in a 2D `(r, h)` half-plane fixed to the axis: `r` = signed distance from the axis along the
  in-plane radial direction, `h` = distance along the axis. Each profile vertex becomes `(r, h)`.
- Orient the profile CCW in `(r, h)` (reverse the walk if its signed area is negative) so the
  revolved faces come out with outward normals — the kernel's surfaces carry no "reversed" flag.
- Build directly in world coordinates (no `PlaceInFrame`, like `Extrude`): meridian edges are the
  profile edges rotated to each angular station; parallel edges are arcs about the axis at each
  non-axis vertex's radius. Nothing is stored unless `brep::Validate` passes (REQ-201).
- Refusals, each named: a non-finite / zero-length axis; an axis not in the profile plane; a
  profile that crosses the axis; a zero or out-of-range angle; an arc in the profile; and any
  result `Validate` rejects.
- Result carries `PrimitiveKind::None` and no recipe (ADR-046 (e)); `kGsFormatVersion` untouched.

## Test approach (`tests/BrepTests.cpp`, `[brep][req314]`)

- A rectangle offset from the axis, revolved a full turn → the REQ-313 cylinder (or a hollow tube),
  volume and area to a relative 1e-9.
- A right triangle with one leg on the axis, revolved a full turn → the REQ-313 cone.
- A trapezoid → a frustum; volume vs. the closed form.
- Pappus's theorem: volume = 2*pi * (centroid r) * (profile area), for a partial and a full revolve.
- A partial revolve's two caps are the profile area; the swept + cap areas close the solid.
- Placement / survey-magnitude invariance on a tilted axis.
- Refusals by name: axis off the plane, profile crossing the axis, angle 0 / > 2*pi, an arc edge.

## Verification

`build-project`, `testing` (full `BrepTests` + the wider suite), `code-review`,
`architecture-review` (kernel stays graphics-free and directly unit-tested).

## Status

**Kernel implemented — 2026-09-02 (PR #NNN).** `brep::Revolve` added; 5 new `[brep][req314]`
cases (rectangle == cylinder, right triangle == cone, Pappus full + 90° wedge, tilted-axis survey
magnitude, every refusal). Full suite green: **985/985 ctest**. No existing path touched — a new
function plus `Problem` values.

### Known limitations carried forward (all named, none silent — REQ-201)

- **Arcs in the profile are refused** (`RevolveArcInProfile`) — sphere / torus portions are
  increment 2b.
- **The profile must touch the axis** along one contiguous run (`RevolveProfileMissesAxis`) — this
  is what guarantees every face's material is on its −radial side, so no unrepresentable inner
  face arises. A hollow revolve (tube, washer) is a boolean SUBTRACT.
- A non-convex profile with a slot **facing the axis** would still slip past the touch-axis check
  in rare cases; `brep::Validate` is the backstop (nothing stored), though the message is then a
  generic validity reason rather than a specific one.
- The axis must lie in the profile plane; an oblique / skew axis is out of scope.

### Next: increment 2b

The `REVOLVE` command (select profile → axis by two points or an entity → angle, typed or dragged
with a ghost), and arc-profile revolve (sphere / torus portions) in the kernel.
