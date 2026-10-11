# TASK-233 — `brep::MoveVertex` and `brep::MoveEdge`: the rest of a 3D grip

## Requirement authority

- **REQ-333** (drafted here — moving a solid's vertex or edge), implemented against per the standing
  arrangement for an unaccepted REQ.
- **ADR-046 amendment (n)** — the architectural decision below.
- **REQ-319 / ADR-046 amendment (i)** — push/pull, whose corner re-solve this generalises.
- **GitHub issue #148 acceptance 3**: *"3D grips move faces, edges and vertices, and the solid stays
  valid after each edit."* Faces have been done since slice 3; this is the other two thirds.
- User instruction, 2026-09-09: fix criteria 3 and 4, criterion 4 first, then this.

## The question this task has to answer first

**What does moving a vertex even mean here?** It is not obvious, and the naive answer is not
representable.

A box corner is used by three quadrilateral faces. Move that one corner and each of those quads has
four points that are no longer coplanar — and `SurfaceKind::Plane` cannot hold a non-planar face.
Storing the result would need general freeform or triangulated faces, which ADR-045 deliberately does
not do. So "move the vertex and leave everything else alone" has no representation, and a version
that did it anyway would produce a solid whose faces do not contain their own boundaries: the exact
failure REQ-319's precondition exists to prevent, and one `Validate` cannot see.

**The answer, and it falls out of the machinery that already exists.** A planar face has one degree
of freedom that keeps it planar: sliding along its own normal. Three planes meeting at a corner have
three such freedoms, and three freedoms is exactly a point. So:

> **Moving a vertex by `delta` = offsetting each of the three planes meeting there by
> `dot(delta, outward normal)`, then re-solving every affected corner.**

That lands the dragged vertex at exactly `p + delta` — each new plane reads
`dot(n, x) = d + dot(n, delta)`, which `p + delta` satisfies identically — and it keeps every face
planar by construction, because nothing but a plane offset ever happened.

**An edge is the same statement with two planes**, and it has a property worth stating rather than
apologising for: the component of `delta` ALONG the edge is annihilated automatically, because the
edge direction lies in both planes and so is perpendicular to both normals. That is not a limitation
being papered over — sliding an edge along its own line genuinely produces the same edge, so the
component is a no-op by geometry rather than by omission.

## Shape

A shared core, `OffsetPlanarFacesAndResolve`, that takes a list of (face, signed distance along its
outward normal) and re-solves every vertex of every offset face against the planes meeting there —
best-conditioned triple, then every other plane at that corner checked against the answer. That is
push/pull's own algorithm with the "exactly one face moves" assumption lifted.

`MoveVertex` and `MoveEdge` are then thin: gather the adjacent faces, check them, turn `delta` into
offsets, call the core.

### Refusals, each for a stated reason

| refusal | why |
|---|---|
| `MoveVertexNotThreePlanes` | fewer or more than three planar faces meet. A pyramid's apex has four: offsetting them generally leaves no single point satisfying all four, so the corner would have to SPLIT — a topology change, and a different operation. This is `PushPullVertexUnsolvable`'s reasoning at a vertex the user picked directly. |
| `MoveEdgeFacesParallel` | the two faces are coplanar or parallel, so their planes define no line to put the edge on. |
| `MoveSubObjectNeighbourCurved` | a curved face meets the moved sub-object. Re-solving a corner means intersecting the surfaces there, and a cylinder is not a plane to intersect. Push/pull can re-parameterise a wall along its axis; there is no equivalent for a corner being dragged in an arbitrary direction, so it is refused rather than approximated. |
| `MoveSubObjectNoMotion` | `delta` is zero, non-finite, or — for an edge — entirely along the edge, where it expresses no motion at all. |
| `MoveSubObjectResultInvalid` | the result failed `Validate`: pushed through itself, inverted, or collapsed. |

### What is deliberately NOT done

**`PushPullFace` is not refactored onto the shared core in this slice**, and that is a decision rather
than an oversight. Its planar path is entangled with two curved paths (the cylinder-wall radius
change and the cap-push re-parameterisation) that the core has no business knowing about, and
unpicking them risks a shipping, well-tested feature for tidiness alone. The core is written with its
failure codes as a parameter precisely so that routing push/pull through it later is a wiring change
rather than a rewrite. Recorded as DEBT-1.

## Files

- `src/util/brep.{hpp,cpp}` — the core, the two operations, five `Problem` values and their text.
- `tests/MoveSubObjectTests.cpp` (new) + `CMakeLists.txt`.
- `spec/requirements.md` — REQ-333 and its traceability row; issue #148 criterion 3's status.
- `spec/architecture.md` — ADR-046 amendment (n).

## Test approach

The acceptance is exactness plus the refusals, and the closed forms are the check:

1. **A box corner dragged by `(dx,dy,dz)` lands exactly there**, and the box stays a box — 8/12/6,
   every face still planar, valid. Volume against the hand-computed closed form.
2. Dragging a corner outward on all three axes grows the box by exactly the expected amount.
3. **A box EDGE dragged perpendicular to itself** moves both adjacent faces; the volume matches the
   closed form.
4. **The along-edge component is a genuine no-op**: dragging along the edge direction is refused as
   no motion, and a diagonal drag gives the same solid as its perpendicular component alone.
5. A wedge — where the adjacent planes are not axis-aligned — to prove the solve is general and not
   a box special case.
6. Every refusal by name: a pyramid apex (four planes), a cylinder's corner (curved neighbour), a
   zero delta, a non-finite delta.
7. Every edit still `Validate`s, and the recipe is DROPPED (a box with a corner pulled out is no
   longer the box its recipe describes — REQ-319 item 9's rule, which applies here and did not apply
   to rotate/scale).

## Verification

- **build-project** — PASS, Release and Debug, no new warnings.
- **testing** — PASS. `ctest` **1391/1391** (1384 + 7 new cases).
- **Proven to bite.** Replacing the per-plane projection `dot(delta, n)` with a uniform offset fails
  3 of the 7 cases, including the exact-landing assertion at a residual of **1.195** where the test
  allows 1e-9. The closed-form volumes (22x11x9 for the corner drag, 20x13x10 for the edge) came out
  as predicted before the code was run, which is what makes them acceptance rather than description.
- **A real defect found while writing this, and it is the finding worth keeping.**
  `CollectFaceVertices` **appends** to its output rather than clearing it — every existing caller
  either passes a fresh vector or wants the accumulation, so nothing had ever exercised the other
  case. I reused one buffer across a loop, which made every face after the first appear to use the
  vertex, and `MoveVertex` refused a perfectly ordinary box corner with `MoveVertexNotThreePlanes`.

  What made it findable quickly was not reading the code: it was that the refusal **named
  something checkable**. "Not exactly three planar faces meet here" is a claim about the box, and the
  box plainly has three, so the disagreement was between the code and an observable fact rather than
  somewhere in a hundred lines of solve. Three call sites fixed; the append behaviour is now noted at
  each.
- **architecture-review** — PASS. Pure kernel addition in the layer that owns the other solid edits.
  The shared core satisfies CLAUDE.md's "two current concrete uses" rule with two, and is written so
  a third (push/pull) is a wiring change.
- **code-review** — self-run. One thing fixed: the offset faces' surface origins were being
  re-derived as `n * d`, which is a point on the plane but sits at the foot of the perpendicular from
  the WORLD origin — arbitrarily far from the face, and at state-plane magnitudes a large number
  standing in for a small one. Now translated from the original origin, which is what `PushPullFace`
  does and for the same reason.

## Technical debt

- **DEBT-1 — `PushPullFace` still has its own copy of the corner re-solve.** Deliberate, and the
  reason is in the task above and in ADR-046 (n): its planar path is entangled with two curved paths
  the core has no business knowing about. The core takes its failure codes as a parameter so that
  routing push/pull through it is a wiring change rather than a rewrite. Worth doing when there is a
  reason beyond tidiness — a third caller, or a change that would otherwise have to be made twice.
- **DEBT-2 — nothing calls these yet.** This is the kernel only. The sub-object grips and the gizmo
  handles for an edge and a vertex — which REQ-060 currently declines to draw, correctly, because no
  operation existed — are their own slice, and #148 criterion 3 is not closed at the UI until they
  land. Named here so that is a stated boundary rather than an assumed one.

## Landing note

Fourth in the chain behind TASK-230 (PR #452), TASK-231 and TASK-232, none merged. Developed locally
on top of them; the PR is held until they land and this is rebased onto `beta`.
