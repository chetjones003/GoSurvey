# TASK-203 — sweep twist (straight paths) and fixed orientation (any path) (REQ-315, issue #259)

## Requirement authority

- **REQ-315** — Sweep and loft, accepted/delivered. Its Revisions line named "a twist or fixed
  orientation on a curved or multi-segment path" as deferred — the third and last item in issue #259,
  after TASK-201 (closed paths) and TASK-202 (mitred corners).
- **GitHub issue #259** — explicitly noted this item "needs a design decision on the desired framing
  behaviour before implementing." Two decisions were put to the user up front (see below), and two
  more real findings surfaced mid-implementation, each put back to the user before proceeding further
  — this task has four recorded decisions in total, more than either of the other two #259 items.
- **A SPEC amendment is part of this task**, per CLAUDE.md's authority order — done ahead of this
  file: REQ-315 gained a Statement bullet, an Acceptance bullet, and a Revisions entry.

## Decisions, in the order they arose

1. **Twist distribution law** (put to the user before coding): proportional to distance travelled
   (arc length) — the standard CAD convention — over an alternative of an equal share per segment.
   Chosen: distance-proportional.
2. **Whether to add a `SelfIntersects` safety check to `Sweep()`** (put to the user before coding,
   based on the *reasonable-sounding but ultimately wrong* premise that fixed orientation on a curve
   needed one and that `SelfIntersects` could provide it): chosen yes, add it — see finding 2 below for
   why this had to be reversed.
3. **Twist combined with an arc path segment** (mid-implementation finding, put back to the user): a
   test caught a *silently wrong volume*, not a crash — the arc band's construction cannot represent a
   vertex's true trajectory under continuously varying twist. Options were to refuse twist+arc by name
   (narrowing scope, mirroring the mitred-corner task's trimmed-NURBS precedent) or attempt a new
   curved-surface representation. Chosen: refuse by name
   (`Problem::SweepTwistNeedsStraightPath`), ship everything else.
4. **The `SelfIntersects` check from decision 2 doesn't work** (mid-implementation finding, put back
   to the user): `brep::SelfIntersects` turned out to be a narrow, torus-specific check (ADR-045 (f)'s
   tube-larger-than-ring case) — not a general overlap detector — verified by reading its actual
   implementation (four lines, checking one `SurfaceKind::Torus` condition) rather than trusting the
   name or the earlier research report that assumed otherwise. Options were to build fixed orientation
   on a curve as designed and document the fold-over-itself risk as a known limitation, or refuse fixed
   orientation on any curved path outright (a much smaller feature than asked for). Chosen: build it,
   document the limitation, no fake safety net.

## Scope, as it ended up

- **Twist** works on a straight, or multi-segment all-straight, path — distributed proportionally to
  distance travelled. Refused, by name, on a closed path (geometrically inconsistent — one seam ring
  cannot carry two different end-of-path orientations) and combined with an arc segment (would need a
  curved-surface representation this kernel does not have).
- **Fixed orientation** (`alignToPath = false`) works on **any** path, including one with arc
  segments — the profile's axes never rotate, only translate, and this generalizes exactly to a curved
  path because a rigid body under pure translation moves every point by the identical vector
  regardless of the path's own shape (proven, not assumed — see Implementation approach).
- Twist and fixed orientation compose on a straight path.
- A mitred corner still mitres under either option (the shear construction only ever depended on the
  two tangents meeting there, never on how the frame got carried).
- **Not built, documented as a known limitation**: detecting a fixed-orientation sweep that folds over
  itself on a curved path. No real general-purpose overlap detector exists in this kernel to build the
  check on top of.

## Files / subsystems affected

- `spec/requirements.md` — REQ-315 Statement + Acceptance + Revisions.
- `src/util/brep.hpp` — `SweepOptions` doc comments; `Sweep()` doc comment; two new `Problem` values
  (`SweepTwistNeedsStraightPath`; `SweepUnsupportedOption`'s doc comment narrowed to the closed-path
  case specifically, since it no longer means "single straight segment only").
- `src/util/brep.cpp` — `Sweep()`: the `singleStraight`-only gate removed; a closed-path-with-twist
  gate and an arc-with-twist gate added in its place; `SweepSegGeom` gained a `length` field
  (`SegGeom` computes it: chord length or `radius * |sweep|`); the frame-propagation loop gates every
  rotation (arc propagation, mitre-turn) on `options.alignToPath`; a new per-ring twist pass
  (proportional to cumulative length fraction) replaces the old "rotate only the end frame" code,
  reducing to it exactly for the single-straight-segment case; the arc-segment rail and side-face
  patch construction both gained an `|| !options.alignToPath` branch, treating a fixed-orientation
  band as a straight-line/ruled construction (matching the straight-segment case) regardless of
  whether the underlying path segment is an arc — this is the fix that makes fixed-orientation-on-an-
  arc exact, not approximate.
- `tests/BrepTests.cpp` — new `[req315]` cases (below); one pre-existing test section ("a twist on a
  curved path" inside "Sweep refuses bad input by name") removed, since that behavior is exactly what
  this task changes.
- `tests/headless/transcripts/req315-sweep.txt` — the twist-on-an-arc-path step now asserts the
  refusal (`SOLIDS 1`, unchanged), reflecting the final (narrower-than-first-attempted) scope.

## Implementation approach

**Twist**: `SweepSegGeom` gained `length`. A cumulative-length array over ring points drives a
per-ring rotation (about that ring's own `zAxis`) applied to `frame[k].xAxis`/`yAxis` after alignment
and mitre are resolved — twist spins the cross-section in place, it does not change the path. For the
single-straight-segment case this reduces exactly to the old "rotate only the end frame" behavior
(ring 0's fraction is exactly 0).

**Fixed orientation exactness proof** (the load-bearing piece of this task, verified by direct
construction before it was written down as a spec claim): for `alignToPath = false`, frame
orientation never changes — the frame-propagation loop's `if (g.arc) { rotate }` branch is now
`if (g.arc && options.alignToPath) { rotate }`. This means every profile point's position at ring `k`
is `points[k] + off` for a *fixed* offset `off` (the profile's own local embedding, constant across
every ring). Therefore ring `k+1`'s corresponding point is `points[k+1] + off = ring_k's point +
(points[k+1] - points[k])` — a **rigid translation by the segment's own endpoint displacement**,
regardless of whether that segment is straight or an arc. This is why the arc-segment rail/patch code
now falls back to the straight-segment (line/ruled) construction whenever `!options.alignToPath`: the
true geometry *is* that construction, exactly, not an approximation of it.

**Two real bugs/false-assumptions found via direct verification, not by re-reading code more
carefully**:
1. First test draft assumed twist preserves volume (`area × length`, mirroring the mitre task's
   identity). A test disproved it: a band's surface is a *ruled* (straight-line) interpolation
   between its two differently-twisted end rings, not a true continuous rotation — linear
   interpolation between two rotated copies of a shape "cuts corners" relative to the true
   continuously-rotating volume, so the twisted volume is provably *less than* `area × length` in
   general, and this was true even for the already-shipped single-straight-segment case (never
   asserted or checked before this task). The false claim was removed from the spec before it could
   ship as a wrong Acceptance bullet; the *ring-position* cross-check against an independent reference
   sweep (proven exact) replaced it as the correctness signal.
2. `Sweep()` was given an internal `SelfIntersects` check per the user's decision 2 — then a test
   built a shape designed to fold over itself, and `Validate` failed with "does not enclose a volume"
   *before* the `SelfIntersects` check even ran, with a different `Problem` than expected. Reading
   `SelfIntersects`'s actual four-line implementation (not its name, not the earlier research report)
   showed it checks exactly one condition — `SurfaceKind::Torus` with `radius2 >= radius` — and
   nothing else. It could never have caught a folded sweep. Reverted the check, the `Problem` value,
   and the spec claim; documented the gap instead of pretending to close it.

## Test approach

`BrepTests.cpp`, `[req315]` tag. Twist: ring-position cross-check against an independent
single-segment reference (exact, not approximate — the two-segment sweep's joint ring must match the
reference's end ring vertex-for-vertex) on a collinear 2-segment path with unequal segment lengths (4
and 6, verifying the *proportional* part of the rule, not just "twist happens"). Fixed orientation:
ring-position check against the "translate by the endpoint displacement" identity on an arc path
(exact, provable, not a hand-placed guess). Refusals: closed+twist, arc+twist, both asserted by name.
Existing `ctest` suite (1150 tests) stays green, including the one headless transcript step this task
changes.

## Architectural-boundary check

- Two new `Problem` values (`SweepTwistNeedsStraightPath`); one existing value's meaning narrowed
  (`SweepUnsupportedOption`, now closed-path-twist only) with its doc comment updated to match; one
  value added and then removed in the same task (`SweepSelfIntersects`) once proven not to work —
  removed cleanly rather than left as dead/misleading code.
- No new `SurfaceKind`; fixed orientation reuses the straight-segment ruled construction exactly,
  twist reuses the existing per-ring frame mechanism with an added rotation pass.
- `solidpick`, the renderer, IO, and the command layer are unaffected — `SweepOptionsFrom` already
  plumbed `twistRad`/`alignToPath` through with no path-shape awareness, so no command-layer change
  was needed; the kernel gate was the only thing restricting this before.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean.
- **testing** — PASS. `ctest` **1150/1150**.
- **architecture-review** — PASS by inspection (see boundary check above).
- **code-review** — self-run; both findings above were caught during this same implementation pass by
  verifying claims with direct construction before writing them into the spec or a test, the same
  discipline TASK-201 and TASK-202 used and documented.

## Final independent review (second pass, a fresh reviewer)

Found and confirmed one real, previously-unnoticed defect: the mitre shear loop
(`brep.cpp`, after the profile-prep loop) was gated on `mitreAt[k]` but **not** on
`options.alignToPath` — so a fixed-orientation ring at a sharp corner was sheared off its true path
point, even though fixed orientation needs no shear at all (every ring already sits exactly on its
path point without one, unlike the aligned case, which genuinely needs the shear to reconcile two
different cross-section planes at the corner). Fixed by adding `|| !options.alignToPath` to the same
skip condition. The reviewer's own test (a non-planar closed triangular path) additionally surfaced
that fixed orientation on **any closed path** always encloses zero volume — a rigid, non-rotating
cross-section translated around a closed loop returns to its exact start, confirmed against two
independent non-planar examples rather than assumed — which the kernel's existing generic closure
check already refuses correctly (`Problem::NotClosed`) with no special-casing needed. Replaced the
reviewer's (well-intentioned but geometrically-impossible) "closed path succeeds" test with two
correct ones: an **open** path with two non-coplanar sharp corners (proves the actual bug fix) and a
**closed** path asserting the `NotClosed` refusal by name (documents the zero-volume fact rather than
treating it as a surprise). Spec's Statement/Acceptance/Revisions updated to match. Re-verified:
`ctest` **1152/1152** after both fixes.

## Acceptance criteria status

Against issue #259's "twist / fixed orientation on a curved or multi-segment path" item:

| criterion | status |
|---|---|
| Twist works on a multi-segment (all-straight) path, distributed proportionally | **met** |
| Fixed orientation works on a curved (arc) path | **met**, and proven exact, not approximate |
| Twist and fixed orientation compose | **met**, on a straight path |
| A mitred corner still mitres under either option | **met** (unaffected — shear depends only on tangents) |
| Nonzero twist refused on a closed path | **met** |
| Twist combined with an arc segment | **narrowed**: refused by name, not built, after a verified finding that building it correctly needs a surface representation out of this kernel's current scope |
| Fixed-orientation self-overlap on a curve is caught | **not met, by verified finding and user decision**: no real detector exists to build this on; documented as a known limitation instead |

## Not covered by test, stated plainly

- **A genuine self-overlapping fixed-orientation solid.** No test asserts this is refused, because it
  isn't — deliberately, per the decision above. No test asserts it succeeds either with a specific
  "wrong" volume, since that would just be pinning an accident. The limitation is documented in the
  `SweepOptions::alignToPath` doc comment and the spec Statement, not exercised by a test.
- **Command-layer / GUI verification of `T`/`A` keywords on the newly-supported path shapes.** The
  command layer never restricted these by path shape (confirmed by reading `SweepOptionsFrom` and the
  keyword-parsing code before starting), so no command-layer change was needed and none was made;
  only the kernel gate changed. The updated headless transcript step exercises the twist+arc refusal
  end-to-end through the command layer, which is the one behavior change visible from there.

## Technical debt

- **DEBT-1 — a general geometric self-intersection detector does not exist in this kernel.**
  `brep::SelfIntersects` is torus-specific. Every OTHER feature op's `Validate(s) != Ok ||
  SelfIntersects(s)` pattern (dozens of call sites) is equally narrow for the same reason — this was
  already true everywhere else in the kernel before this task, not introduced by it, but this task is
  what surfaced it clearly enough to write down. A real one is a separate, substantial undertaking
  (checking every tessellated face against every other) — out of scope here and not attempted.
- **DEBT-2 — REQ-315's own Statement text claimed the command layer checks `brep::SelfIntersects`
  before consuming Loft/Sweep's source profiles and path.** Grepped and confirmed: it does not, for
  Loft, Sweep, Extrude, or Revolve — nowhere in `CadCommands.cpp`. This is a pre-existing spec/code
  mismatch, not introduced by this task; the sentence was corrected here (removed the parenthetical
  claim) but the underlying command-layer gap across all four operations is unaddressed and not this
  task's to fix.
