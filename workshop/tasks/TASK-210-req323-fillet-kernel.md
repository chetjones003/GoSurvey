# TASK-210 — FILLET in the kernel (REQ-323 increment 1, GitHub issue #148 acceptance 5)

## Requirement authority

REQ-323, accepted the same day (D-2026-09-05-c, ADR-046 amendment (j)). Increment 1 is items 1–9:
one straight **convex** edge between two **planar** faces, every face at either endpoint planar and
square to the edge, a shared corner refused by name.

Kernel first, command second — the shape REQ-319 and every Phase 4 slice used.

## What was built

| file | change |
|---|---|
| `src/util/brep.hpp` | ten `Problem` values; `FilletEdge`, `FilletEdges` |
| `src/util/brep.cpp` | the rolling-ball construction, the pre-check, and the compaction pass |
| `tests/FilletEdgeTests.cpp` | new — 16 cases |
| `spec/requirements.md`, `spec/project.md` | two acceptance numbers corrected (below) |

## The construction, in one paragraph

The ball's centre line is the meet of the two adjacent planes each offset by `r` toward the
material — a 3×3 solve whose determinant is `dot(cross(nA, nB), t)`, non-zero exactly when the faces
are not parallel. The tangent points are that line pushed straight back out along each face's own
normal. The direction from the edge *into* each face is taken from the topology
(`cross(outward normal, traversal direction)`) rather than sampled, so it is right for a face of any
shape; the sign of `dot(uA, nB)` is what separates convex from concave.

## Two things that took a second attempt

1. **`tanB`'s traversal flag was inverted**, which `Validate` reported as `LoopNotClosed`. Worth
   recording because the fix is not "flip it until it passes": face A and face B traverse their
   shared edge in *opposite* directions — that is the orientability invariant — so once the fillet
   loop's first use is fixed as the opposite of face A's, every other flag in the ring is determined
   rather than chosen. It now reads that way.

2. **The end-face gap is found by walking the loop**, not by assuming where the corner was. A loop's
   starting point is arbitrary and its winding depends on which side of the solid the face is on, so
   the code looks for the one place consecutive uses stop meeting and inserts the arc there.

## The two acceptance numbers this corrected, and why that matters

REQ-323's acceptance was written as **arithmetic, before any code**. Both errors below were mine, in
the requirement, and both surfaced the moment the implementation disagreed with it. A test written
after the code would have recorded whatever came out.

- **Area.** Stated `800 + 18*pi`, on the reasoning that each end face loses a quarter-disc where the
  fillet passes. It does not. The end face **is** the cross-section plane, so what it loses is the
  same cross-section the removed prism has, `r^2 (1 - pi/4)` — the sliver *between* the arc and the
  old square corner, not the disc inside the arc. The area is `792 + 22*pi = 861.1150383794`,
  re-derived face by face by hand (160 + 120 + 160 + 200 + 2×79.141593 + 20π) before the requirement
  was changed. **The volume, `1520 + 20*pi`, was right first time.**
- **The wedge's end curve.** Claimed to be an ellipse. It is not: `MakeWedge` is a right prism, so
  its ends are square to the ridge and the boundary is a circle. An ellipse needs an end face
  *oblique* to the edge — a pyramid's base edge — which increment 1 refuses by name. The wedge keeps
  its place in the acceptance for the dihedral it does have: 68.199°, setback
  `40 / (sqrt(464) - 8) = 2.9540659229` rather than 2.

That is **three** geometric claims about this kernel wrong on measurement in two days, after the
wedge-vs-cylinder mix-up about what `Validate` actually rejects (D-2026-09-04-c). The pattern is now
recorded in D-2026-09-05-c rather than rediscovered a fourth time.

## Test approach

`FilletEdgeTests`, 16 cases. The box is checked against both closed forms *and* against the topology
delta amendment (j) states, plus the surface **kind** — a fillet that produced the right volume out
of the wrong surface would pass the numbers and be wrong for everything downstream.

The **wedge** is the case that earns its keep: its dihedral is not 90°, so an implementation that
simply cut `r` off each face would pass the box and fail here. Its setback is asserted against the
closed form `2*L*r / (sqrt(L^2+h^2) - h)` rather than a decimal, because the first draft's hand-typed
`2.9516` was itself wrong by 0.0025 and this case caught that too.

Every refusal is asserted **by name** with the output solid untouched, including at the exact limit
`r = 8` — at the limit the face does not thin, it vanishes.

The last case is REQ-323's own final bullet: `PushPullFace` still works on the filleted solid. A
fillet that left a shape later operations refused would be worse than one that refused up front.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean tree.
- **testing** — PASS. `ctest` **1218/1218**.
- **architecture-review** — PASS. Kernel-only; the operation follows amendment (i)'s copy-edit-
  validate shape and brings its own pre-check, and adds amendment (j)'s topology change.
- **code-review** — self-run. It found the inverted traversal flag above, and one thing worth
  keeping: `CompactUnused` is a separate pass rather than bookkeeping woven through the edit,
  because `UnusedVertex` is a `Validate` failure — forgetting the sweep fails loudly.
- **performance-review** — PASS. One whole-solid copy per filleted edge, as every modifying
  operation makes (ADR-046 (d)), plus a linear sweep per edge.

## Not covered by test, stated plainly

- **The `FILLET` command.** Next slice — there is no way to reach this from the UI or a transcript
  yet, so REQ-323's undo, `.gs` round-trip and refusal-message bullets are not yet asserted.
- **An oblique end face.** Refused by name; the ellipse construction is increment 4.

## Technical debt

- **DEBT-1 — `FilletEdges` re-finds each edge by its endpoint POSITIONS** after the previous fillet
  compacted the arrays. Correct, because edges that share no vertex do not move each other's
  endpoints, but it is O(edges) per requested edge and it would need rethinking if the shared-vertex
  restriction were lifted.
- **DEBT-2 — the concave edge, the oblique end face and the spherical corner** are each refused by
  name and each is its own increment. Only the corner blocks an issue-#148 acceptance line.
