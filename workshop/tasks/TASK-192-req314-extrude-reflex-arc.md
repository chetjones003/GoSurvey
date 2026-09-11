# TASK-192 — Extrude builds a reflex profile arc (REQ-314 amended, issue #147)

## Requirement authority

- **REQ-314** increment 1, acceptance amended — a profile arc may curve into its loop.
- **ADR-046 (d)**, amended — the note that recorded this as "a separate feature, now unblocked".
- **D-2026-09-03-e** — the recorded decision.
- Uses **`Surface::inward`** (REQ-314 B2a, D-2026-09-02-c). Adds nothing to it.
- REQ-201, ADR-045 (e) (the geometric closure probe, which is what proves this correct).

## Why now

ADR-046 (d) already said it. When B2a gave `Surface` an `inward` flag for the wall of a Boolean bore,
it recorded in the same breath that `ProfileArcReflex` "stays for now; a reflex profile arc in an
extrude is a separate feature, now unblocked." This is that feature, and it turns out to need nothing
that was not already built.

The refusal was costing ordinary shapes. An annular sector, a bay bitten out of a rectangle, and the
plan outline of any wall that bends are all normal things to extrude, and every one of them was
refused by name.

## What changed

Two lines in the side-face loop, and the deletion of the refusal.

After the builder's existing walk the loop runs CCW about the extrusion direction. So an arc whose
sweep is **still positive** has its centre on the interior side and sweeps an ordinary outward
cylinder — unchanged. A **negative** one has its centre outside the loop and sweeps a face whose
material is on the far side from its own axis, which is precisely the face B2a defined:

```cpp
f.surface.inward = pe.sweep < 0.0;
f.uStart = std::min(u0, u1);
f.uEnd   = std::max(u0, u1);
```

The span is stored **increasing** with the orientation carried by `inward`, which is the convention
the bore walls already set. A negative `uEnd - uStart` would instead make the face's own **area**
come out negative, and an area is a magnitude.

Nothing else moved: the normal evaluators, the tessellator's winding and the volume integrand were
all taught about `inward` by B2a. This is simply the second caller.

## Test approach

Three cases, chosen so a mishandled orientation cannot hide:

- **A quarter annulus.** Its volume and surface area can both be written down —
  `(π/4)(11² − 9²)·5` and the two walls plus two ends plus two caps — and they fail in *different*
  ways if the inner face is wrong, the volume by twice the void and the area not at all. It also
  asserts that **exactly one** of the two cylinder faces is inward: setting the flag on both would
  still leave a positive volume, so that is checked directly rather than inferred.
- **The same solid described three ways** — the profile reversed, and the extrusion downward. The
  builder normalises the walk, and that normalisation is exactly where a reflex arc's sign could
  quietly be read against the wrong direction.
- **A bay bitten out of a rectangle** — REQ-314's own former refusal case, kept rather than replaced,
  because its arc reaches most of the way across the shape where an annulus wall is a thin sliver.
- Plus a tilted frame at state-plane magnitude, and the tessellation cross-check (winding against
  analytic normals, mesh volume and area against the closed forms).

The obsolete `REQUIRE_FALSE` section in "Extrude refuses bad input by name" is deleted rather than
weakened. `Problem::ProfileArcReflex` STAYS in the enum and in `ProblemText`: LOFT and SWEEP (REQ-315, which landed after this branch was cut) still raise it, so only `Extrude` stops.

## Verification

- `ctest`: **1030/1030 green.**
- **Negative-tested both halves, and both turn three cases red in the same way** — which is itself
  the interesting result. Leaving `inward` false, or leaving the span decreasing, does not produce a
  subtly wrong volume: `Validate`'s geometric closure probe rejects the solid outright and `Extrude`
  returns `false`. The probe ADR-045 (e) added is what makes this change provable rather than
  plausible, and it earned its place again here.

## Assumptions

- **ASSUMPTION-1 (stated):** a sweep of exactly zero is already refused upstream as a degenerate edge,
  so `pe.sweep < 0.0` is a total test and needs no epsilon. Checked: the degenerate-sweep guard runs
  before the walk.

## Technical debt / stated boundaries

- **`Revolve` still refuses an arc in its profile** (`RevolveArcInProfile`). That is a different
  problem, not this one deferred: a revolved arc sweeps a portion of a sphere or a torus, not a
  cylinder, so nothing here applies to it.
- **SLICE and the B1 Booleans still refuse curved faces.** Their own increments' boundaries, and a
  reflex extrude now produces exactly the kind of solid they will need to accept — which is a reason
  to expect this to be exercised by those increments rather than a reason to widen them here.
- **The chord-based self-intersection screen is unchanged**, so a profile that only overlaps itself
  through its arc bulges still reaches `Validate` rather than getting a named refusal. Pre-existing,
  and stated in the screen's own comment; a reflex arc makes it slightly easier to reach.

## Status

Complete and verified. Goes to review, not done; the issue is not closed here.

## Rebase note (2026-09-03)

Rebased onto `beta` after REQ-315 (loft, then sweep) landed. The text merged with one conflict in
`tests/BrepTests.cpp` — both sides had appended a test section at the same place — but the **build
then failed**, which is the part worth recording:

```
brep.cpp(2264): error C2838: 'ProfileArcReflex': illegal qualified name in member declaration
```

This branch retired `Problem::ProfileArcReflex` outright, which was correct when `Extrude` was its
only user. While the branch sat, `Loft` and `Sweep` were written **against it** — `brep.cpp:2258`
refuses a reflex arc in a loft/sweep profile, and `BrepTests` asserts that refusal. Retiring the
enumerator deleted something two newer operations depend on.

So the scope is narrower than first written: **`Extrude` stops raising it; the enumerator and its
`ProblemText` entry stay** for LOFT and SWEEP, neither of which has been taught to carry an inward
wall through its own surface construction. The requirement is unchanged — REQ-314's amendment was
always about `Extrude` — but the spec wording ("retired") was not, and has been corrected in
`spec/architecture.md`, `spec/project.md` and above.

The general lesson is the one already in the decision log for the REQ-312 work: a clean auto-merge
says the *text* did not collide, not that the *meaning* survived. Here the compiler caught it; a
change that removed a *behaviour* rather than a *symbol* would have merged clean and built clean.
