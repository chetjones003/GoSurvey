# TASK-218 — edge chains and the spherical corner patch (REQ-323 increment 2)

## Requirement authority

REQ-323 increment 2 (D-2026-09-08-a, ADR-046 amendment (j) extended). **Closes GitHub issue #148
acceptance 5 for the fillet** — *"fillet and chamfer work on single edges and on edge chains"* — for
everything but the chamfer, which is a separate requirement.

Stacked on TASK-217 (the FILLET command), which is still in review as PR #414.

## What was built

| file | change |
|---|---|
| `src/util/brep.{hpp,cpp}` | `FilletEdgesGeneral` — the one-pass builder; `FilletEdge` and `FilletEdges` reduced to one call each into it; `FilletCornerPartial` and `FilletCornerNotOrthogonal`; `FilletEdgesShareVertex` retired |
| `tests/FilletEdgeTests.cpp` | 4 new cases; increment 1's shared-vertex case re-pointed at the narrower refusal |
| `tests/headless/transcripts/req323-fillet-solid.txt` | a three-edge corner through the command |
| `spec/requirements.md`, `spec/architecture.md`, `spec/project.md` | acceptance, ADR (j) part 4, D-2026-09-08-a |

## Why the sequential form had to go

Increment 1 applied fillets one after another. That cannot reach a corner: **after the first fillet
the shared vertex no longer exists**, so the second edge arrives at a *cylinder* where its
construction needs a plane. No amount of iterating gets past it.

So `FilletEdgesGeneral` does every requested edge in one pass over the original solid, and both
public entry points became one line each. Increment 1's position-based edge re-lookup (TASK-210
DEBT-1) went with the sequential form — it existed only to survive the compaction between passes.

The whole of increment 1's test suite then ran through the new builder unchanged, which is the
regression check that mattered: **20 of its 21 cases passed on the first run**, and the twenty-first
was the intended change (below).

## The geometry, and why it stays closed-form

Three facts, each doing real work:

1. The corner ball touches all three faces, so its centre is at distance `r` from all three planes —
   one 3×3 solve, the same shape a single edge uses with two planes and the edge direction.
2. **Each cylinder axis passes through that centre.** An axis is the locus of points at distance `r`
   from its own two planes; the centre is at distance `r` from all three. So the fillets *terminate*
   on the corner rather than approaching it, and one rule builds both kinds of end — an arc of
   radius `r` about the axis point, which at an open end is the edge's own offset and at a corner is
   the ball's centre. Nothing else differs between them.
3. A cylinder of radius `r` whose axis passes through the centre meets a sphere of radius `r` centred
   there **exactly on the great circle** perpendicular to that axis. So every fillet-to-patch
   boundary is a great-circle arc, and the patch is a spherical triangle whose corners are the same
   tangent points the fillets already land on.

With mutually perpendicular faces that triangle is an **octant**, which in the frame `(n1, n2, n3)`
is exactly the parameter rectangle `u, v ∈ [0, π/2]`. That is the whole reason increment 2 asks for
orthogonality: a general spherical triangle is bounded by three great circles that are *not*
iso-parameter lines, so it would need REQ-321's `paramLoops` — and with them a numerically
integrated area in place of the closed form. Refused by name rather than approximated.

## A refusal narrowed, which is a user-visible change

`FilletEdgesShareVertex` said only *"these two share a vertex"*. `FilletCornerPartial` says the thing
that is actually unbuildable: **some** of the edges at that corner are selected and some are not, so
the ball would have to roll off a rounded edge onto one staying sharp — a setback blend, a different
construction. The old value was **retired**, not left dead, and its message replaced.

## Test approach

The acceptance was again written as arithmetic before the code — the discipline that caught two
wrong numbers in increment 1. The rounded box's closed forms, each term separately checkable:

```
V = a·b·c + 2r(ab+ac+bc) + πr²(a+b+c) + (4/3)πr³      a = L−2r, b = W−2r, c = H−2r
A = 2(ab+ac+bc) + 2πr(a+b+c) + 4πr²
```

For 20 × 10 × 8 at `r = 2`: `1120 + 344π/3` and `368 + 120π`, topology 24/48/26 (6 planes + 12
cylinders + 8 octants), Euler unchanged. **This time the numbers were right and the implementation
matched to 1e-12 on the first run.**

Also: the three-edge corner leaving exactly one patch, with its three corners asserted at the ball
centre plus `r` along each face normal; a partial corner refused; an oblique (wedge) corner refused;
and the octant asserted to be an iso-rectangle, because that property is the reason the area is
exact and it would be lost silently.

## Two things worth recording

1. **A splice ate a `case` label.** Replacing the retired `ProblemText` entry removed
   `case Problem::FilletResultInvalid:` along with it, so that value fell through to the generic
   "The solid is not valid." — the exact class of gap the 2026-09-06 external review had already
   found once in this file. **The reviewer's own test caught it**, which is the argument for that
   test existing.
2. **A test name with an em-dash cannot be re-invoked by ctest** on this console encoding: the test
   itself passed, `ctest` reported it failed, and the log said "No test cases matched". Renamed to
   ASCII. Worth knowing before someone spends time on a phantom failure.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean tree.
- **testing** — PASS. `ctest` **1309/1309**.
- **architecture-review** — PASS. The builder is a whole-solid rebuild, which ADR-046 amendment (j)
  now records as the shape every later blend will need.
- **code-review** — self-run. It found the deleted `case` label above.
- **performance-review** — PASS. One pass, one solid copy, plus a map lookup per new vertex.

## Not covered by test, stated plainly

- **Different radii on edges meeting at one corner.** The signature takes a single radius, so the
  case cannot arise yet; when it can, the patch is no longer a sphere.
- **A corner of more than three edges** — refused as `FilletVertexNotSimple`, untested against a
  real primitive because none of the seven produces one.

## Technical debt

- **DEBT-1 — the non-orthogonal corner** is the next increment and the one that needs `paramLoops`.
  It is also where the closed-form acceptance stops being available, so its test will have to be
  built differently.
- **DEBT-2 — inherited:** the concave edge and the oblique end face, each refused by name.
- **DEBT-3 — CHAMFER has no requirement yet.** It shares this edge selection and this refusal shape
  but none of the surface geometry, and #148 acceptance 5 names it alongside the fillet.
