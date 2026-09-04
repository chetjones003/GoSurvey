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
contain it.

**PROGRESS 2026-09-03 — PR A2 done.** Numerical face integration: `IsectStrip` / `MakeIsectStrip` /
`IsectStripAt` scan+bisect the axial extent of an Intersection-bounded cylinder face at a longitude;
`IntegrateCylinderFaceNumeric` integrates it with a graded 8-node Gauss rule (`GradedGaussIntegrate`
— cosine-clustered panels so a lens end that pinches to zero width still integrates). `IntegrateFace`
routes Intersection-bounded cylinder faces there; the tessellator uses the same strip. `Validate`'s
closed-surface residual relaxes to `1e-5·scale³` for solids with an Intersection edge.
`BuildBranchPipeIntersection` (8 vertices / 10 edges / 4 faces) + `TryBooleanBranchPipe` (perpendicular
intersecting axes, `r ≠ R`, the thin cylinder clear of the thick one's caps) → `INTERSECT` builds the
lens. `.gs`: an Intersection edge writes its two surfaces + witness (`BrepSurfaceToJson` /
`BrepSurfaceFromJson`); `kGsFormatVersion` 2 → 3 with a no-op `MigrateV2ToV3`. Tested: the lens
volume matches a fine numerical reference to 2e-4, on-axis and on a tilted survey-magnitude frame and
after `Translate`; the reversed operand order; `SUBTRACT` / `UNION` still refused. **Follow-up:** a
headless-transcript `.gs` round-trip is deferred — building a perpendicular cylinder pair from
commands needs a `CYLINDER` axis option (the base point stays in world coords under a `UCS`).

**PROGRESS 2026-09-03 — PR B done.** `SUBTRACT` (`main − branch`): `BuildBranchPipeSubtract` — a
**genus-1** solid, 8 vertices / 12 edges / 6 faces. The two intersection loops become the **inner
loop** of `B`'s two wall half-faces (split at the `ψ = ±π/2` seams); `B`'s two flat caps; `A`'s wall
inside `B`, inward, in two halves. `IntegrateFace` gained a branch: a cylinder face whose
`Intersection` edge is only in an *inner* loop is the full band **minus** the bite
(`IntegrateCylinderFaceNumeric` on the inner-loop strip). `SameSurfaceApprox` now ignores an
along-axis frame-origin offset (so a stored surface at the crossing matches the face's surface at a
cap). The tessellator draws a holed band as a lower + upper sub-strip. `TryBooleanBranchPipe` routes
`SUBTRACT` when the minuend is the thick cylinder; `thin − thick` (two stubs) and `UNION` fall
through. Volume `vol(main) − lens`, to 5e-4 against a fine numerical reference; genus-1 topology,
tilted survey frame, `Translate` all covered.

**PROGRESS 2026-09-03 — PR C done. The branch-pipe trilogy is complete.** `UNION` (`main ∪ branch`):
`BuildBranchPipeUnion` — the solid pipe-tee, **genus 0**, 12 vertices / 18 edges / 10 faces. `B`'s
wall outside `A` (two halves, each with the branch mouth as an inner loop — identical to `SUBTRACT`'s
`B` walls but outward) + `B`'s two caps + `A`'s wall outside `B` (four stub halves) + `A`'s two caps.
`IsectStrip` gained a `oneSided` mode: a face with **one** `Intersection` edge and a flat rim on the
far side (a stub band) integrates from the curve to `rimZ`. The `-X` branch cap's rim arcs run about
`−X` so its outward loop winds opposite the `+X` cap's with the tally-forced edge orientation. Volume
`vol(branch) + vol(main) − lens`, to 5e-4; operand order does not matter.

**B2b-2's first operand pair (a thin cylinder crossing a thicker one at right angles) is complete for
all three operations.** Remaining B2b-2 sub-slices: sphere∩cylinder, skew / non-perpendicular
cylinder axes, non-elliptical cone sections.

**PROGRESS 2026-09-03 — issue #242 opened to track the remaining sub-slices; sphere∩cylinder started
(TASK-195).** Finding: sphere∩cylinder splits — the **centred** case (cylinder axis through the
sphere centre) meets along **two plane circles**, not a quartic, so it is closed-form and needs no
procedural curve; only the **offset / skew** axis is the genuine quartic. User chose the centred
case first (matches the closed-form-then-procedural phasing). **Slice A / INTERSECT done**:
`BuildSphereCylinderIntersection` + `TryBooleanSphereCylinder` in `brep.cpp` — a spherical-ended
barrel, 6v/10e/6f, closed-form volume/area, no `.gs` bump. SUBTRACT/UNION of the centred pair and
the offset quartic are the next slices.

### Deferred within B2b-2 (later sub-slices)

- torus ∩ anything, cone parabola / hyperbola sections.
- The fully-general tilted-AND-skew pair, SUBTRACT/UNION/thin−thick (INTERSECT done, see below).

**PROGRESS 2026-09-04 — fully-general (tilted AND skew) branch pipe INTERSECT done
(feat/issue242-general-branch-pipe-intersect, issue #242).** Key finding: the thin-cylinder-on-thick
crossing quadratic is closed-form for ANY relative axis position — `(Px)²+(Py)²=R²` is quadratic in
the thin-axis parameter `s` for *any* fixed `φ` regardless of tilt/offset, since `Px,Py` are affine
in `s`. All three prior special cases (perpendicular-coplanar, tilted-coplanar `α≠0,g=0`,
perpendicular-skew `α=0,g≠0`) were solving degenerate versions of ONE quadratic; the general solve
is `s(φ) = (r sinα cosφ ± √(R² − (g − r sinφ)²)) / cosα`, `g` the true common-perpendicular gap
(signed, builder doesn't care), reducing to both prior formulas at α=0 or g=0.
`BuildGeneralBranchPipeIntersection`: same 8v/10e/4f χ=2 lens topology; thick-wall mouth patches
have no closed-form angular extent once both α,g≠0, so `uStart/uEnd` come from a one-time 360-sample
scan with margin (the numeric strip search at integration time finds the exact band regardless — it
never needed the closed form, only the WIDTH of the loop it was told to trust it in).
`TryBooleanBranchPipe`: `cThin`/`cThick` (closest points of the two skew lines) hoisted above the
`perp` split since both branches need them; general branch builds `gfr` via `thin's own in-plane
direction` (not the gap direction — that only works when `perp`), conservative (not tight) bounds
`sBound=(r|sinα|+R)/cosα`, `zBound=sBound|sinα|+r cosα` gate "fully crosses"/"caps clear". INTERSECT
only; SUBTRACT/UNION/thin−thick still refused. Volume vs a 1400×1400 section integral (8e-3), tilted
survey frame. Full suite green (1119). **All test assertions passed on the first build** — the
closed-form derivation + numeric u-scan removed the risk that broke the cyl-box pocket slice.

**PROGRESS 2026-09-04 — `cylinder − box` slot done (feat/issue242-cylinder-box-slot, issue #242).**
A slot (two parallel full-length cutting faces) leaves two disjoint "wing" pieces — each is exactly
the single-flat notch shape, so **no new builder**: both wings call `BuildCylinderLongitudinalFlat`
directly, the second in a frame with `xAxis`/`yAxis` both negated (mirrored, still right-handed) so
"kept x ≤ px" reads as the slot's far side. Recogniser: `cutFaces` now a vector (was a single
`cutFace`); `cutCount==2` with antiparallel normals → express face1's threshold in face0's frame
(`qx1Signed = -px1`, since face1's own local xAxis = -n1 = n0 = -lf0.xAxis) → probe the slot's 8
extreme points against every other box face → two `BuildCylinderLongitudinalFlat` calls. A
part-length slot still refused. 4v/6e/4f χ=2 each wing, closed-form. Full suite green (1118).

**PROGRESS 2026-09-04 — `cylinder − box` partial-length pocket done (feat/issue242-cylinder-box-pocket,
issue #242).** `BuildCylinderPocket`: a rectangular bite that reaches neither cap — floor + ceiling
(circular-segment planar faces) + 5 cylinder-wall sub-bands (lower/upper split minor/major at u=±φ to
match the pocket's chord, plus the middle major band) + 2 full-circle caps. 8v/16e/10f, χ=2, all
arcs+lines (closed form, no marching). **Bug found and fixed during this slice**: `CylinderCutZExtent`
only recognises a plane cut via an `Ellipse` edge; a face bounded by plain rim `Arc`s (no ellipse)
always falls through to `ConicalFaceIntegrals(sf.radius,...,sf.height,...)`, which trusts
`sf.frame`/`sf.height` as the face's OWN span — every sub-band needs its OWN frame origin (shifted to
its local z=0) and its own height, not the full-cylinder ones, or every partial band silently
integrates as if it ran the whole cylinder length (→ `Problem::NotClosed`, closed topology but wrong
volume). Worth remembering for any future partial-height cylinder sub-face. Full suite green (1118).
Also fixed a stale doc comment: sphere∩cylinder has **no separate skew-axis case** — a sphere is
rotationally symmetric, so the perpendicular offset `d` is the only invariant of a cylinder's
placement relative to it; the existing `d>r`/`d<r` builders already cover every axis direction
(docs/issue242-sphere-cylinder-skew-not-distinct, PR #276).

**PROGRESS 2026-09-04 — skew branch pipe `thin − thick` done (feat/issue242-skew-branch-pipe-thin-minus-thick, issue #242).**
`BuildSkewBranchPipeThinStub` = `BuildBranchPipeThinStub` via the `SkewBranch` helper, off-centre
inward dimple u ∈ [uLo,uHi] / [−uHi,−uLo]. Two stubs 4v/6e/4f χ=2, total volume `πr²L − lens` (5e-3).
**Perpendicular branch-pipe family now COMPLETE** — coplanar + skew × INTERSECT/SUBTRACT/UNION/thin−thick.
Only the fully-general **tilted-AND-skew** cyl-cyl case is left. Full suite green (1118).

**PROGRESS 2026-09-04 — skew (offset) perpendicular branch pipe SUBTRACT + UNION done (feat/issue242-skew-branch-pipe-sub-union, issue #242).**
Because the offset thin (axis ‖ fr.yAxis, x = g > r) pierces the thick entirely on its +x half,
**both mouths land in u ∈ (−π/2, π/2)** — so the thick wall splits at **ψ = 0 / ψ = π** (not ±π/2),
each half carrying one mouth. `SkewBranch` helper struct (shared frame + `cpt`/`thinPt`);
`BuildSkewBranchPipe{Subtract,Union}` are the coplanar builders with that seam relocation + skew
`aSurf`/curve; UNION stub caps ⊥ fr.yAxis at `yLo/yHi`. `TryBooleanBranchPipe` skew branch dispatches
all three thick-minuend ops; `thin−thick` skew + tilted-skew still refused. SUBTRACT 8v/12e/6f genus1,
UNION 12v/18e/10f; volumes vs a 1400×1400 section integral (5e-3). Full suite green (1118). No `.gs` bump.

**PROGRESS 2026-09-04 — skew (offset) perpendicular branch pipe INTERSECT done (feat/issue242-skew-branch-pipe-intersect, issue #242).**
`BuildSkewBranchPipeIntersection(fr, r, R, g)` = `BuildBranchPipeIntersection` verbatim except
`aSurf.frame` (thin axis = `fr.yAxis`, through `(g,0,0)`), the curve `s(φ) = ±√(R²−(g+r sinφ)²)`,
and the two thick-wall patches now OFF-CENTRE — `u ∈ [uLo, uHi]` and its mirror, with
`uLo = atan2(√(R²−(g+r)²), g+r)`, `uHi = atan2(√(R²−(g−r)²), g−r)` (symmetric `±psi0` only when g=0).
`TryBooleanBranchPipe` gained a `gap > eps` skew branch: proper skew-line closest points →
`sfr` (zAxis = thick, xAxis = gap dir); requires perpendicular + INTERSECT + `g+r<R` + thin fully
crosses + thin axis ‖ `±sfr.yAxis` (tilted+skew still refused). 8v/10e/4f χ=2, volume vs a 1400×1400
section integral (5e-3), tilted frame. SUBTRACT/UNION skew refused. Full suite green (1118). No `.gs` bump.

**PROGRESS 2026-09-04 — branch pipe `thin − thick` done (feat/issue242-branch-pipe-thin-minus-thick, issue #242).**
`BuildBranchPipeThinStub(fr, r, R, alpha, zetaFlat, sideSign)` = `BuildCylinderSphereOffsetStub`'s
topology (4v/6e/4f χ=2: thin wall 2 halves + flat cap + inward dimple) with the dimple surface
swapped sphere→thick-cylinder (`bSurf`, `inward`, u ∈ ±psi0 / π±psi0). Handles perpendicular
(`alpha=0`) and tilted alike — one builder, `cpt(φ)` the same closed form. `TryBooleanBranchPipe`'s
`thin − thick` guard now pushes two stubs (`zetaFlat = thin.length − sThin` / `−sThin`). Total
volume `πr²·L − lens` vs the section integral (5e-3). Full suite green (1117). No `.gs` bump.
**Coplanar branch-pipe family now COMPLETE** (perp + tilted × INTERSECT/SUBTRACT/UNION/thin−thick).

**PROGRESS 2026-09-04 — non-perpendicular branch pipe SUBTRACT + UNION done (feat/issue242-branch-pipe-angled-sub-union, issue #242).**
`BuildAngledBranchPipeSubtract` / `BuildAngledBranchPipeUnion` = the perpendicular builders verbatim
except `aSurf.frame` (tilted) + the closed-form `cpt`; UNION additionally re-aims the two thin stub
caps ⊥ the tilted axis (`thinPt(φ,ζ)`, rim arcs about `±t̂`, centres `ζ·t̂`) and takes `zetaA0/zetaA1`
(thin-axis params) instead of `xA0/xA1`. `TryBooleanBranchPipe` non-perp branch now dispatches all
three ops; `thin − thick` (two stubs) still refused. SUBTRACT 8v/12e/6f genus 1, UNION 12v/18e/10f;
volumes `πR²L − lens` and `πR²L + πr²L − lens` vs a 1200×1200 section integral (5e-3). Full suite
green (1117 ctest). No `.gs` bump.

**PROGRESS 2026-09-04 — non-perpendicular branch pipe INTERSECT done (feat/issue242-branch-pipe-angled, issue #242).**
`BuildAngledBranchPipeIntersection` = `BuildBranchPipeIntersection` verbatim except `aSurf.frame`
(thin axis = `cos α·x̂ + sin α·ẑ`) and the witness `cpt(φ,sign)` — the closed-form curve
`ζ(φ) = (r sinα cosφ ± √(R²−r²sin²φ)) / cosα` along the thin axis (derives to `cos²α`·quadratic →
disc factors as `cos²α(R²−r²sin²φ)`). `psi0 = asin(r/R)` unchanged (thin's y-extent is ±r regardless
of α). `TryBooleanBranchPipe` reworked: proper 2-line intersection for `meet` (was a perpendicular-only
projection), coplanar-gap test, `alpha` from the thin axis vs `fr.xAxis` (= thin's own in-plane
component), α-aware fully-crosses / clears-caps margins (reduce to the old `≥R` / `≥r` at α=0).
Non-perp SUBTRACT/UNION and skew axes still refused. 8v/10e/4f χ=2, volume vs a 1200×1200 thin-section
integral (5e-3), tilted frame. Full suite green (1117 ctest). No `.gs` bump.

**PROGRESS 2026-09-03 — `cylinder − box` notch done (feat/issue242-cylinder-box-notch, issue #242).**
`BuildCylinderLongitudinalFlat` + a recogniser in `TryBooleanCylinderThroughPlanar`: when the box
presents exactly one face parallel to the cylinder axis that cuts within the radius, the box is
otherwise square to the axis, and every extreme point of the removed segment lies inside the box,
`cylinder − box` reduces to `cylinder − (that one half-space)` — a full-length planar flat. Result
4v/6e/4f, χ = 2 (two D caps, one rectangular flat, one partial cylinder wall). Closed-form volume
`L·(πr² − (r²·acos(px/r) − px·√(r²−px²)))`. No new curve types (lines + arcs), no `.gs` bump.
Partial-length pocket, two-face slot, and tilted box still refused by name; a full-swallow box
reports `BooleanEmptyResult`; a disjoint box returns the cylinder unchanged. Full suite green (1116).
Still deferred: partial-length pockets, slots, corner clips, tilted `cylinder − box`.

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
