# TASK-223 — a blend's precondition sees the whole request (REQ-323 + REQ-329 item 4b)

## Requirement authority

- **REQ-323 item 4b** and **REQ-329 item 4b** — both AMENDED 2026-09-08 by **D-2026-09-08-c**.
- **ADR-046 amendment (l)** — the architectural decision; amendment (i) is the rule it corrects the
  application of.
- **GitHub issue #148 acceptance 6** — *"a fillet or chamfer that cannot be built is refused with a
  clear message and leaves the solid unchanged."* This was a defect against it for **both**
  operations.

## Where it came from

`/code-review high` on TASK-222 (the chamfer), Finding 1. The reviewer noticed the pre-check reads
the ORIGINAL solid. Reproducing it landed on the fillet too, which had already shipped with it.

**The user's instruction, 2026-09-08:** *"fix it for both the fillet and the chamfer"* — so this is
not scoped to where the defect was found.

## The defect, measured

REQ-323 item 4 stated the precondition as *"the radius must be strictly less than the distance from
the edge to the far boundary of each adjacent face"*, and REQ-329 item 4 said the same for a
distance. Both are **per-edge**, and both measure against the **original** solid. That was sufficient
while the operation took ONE edge; increment 2 made the request a SET, and nobody noticed, because
the check kept passing.

Two things it cannot see:

1. what another requested edge takes out of the same face;
2. one edge's two ends eating each other — at a corner the blend runs some distance ALONG the edge
   before it begins, and with a corner at both ends the two can want more than the edge has.

**What that produced was a solid, not a refusal.** `Validate` is topological, so a face whose
boundary has crossed over and inverted passes it, and `ComputeMassProperties` returns a number for
it. A 20 x 10 x 8 box's two 20-long top edges are 10 apart across a 10-wide face, so any setback
above 5 crosses:

| | before | after |
|---|---|---|
| `FILLET 6` | **accepted**, volume 1290.97336, self-intersecting | refused, `FilletRadiusOverlapsAnother` |
| `CHAMFER 6` | **accepted**, volume 880, self-intersecting | refused, `ChamferDistanceOverlapsAnother` |
| either at exactly 5 | refused — by accident: the face reaches ZERO area and `DegenerateFace` catches it | refused by name |
| either at 4.9 | accepted | accepted, unchanged, and still on the closed form |

So the failure window was "the strips overlap", not "the strips touch", and the one case that was
caught was caught for the wrong reason.

## The fix

**One shared implementation, deliberately.** `BlendSpan` + `BlendsFit` in `brep.cpp`, called by both
`FilletEdgesGeneral` and `ChamferEdgesGeneral`. This codebase tolerates mirrored fillet/chamfer code
elsewhere (the paper-space paths, the prompt handlers) and that is fine — **mirrored prompts can
drift harmlessly, a mirrored precondition cannot, and this defect is what that drift looks like.**
Two concrete uses, so no abstraction is being invented ahead of need.

Two conditions join the per-edge one, which stays:

1. `length > consumed[0] + consumed[1]` per edge. `consumed` is zero at an open end, so an unchained
   request is untouched. For the fillet it falls out of geometry already computed —
   `dot(axisPt[k] - p[k], t)` is zero at an open end and the corner's own bite at a corner, with no
   second rule. For the chamfer it is `distance`, exact because a corner is already required to be
   orthogonal.
2. For two requested edges bounding one face, the room between them must exceed the sum of their
   setbacks. **Adjacent edges are exempt**, and that is load-bearing rather than an optimisation:
   their cuts are *meant* to meet at the corner vertex, and condition (1) covers their interaction.
   Without the exemption every corner in increment 2 would refuse.

**Four new refusals, not two, and not folded into the existing one.** `FilletRadiusTooLarge`'s
sentence says the setback *"would reach past the far side of an adjacent face"* — true of the
per-edge case and false of both new ones. A user whose real obstacle is a second edge they selected
would go looking for the wrong thing. Both requirements say "refused by name"; a name describing a
different failure is not one.

## Where it is exact and where it is conservative

Condition (2) measures from the nearer of the other edge's two endpoints, which is **exact** for the
parallel case a box, wedge or prism actually produces. For two edges of one face that are neither
parallel nor adjacent — reachable only on a face with more than four sides — it is conservative and
errs toward refusing something buildable. Deliberate: the failure being fixed is the other direction.

## Verification

- **build-project** — PASS, no warnings.
- **testing** — PASS. `ctest` **1333/1333** (was 1325: +4 `FilletEdgeTests`, +4 `ChamferEdgeTests`),
  plus new blocks in both `headless.req323-fillet-solid` and `headless.req329-chamfer-solid`.
  **Nothing that passed before now fails** — in particular the rounded-box (`r = 2`) and bevelled-box
  (`d = 2`) closed-form cases are unaffected, which is what shows the new check has not quietly
  shrunk the envelope.
- **architecture-review** — PASS. One new helper in `brep.cpp`'s existing anonymous namespace, no new
  type in the header beyond four `Problem` values, no new dependency.
- **code-review** — self-run. The risk here is OVER-refusal, since a false refusal is the failure
  mode a fix like this introduces. Covered by asserting the largest still-valid value in every new
  case (4.9 for two opposite edges, 3 for the five-edge chain, 3.9 for all twelve) rather than only
  asserting the refusals.
- **performance-review** — PASS. `O(n^2)` in the number of REQUESTED edges, with `n` at most the
  edge count of one solid and typically under a dozen; no per-frame cost.

## The finding worth keeping

**Not one existing test failed, before or after.** None of them asked for two blends that collide.
The defect surfaced because a review read the check and noticed *what it was measured against*, not
because anything went red. A precondition exercised only on requests it accepts is no evidence that
it refuses the right things — and REQ-323 had shipped, reviewed and merged-pending with it.

## Technical debt

- **DEBT-1 — the non-parallel, non-adjacent pair in one face is conservative, not exact.** Unreachable
  on today's primitives; would need a real in-face 2D intersection test to be exact.
- **TASK-222 DEBT-4 is CLOSED** by this task. TASK-222 DEBT-5 (the contradictory "could not parse"
  trailer, shared with FILLET) is untouched and still open.
