# TASK-204 — Oblique plane cuts of a cone: ellipse, parabola, hyperbola (REQ-314 B2b-2 tail, GitHub issue #283)

## Status

**APPROVED 2026-09-05.** Decisions recorded against the four questions below:

- **Q1 — all three regimes together** (ellipse, parabola, hyperbola), not parabola/hyperbola alone.
- **Q2 — reuse `CurveKind::Ellipse`** for the ellipse regime (cone-specific centre/axis derivation,
  no format cost); a new curve kind only for the parabola/hyperbola regime, which does bump
  `kGsFormatVersion`.
- **Q3 — numerical quadrature approved** for this face's area/volume (second ADR-045(b) extension: an
  analytic curve whose face integral has no tractable closed form), same tolerance discipline as the
  existing `Intersection`-curve carve-out.
- **Q4 — implement now.**

Implementation proceeds in two slices: **(a) the ellipse regime first** (reuses existing machinery,
no `.gs` bump, smallest risk), verified alone; **(b) parabola/hyperbola second** (new curve kind,
numeric face integration, `.gs` bump).

**PROGRESS 2026-09-05 — slice (a), the ellipse regime, done.** `ComputeConeObliqueEllipse` derives the
true centre/semi-axes/orientation (the cone's radius growth means the axis-plane intersection is NOT
the centre, unlike a cylinder — verified numerically against ~20000 random cone/plane configurations,
worst residual ~1e-9, before being coded) via the quadric-cone/plane-intersection diagonalisation
described above. `SliceConeOblique` mirrors `SliceCylinderOblique`'s topology exactly, reusing
`CurveKind::Ellipse` (Q2 as originally decided) — no `.gs` bump for this regime. Face integration
needed a genuinely new numeric path (`IntegrateConeCutFaceNumeric`, `ConeCutStrip`) since a cone's
z(u) at the cut is rational (not the trig-polynomial form `ConicalFaceIntegrals`/`CylinderPlaneCutIntegrals`
assume) — approved per Q3. Tessellation needed the equivalent `coneCut` branch (the existing cone
tessellation path only special-cased `SurfaceKind::Cylinder` z-extents; a Cone face silently kept its
full `[0,height]` span otherwise). Two real bugs were found and fixed during verification, both by the
same method: comparing against an independent numerical reference rather than trusting the derivation's
internal consistency —
1. **Sweep-direction bug**: the two half-ellipse arcs' signed sweeps were computed as `sweepLower` and
   `sweepUpper = 2*pi - sweepLower`, silently assuming both halves wind the same (positive) rotational
   direction. Cone-azimuth `u` and the ellipse's own parameter `t` need not agree in rotational sense,
   so each half's sweep is now found independently via its own interior witness point.
2. **volTerm sign bug**: `IntegrateConeCutFaceNumeric`'s cross term came out with the wrong sign
   (`+kk*(...)` instead of `-kk*(...)`, `kk` the cone's slope) — found by reproducing
   `ConicalFaceIntegrals` itself with the new per-`u` formula degenerated to a full `[0,h]` face and
   comparing directly; confirmed independently against a brute-force 2-D numeric double integral over
   the real cut face.
New test: "Curved B2b-2 tail: an oblique plane slices a cone into two elliptical-ended pieces"
(`BrepTests.cpp`, `[req314]`) — volumes against an independent closed-form circular-segment-area
integral (not reusing any of the derivation under test), tessellated volume, `.gs`/Translate round
trip, survey-magnitude stability, cap-clip refusal, and confirms the parabola/hyperbola regime still
refuses by name. Full suite green: 953→954 Catch2 cases, 1153 ctest (one pre-existing unrelated
failure, `RecentDrawingsTests`, confirmed present on unmodified `beta` too).

Parabola/hyperbola (slice b) not yet started.

## Requirement authority

- **REQ-314** (Domain: feature operations on the solid kernel — extrude/revolve/slice/booleans),
  increment B2b-2 (procedural / non-trivial-closed-form curved faces).
- **ADR-045 (b)**, amended (D-2026-09-02-i): a face bounded by a curve the kernel cannot integrate in
  closed form is integrated by adaptive numerical quadrature instead, to a tolerance inside REQ-101.
  That amendment was written for the two-surface `CurveKind::Intersection` marching curve; this task
  asks whether it also covers a cone's plane-cut curve (it is a different code path — see below).
- **ADR-046** phasing: B2b-2 is deliberately the *last* increment, "years of specialist effort" in the
  general case — the reason to plan this one carefully rather than dive in.
- Acceptance criteria carried over from issue #283 (per pair): valid closed solids, volumes within
  REQ-101 of a numerical reference, named refusals for degenerate/tangent cases, `.gs` round-trip,
  survey-magnitude stability, one undo step.

## What issue #283 actually describes, and what I found instead

The issue's text: *"A cone intersected by a plane at certain angles produces a parabola or hyperbola —
an open (non-closed-loop) intersection curve, unlike every closed-quartic-loop case B2b-2 has handled
so far... likely the largest remaining item."*

Two things are worth separating before planning further:

1. **This is `Slice`, not `Boolean`.** Every B2b-2 item before this one (sphere∩cylinder, every
   branch-pipe variant) was a **Boolean** between two solids, using the marching `CurveKind::Intersection`
   between two *surfaces*. A cone cut by an oblique **plane** is the same family of problem `Slice`
   already solves for a cylinder (`SliceCylinderOblique`, REQ-314 B2b-1) — nobody has extended that to
   a cone. There is currently **no recogniser at all** for an oblique cone cut; today it falls straight
   through to `Problem::SliceCurvedFace` regardless of angle, including the plain **ellipse** case,
   which is the easy one `SliceCylinderOblique` already models for the cylinder. So the real gap is
   "oblique cone slicing", of which ellipse / parabola / hyperbola are the three angle regimes — not a
   parabola/hyperbola-only problem.

2. **The kernel only ever holds a *finite* cone** (a frustum: `Recipe::radius`/`radius2`/`height`,
   two flat caps — see `MakeCone`, `SliceCurvedPrimitive`'s perpendicular-cut case). A plane that would
   produce an *unbounded* parabola or hyperbola on an infinite mathematical cone gets cut off by the
   frustum's own rim/cap before it ever reaches infinity. So the face boundary the kernel actually has
   to build is **still a closed loop** — cut-curve arc plus a piece of rim and/or cap, exactly the shape
   `SliceCylinderOblique` and the B2b-2 stub builders (`BuildBranchPipeThinStub`'s rim-vs-curve pattern,
   `IsectStrip::oneSided`) already use. **My working conclusion: this does not need new open-loop
   `Face`/`Loop` machinery.** It needs a new closed-form curve parametrisation and a face-integration
   path for it — smaller than "new kernel topology," but I want that conclusion checked (Q1 below)
   before treating it as settled.

## The geometry, worked through

Cone local frame (`Recipe`/`Surface::frame`): axis `+Z`, base radius `r0` at `z=0`, top radius `r1` at
`z=h`. Radius is **affine** in height: `r(z) = r0 + k·z`, `k = (r1 − r0) / h`. A point on the cone at
azimuth `u`, height `z`: `(r(z)·cos u, r(z)·sin u, z)`.

Plane, in the cone's local frame: normal `(nx, ny, nz)`, passing through local point giving constant
`C` such that the plane is `nx·x + ny·y + nz·z = C`. Substituting the cone parametrisation:

```
r(z)·(nx·cos u + ny·sin u) + nz·z = C
```

Write `A(u) = nx·cos u + ny·sin u` (amplitude `amp = sqrt(nx²+ny²)`). Since `r(z) = r0 + k·z`, this is
**linear in `z`** — solvable directly, no Newton/bisection:

```
z(u) = (C − r0·A(u)) / (nz + k·A(u))
```

The denominator `nz + k·A(u)` is where the three conic regimes come from:

- **`|nz/k| > amp`** (whenever `k = 0`, i.e. a true cylinder, this is `k=0` and the denominator is just
  `nz` — never zero for an oblique cut — matching the existing cylinder case): the denominator never
  vanishes for real `u`. `z(u)` is bounded and periodic over the full `u ∈ [0, 2π)` — the plane crosses
  every generator of the (possibly truncated) cone once — this is the **ellipse** case, same topology
  as `SliceCylinderOblique`: a closed loop that wraps the full circumference, no rim arc needed *if it
  clears both caps*.
- **`|nz/k| = amp`**: the denominator has one double real zero — the plane is parallel to exactly one
  generator line. **Parabola.**
- **`|nz/k| < amp`**: the denominator has two distinct real zeros in `u` over one period — the plane
  crosses to the cone's other nappe there. **Hyperbola** (only one branch is ever physically present,
  since the kernel's cone is a single nappe/frustum). Between the two zeros, `z(u) → ±∞` on the
  mathematical infinite cone; on the **finite frustum**, the curve instead runs off the top or bottom
  cap (or off the lateral rim, if the cut also clips a cap) well before reaching either zero — the
  finite piece of curve the face actually needs is bounded by wherever `z(u)` first leaves `[0, h]`,
  found by solving `z(u) = 0` / `z(u) = h` (still closed-form quadratics, or found by a bounded search
  the same way `IsectStrip`/`SphereIsectStrip` already bisect for the marching-curve cases — reuse that
  scan-and-bisect pattern here even though `z(u)` itself is exact, since locating **where** the valid
  `u`-interval starts and ends is the same kind of root-search).

So: **curve evaluation is exact (closed form) in all three cases; only the *domain* of validity (does it
wrap fully, or where does it exit through the frustum) needs a bounded numeric search**, and only for
the parabola/hyperbola case.

## Proposed approach

1. **No new `CurveKind`.** Represent the cone-plane cut curve as a **new closed-form curve type**
   (`CurveKind::ConicCut` or similar) carrying the plane (point + normal, in the cone surface's own
   frame — mirrors how `Intersection` carries its two surfaces) plus the cone's own `r0, r1, h` (already
   on `Surface`). `EdgePointAt`/tessellation evaluates `z(u)` directly (no marching, no Newton) — cheaper
   and exact, unlike the two-surface `Intersection` curve.
2. **Topology, per regime:**
   - **Ellipse regime, clears both caps**: same shape as `SliceCylinderOblique` — two pieces, each a
     cone frustum with one flat rim and one `ConicCut` rim. Reuse that function's structure almost
     verbatim, generalising `AddEllipse`'s closed-form circle-plane-cut edge to whatever the new curve
     type needs (the *3D* curve is a genuine planar ellipse here too — worth checking whether it can
     still be stored as `CurveKind::Ellipse` with a cone-specific derivation of centre/axes, rather than
     inventing a new kind for this branch specifically; see Q2).
   - **Ellipse regime, clips a cap**: refuse by name (`Problem::SliceResultComplex`), matching
     `SliceCylinderOblique`'s existing "the ellipse would clip a cap" refusal.
   - **Parabola / hyperbola regime**: the cut curve is a **partial arc**, closed into a loop by rim
     and/or cap-edge pieces — reuse the `IsectStrip::oneSided` / stub-builder pattern (curve edge +
     line seam(s) + rim arc) rather than inventing new `Face`/`Loop` shapes.
   - **Tangent (parabola) exactly on a rim/seam boundary**, or a cut that misses the solid, or degenerates
     to a single point: refused by name, never guessed.
3. **Face integration.** `ConicalFaceIntegrals`/`CylinderPlaneCutIntegrals` are closed form because
   their `zHi(u) − zLo(u)` is a **trig polynomial** (`d0 + d1 cos u + d2 sin u`). Here `z(u)` is a
   **rational function** of `cos u, sin u` (linear over linear) — no equally clean closed antiderivative
   is worth deriving by hand. Recommend the same path B2b-2 already took for the marching curve:
   numerical quadrature over `u` (`GradedGaussIntegrate`, the existing helper), evaluating `z(u)` exactly
   at each sample (cheap — no bisection needed for the value itself, only for the domain edges in the
   parabola/hyperbola case). This is a **second, small ADR-045(b) extension**: closed form stays the
   rule; a second named exception (an analytic curve whose *face integral* has no tractable closed
   form) joins the first (a curve with no closed form *at all*). Flag for the user (Q3).
4. **Recognisers**: `SliceConeOblique`, parallel to `SliceCylinderOblique`, tried after it in `Slice`'s
   curved-face branch. No Boolean-side change — issue #283's cone item is Slice-only; nothing here
   touches `TryBooleanCurved`.
5. **`.gs`**: a new edge kind (or reused `Ellipse` with a note — see Q2) needs read/write and a
   `kGsFormatVersion` bump, same as every prior B2b-2 slice.

## Decisions for the user

### Q1 — Confirm the scope reframing: is "oblique cone slice" (ellipse + parabola + hyperbola, all three) the right unit of work, rather than parabola/hyperbola alone?

The issue text asked for parabola/hyperbola specifically, but the ellipse case is equally unbuilt today
and is the *easier* half of the same recogniser (no rim-arc closing needed) — splitting them would mean
throwing away the ellipse's simpler validation as a stepping stone. Recommend building and testing all
three together, since they share one recogniser and one face-integration path.

### Q2 — New `CurveKind::ConicCut`, or reuse `CurveKind::Ellipse` for the ellipse regime and something new only for parabola/hyperbola?

The ellipse regime's *3D* curve is a genuine planar ellipse (a cone cut by a plane that doesn't reach a
parallel-to-generator angle is classically an ellipse) — it might already fit `CurveKind::Ellipse`
exactly, with only its centre/axis-length derivation being cone-specific, needing no format or
tessellation change at all for that branch. Parabola/hyperbola are not ellipses and need something new
regardless. Recommend: derive the ellipse regime's centre/semi-axes in closed form and reuse
`CurveKind::Ellipse` there (zero new format cost for the easy case); add one new curve kind for the
parabola/hyperbola regime only. Splits the `.gs` bump to the smaller regime.

### Q3 — Approve the second ADR-045(b) extension (numeric face integration for an *analytic* curve whose integral has no tractable closed form)?

Precedent already exists for "no closed form at all" (the marching `Intersection` curve). This is a
narrower case — the curve itself is exact and cheap to evaluate, only its face's area/volume integral
needs quadrature. Recommend yes, same tolerance discipline (`1e-5 · scale³` residual, well inside
REQ-101's `±0.01 ft`).

### Q4 — Priority: is this worth doing now, given ADR-046 explicitly phases B2b-2's hardest cases last, and issue #283 is otherwise fully resolved by PR #289?

Nothing else in the codebase depends on cone slicing; it is a completeness item, not a blocker. Worth
a plain go/no-go before a multi-day implementation.

## Files / subsystems affected

- `src/util/brep.hpp` / `.cpp`: new `CurveKind` value (Q2-dependent), `Surface`/`Edge` fields if needed,
  `SliceConeOblique`, a `ConeCutFaceIntegrals`/numeric-integration path, `.gs` read/write, `Validate`
  branch, tessellation branch, `Translate`/`PlaceInFrame` mapping.
- `tests/BrepTests.cpp`: new `[req314]` cases — ellipse (full wrap, clips-a-cap refusal), parabola
  (tangent-to-a-generator, both symmetric orientations), hyperbola (both rim-exit combinations: exits
  through the base, the top, or both), tangent/degenerate refusals, survey-magnitude stability,
  `.gs` round-trip.
- No UI/command changes expected — `Slice` already routes through the shared curved-face branch; no new
  command surface.

## Test approach

Mirror `SliceCylinderOblique`'s test shape (`BrepTests.cpp`, the `[req314]` oblique-cylinder-slice
cases): build a cone via `MakeCone`, slice it at chosen angles picked to land in each regime (compute
the boundary angles from `atan(k)` relative to the axis — the half-angle where ellipse becomes parabola,
and beyond it hyperbola), assert `Validate == Ok`, volumes against a numerical (fine Riemann-sum)
reference within REQ-101, and the by-name refusals for clipped-cap / tangent / miss cases.

## Architectural-boundary check

- Stays inside `Slice`'s existing curved-face dispatch (`SliceCurvedPrimitive` → `SliceCylinderOblique`
  → this). No new command, no new primitive kind, no Boolean-kernel change.
- Reuses `GradedGaussIntegrate` (already shared infrastructure from B2b-2) rather than adding a second
  quadrature scheme.
- Whether it needs a new `CurveKind` at all is exactly Q2 — flagged rather than assumed.

## Verification

`build-project`, `testing` (full `BrepTests` + `.gs` migration), `code-review`, `architecture-review`
(the two ADR-045(b) questions above are the load-bearing architectural decisions this task makes).
