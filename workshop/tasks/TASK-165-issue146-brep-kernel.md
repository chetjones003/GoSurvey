# TASK-165 — B-rep solid kernel and the seven primitives (GitHub issue #146, increment 1 of 2)

## Requirement authority

- **REQ-313** — The B-rep solid kernel and the seven primitive solids (accepted 2026-09-01).
- **ADR-045** — The B-rep solid kernel: analytic faces, a remembered recipe, derived tessellation.
- **D-2026-09-01-b** — the recorded decision behind both.
- Constraints in force: REQ-101 (±0.01 ft), REQ-201 (no silent failures), REQ-300 (dependency
  discipline), REQ-301 (minimal abstraction), REQ-311 (one plane/frame type).
- GitHub issue #146, Phase 3 of #120.

## The SPEC GAP, and how it was closed

Issue #146 could not be implemented as filed. There was **no accepted requirement anywhere in
`spec/requirements.md` for solids** — no REQ mentioned solids, B-rep, or the seven primitives — and
the issue says so itself: *"a new kernel is an architectural decision — it needs an ADR and an
accepted REQ before implementation, not a Workshop-layer choice."* The issue also left DXF/DWG
export of solids explicitly undecided.

Per CLAUDE.md §5 the work stopped there and three questions went to the user, each explained in
plain English before being asked, because each binds the whole of #120's Phases 4–6 and one of them
changes a customer-facing data format:

1. **How a solid is stored.** Chosen: real topology plus a remembered recipe (ADR-045 (a)–(c)).
   Rejected: recipe-only primitives (nowhere for a Phase 4 boolean result to go, plus a migration of
   every already-saved solid); faceted B-rep (fails REQ-101 on a sphere, bloats files, and bakes the
   display quality into the model, which #120 forbids).
2. **DXF/DWG export.** Chosen: exclude with an explicit message (ADR-045 (i)), the `CadMesh`
   precedent from ADR-026 (c). Rejected: a lossy tessellated approximation.
3. **Delivery.** Chosen: two increments — kernel first, then document integration.

All three answers were the recommended option. Recorded as D-2026-09-01-b; REQ-313 and ADR-045
written before any code.

### One deviation from the approved split, and why

The approved label for increment 1 included the seven commands. They moved to increment 2, together
with `.gs` persistence and the DXF/DWG exclusion message. Reason: a `BOX` command on `beta` before
persistence exists lets a user build a solid and lose it on save with no message, which is exactly
the silent failure REQ-201 forbids — and moving them makes increment 1 change **no existing source
file at all**, so it cannot regress anything. Recorded here rather than done quietly.

## Files affected

New:
- `src/util/brep.hpp` — topology, analytic surfaces and curves, the recipe, the seven builders,
  validity, mass properties, bounds, tessellation.
- `src/util/brep.cpp` — the implementation.
- `tests/BrepTests.cpp` — the Catch2 suite.

Modified — **only** these, and only as source-list entries:
- `CMakeLists.txt` — `src/util/brep.cpp` into `GOSURVEY_DOMAIN_SOURCES`; `tests/BrepTests.cpp` and
  `src/util/brep.cpp` into the `GoSurveyTests` target.

SPEC:
- `spec/requirements.md` — REQ-313 and its traceability row.
- `spec/architecture.md` — ADR-045, and `brep` in the `util/` module listing.
- `spec/project.md` — D-2026-09-01-b.

## Implementation approach

Built in the canonical local frame and placed once (`PlaceInFrame`), so each builder reads as its
topology rather than as six dot products per vertex.

**Seaming.** Curved surfaces are cut into faces that each bound normally — cylinder and cone side
into two halves at longitude 0 and π, sphere into two halves by a meridian, torus into four patches
at t ∈ {0, π} and v ∈ {0, π}. This keeps *"every edge is used exactly twice, once in each
direction"* a plain invariant instead of a special case with a self-referencing seam edge, and that
invariant is what `Validate` leans on hardest.

**Mass properties.** Volume is the divergence theorem in its rotation-invariant form,
`V = (1/3) ∮ (p − q)·n dA`, evaluated per face in that face's own frame with `q` transformed into
it. Closed forms were derived for each surface kind and each checked against the textbook volume by
hand before coding: plane (constant integrand, so `d × area`), conical (the cylinder is the
`r0 == r1` case, so one derivation serves both kinds), spherical and toroidal (both general in
latitude / tube angle rather than special-cased to the full range, which removes a trap for Phase 4).
Plane-face area is a shoelace over the loop's endpoints plus a signed circular-segment correction
per arc — which is also what makes a full-circle edge come out as `πr²` for free.

**Numerical stability at survey magnitudes** is the choice of `q`: the mean of the solid's vertices,
a point on the solid. Every integrand then stays at model scale even at easting 3.5e6, so no term is
a difference of two large nearly-equal numbers. This is the whole of the answer to #120's *"large
survey coordinates"* section, and it is asserted directly.

**Tessellation** is derived, `double`, per-face (unwelded, because a solid's edges are creases), with
true analytic normals inside each curved face and segment counts from a chord-tolerance sagitta.

## Test approach

`tests/BrepTests.cpp`, tag `[brep][req313]` — 19 cases, 313,221 assertions, no window and no
document. Every figure is asserted against the **closed form**, never against a recorded output,
because a volume that is 3% wrong looks exactly like one that is right.

Coverage: seven primitives (V/E/F counts, Euler characteristic including 0 for the torus, closed-form
volume and area to 1e-12); placement and rotation invariance; state-plane magnitudes for box, tilted
sphere and torus; every construction refusal by name; six deliberately-broken topologies plus the
geometric-closure case; edge parametrisation endpoints on tilted frames; and an independent
tessellation cross-check (volume and area re-derived from triangles alone, winding vs. analytic
normal, finer tolerance ⇒ more triangles and less error, bounds contain the mesh).

## Verification

- Build: clean, `/W4` warnings-as-errors config, no new warnings.
- `ctest`: **955/955 green** (19 new cases; no pre-existing test changed).
- Architecture: the kernel links no GL, ImGui, document or command TU; it is in `util/` beside
  `ray3d`/`ucs`/`tinbuild` and reachable from the test target, satisfying ADR-002 and REQ-313's
  first acceptance bullet by construction.
- REQ-301 / CLAUDE.md rule 2: no speculative abstraction. `Shell` exists because the requirement
  names it; `Recipe` because the Properties panel and parametric regeneration are its two uses; the
  cylinder shares the cone's integral rather than carrying a second derivation that could disagree.

### Negative tests

Assertions were proved load-bearing rather than assumed:

1. **A flipped sign on a reference-point term in the conical integral left the whole suite green.**
   Investigated rather than shrugged off, and it is a theorem, not a hole: on a closed surface the
   `q` terms collectively equal `−(1/3) q · ∮n dA`, and that integral vanishes — so no sign error in
   them can change a valid solid's volume.
2. That finding exposed a **real gap in `Validate`**: it had no *geometric* closure check at all. A
   curved face whose parametric span disagreed with its own boundary loop was manifold, orientable,
   ring-closed — and a hole. Added: the volume integrated about two well-separated reference points
   must agree. It is exactly what a Phase 4 trim can produce, and nothing else in the check can see
   it.
3. **Deleting the reference-point terms turned four cases red**, including the new span case (which
   returned `Ok` instead of `NotClosed`). That is the proof the terms are load-bearing and the
   closure probe is not vacuous.
4. A test whose premise was wrong was corrected rather than loosened: flipping one edge use to prove
   `EdgeOrientationInconsistent` also breaks ring closure, so `LoopNotClosed` fired first. Reversing
   a whole face's loop is the case that is closed but not orientable, and that is what it now does.

## Assumptions

- **ASSUMPTION-1 (validated):** analytic per-face integrals agree with an independent triangle-based
  computation. Every primitive's volume and area are cross-checked against the tessellation, which
  would not agree if either the analytic formula or the triangulation were wrong.
- **ASSUMPTION-2 (stated, not validated here):** a chord tolerance is the right knob for
  tessellation quality. Nothing consumes it yet; increment 2's cache and the REQ-100 budget are what
  will actually test it.

## Technical debt / stated boundaries

Each is recorded in REQ-313 and ADR-045 rather than left silent:

- **DEBT-1** — no after-the-fact self-intersection test. Refused at construction for the seven
  primitives (a torus tube swallowing its axis is the only route); the general test belongs with
  Phase 4 booleans, the first operation that can produce one.
- **DEBT-2** — no centroid or moments of inertia. #120 puts them in Phase 6.
- **DEBT-3** — plane faces triangulate as a centroid fan: correct for the convex, hole-free faces
  every primitive makes, and **refused by name** (`PlaneFaceNotSimple`) for anything else rather
  than silently wrong. General polygon triangulation is Phase 4's problem.
- **DEBT-4** — increment 2 carries the rest of issue #146: `CadSolid` entity and store, seven
  commands with exact typed dimensions, `.gs` persistence, REQ-064 render path, cached tessellation
  against REQ-100, face and edge snapping, DXF/DWG exclusion message. **Issue #146 stays open.**

## Status

Increment 1 complete and verified. Increment 2 not started. Per the project convention, this goes to
review — it is not marked done here.
