# TASK-188 — REQ-315 loft: the NURBS surface and the LOFT command (GitHub issue #147)

## Requirement authority

- **REQ-315** — Sweep and loft on the solid kernel (accepted 2026-09-03, D-2026-09-03-b).
- **ADR-048** — the freeform surface is a hand-rolled minimal NURBS patch; **loft is delivered
  before sweep**.
- Builds on: REQ-314 / ADR-046 (feature-operation layer), REQ-313 / ADR-045 (kernel + invariants).
- Constraints: REQ-101 (±0.01 ft), REQ-201 (no silent failure), REQ-300 (in-tree, no geometry
  library), REQ-301 (minimal abstraction), REQ-100 profile (d).
- Spec task: TASK-187. GitHub issue #147 / #241, Phase 4 of #120.

## Delivery slices (each its own PR, mirroring the B2b-2 A1/A2/B/C split)

### Slice A1 — the NURBS patch math *(this PR)*

New pure module `src/util/nurbs.{hpp,cpp}` — no GL, no `brep` coupling:

- `nurbs::Patch` — a rational tensor-product B-spline patch: degrees ≤ 3, clamped knot vectors, a
  row-major weighted control net.
- `ValidatePatch` / `PatchProblem` — degrees, control counts, knot-vector length, monotonic and
  clamped knots, finite control points, positive weights. Every fault named (REQ-201).
- `Evaluate`, `EvaluateWithDerivs` — Cox–de Boor basis (`BasisFuns` A2.2) and first derivatives
  (`BasisDers`, A2.3 restricted to order 1), then the rational quotient rule for the point, `dS/du`,
  `dS/dv` and the unit normal.
- `Translate` — every control point by an offset (the REQ-101 document-origin rebase hook).
- Builders loft needs: `RuledLinear` (a straight-span patch between two matched polylines),
  `RationalArc` (exact degree-2 rational control points for a circular arc of any sweep),
  `ArcRibbon` (a straight-span patch between two matched arcs).

Tests — `tests/NurbsTests.cpp`, 6 cases: `ValidatePatch` accept + every rejection; a ruled patch is
exact bilinear interpolation; a 3-point ruled patch passes through every point at its chord
parameter; `RationalArc` control points lie exactly on the circle for quarter / half / 3-quarter /
full / negative / sub-quarter sweeps; an `ArcRibbon` between coaxial arcs is a partial cylinder
(radial distance exact to 1e-7) and a full-turn ribbon closes; analytic derivatives agree with a
central finite difference of `Evaluate` to 1e-4; `Translate` at state-plane magnitude moves every
surface point by exactly the offset.

**No `SurfaceKind::Nurbs`, no `LOFT` command, no `.gs` change in this slice** — the module has no
`brep` caller yet, exactly as B2b-2 A1 added `CurveKind::Intersection` and its evaluator before any
Boolean used it.

### Slice A2 — the freeform face in the kernel *(next PR)*

- `SurfaceKind::Nurbs` + the `Patch` payload on `Surface` (additive, defaulted).
- `brep::Loft(const std::vector<Profile>&, Solid* out, Problem* outWhy)` — matched-edge-count planar
  profiles → NURBS side faces (one patch per corresponding profile-edge pair) + two planar caps.
- `ComputeMassProperties` routes a `Nurbs` face through **adaptive Gauss–Legendre quadrature**
  (ADR-045 (b) as widened by D-2026-09-03-b); volume checked against extrude / frustum / barrel
  hand-values within REQ-101.
- `Validate` support (the two-reference-point closure check already covers a NURBS shell),
  `Tessellate` + `TessellateIsolines` + `ClosestPointOnSurface` (Newton) for a `Nurbs` face.
- `.gs` gains the `Nurbs` surface encoding; `kGsFormatVersion` **3 → 4**; `io/GsMigrate` + CI check.

### Slice B — the command and the viewport *(next PR)*

- `LOFT` (typed + prompted), wired through the six places
  ([[project_3d_entity_checklist]]): route, submit-pick, command-line Enter, input hint / resolve,
  rubber preview (shares the builder with the commit), cancel; plus the `kRegistry` entry.
- Rendering in every REQ-064 style; selection / erase / one-undo-step; DXF/DWG skip message.

## Status

**Slice A1 implemented.** Build green, `ctest` 1037/1037. Slices A2 and B to follow as their own
PRs.

## Verification

- `build-project`: `./dev/build` clean (release).
- `testing`: `./dev/test` — 1037/1037, including 6 new `NurbsTests` cases.
- `architecture-review`: new module is pure `util/` (only `ray3d`), directly unit-tested, no GL —
  matches ADR-048 (c) and the ADR-002 layering. No existing file's behaviour changes.
- `dependency-audit`: no new dependency; the NURBS maths is hand-rolled (REQ-300).
