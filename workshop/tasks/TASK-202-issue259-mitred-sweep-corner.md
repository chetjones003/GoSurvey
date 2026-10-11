# TASK-202 — mitred sweep-path corners: straight-to-straight (REQ-315, issue #259)

## Requirement authority

- **REQ-315** — Sweep and loft, accepted/delivered. Its Revisions line named a mitred path corner as
  deferred, alongside twist-on-curved-path and a full-turn arc segment (both issue #259 items; the
  full-turn arc segment shipped in TASK-201).
- **GitHub issue #259** — the tracking issue for all three deferred items. This task is the second.
- User decided (2026-09-04) to attempt the full general case (including a corner touching an arc
  path segment) — then, mid-investigation, a real conflict surfaced: an arc-adjacent mitre needs a
  **trimmed** NURBS patch (different profile vertices reach the cut plane at different points around
  the curve, so the patch's parameter domain is not a rectangle), and REQ-315 already names trimmed
  NURBS **out of scope as its own future decision**. Put back to the user, who chose to ship the
  straight-to-straight case now and leave the arc-adjacent case deferred alongside it, rather than
  reopen the trimmed-NURBS question here.
- **A SPEC amendment is part of this task**, per CLAUDE.md's authority order — done ahead of this
  file: REQ-315 gained a Statement bullet, an Acceptance bullet, and a Revisions entry recording the
  scope decision and the reason.

## Scope, as decided

1. A sharp (tangent-discontinuous) corner where **both** adjoining path segments are straight, and
   the profile is **polygonal** (no arc edge) — mitres: the shared ring is cut on the plane bisecting
   the incoming and outgoing tangents.
2. Applies at an interior joint and at a closed path's own closing seam alike.
3. Refused by name, unchanged behavior for what's out of scope: a corner touching an **arc** path
   segment (`Problem::SweepPathCorner` — needs a trimmed patch, deferred with REQ-315's own trimmed-
   NURBS boundary); a profile with an **arc edge** at a mitred corner (`Problem::SweepMitreProfileArc`
   — shearing a circular edge onto an oblique plane makes an ellipse, not built this increment); a
   corner too sharp to mitre, near a full reversal (`Problem::SweepMitreCollapsed`).

## Files / subsystems affected

- `spec/requirements.md` — REQ-315 Statement + Acceptance + Revisions (done first).
- `src/util/brep.hpp` — two new `Problem` values; `Sweep()`/`SweepPath` doc comments.
- `src/util/brep.cpp` — `Sweep()`: corner classification (mitre vs. refuse, by reason); the mitre
  shear itself; a frame-propagation fix (`TurnFrameToTangent`, new); closed-path `prep[np-1]`
  aliasing fix (see below).
- `tests/BrepTests.cpp` — new `[req315]` cases (single corner, multi-corner open path, closed
  rectangular path all-mitred, and the three refusal reasons); two pre-existing tests that asserted
  a mitred corner was **refused** are rewritten to assert it now **builds** (the behavior they tested
  is exactly what this task changes).

## Implementation approach

**The construction.** At a mitred joint, the shared ring's vertices are individually slid — each
along the straight rail it already sits on (the segment's own constant tangent direction) — until
they land on the bisector plane `N = normalize(tangentIn + tangentOut)`, anchored through the path's
own vertex at that joint. This is a closed-form per-vertex line/plane intersection, not an iterative
solve. Two geometric facts made the topology reuse-only (no changes to ring/rail/patch construction
beyond feeding it different vertex positions):

- **Volume identity.** Since the bisector plane passes through the path's own vertex, each leg's
  swept volume is exactly `area × nominal leg length`, regardless of the plane's tilt — a Cavalieri
  argument (truncating a constant-cross-section prism through its own centroidal-axis point). This is
  what makes the feature testable without a numerical reference.
- **Two-sided consistency.** Shearing "from the incoming leg's own perpendicular ring" and shearing
  "from the outgoing leg's own perpendicular ring" land on the exact same 3D point, PROVIDED the
  outgoing leg's frame is obtained by **turning** the incoming frame through the corner (rotating its
  axes by the same rotation that carries `tangentIn` to `tangentOut`) — not by re-deriving a frame
  fresh from `profile.plane`. Verified by direct construction (see "bugs found" below); this is the
  discrete, single-turn analogue of the continuous `RotateAbout` propagation an arc segment already
  uses.

**Two real bugs found and fixed while implementing** (both by direct numeric construction, not by
re-reading the code more carefully — this is the note CLAUDE.md's "review as if someone else's PR"
step exists for, done here during first-pass implementation instead):

1. **Wrong frame propagation past a mitre.** The first draft re-derived each leg's frame fresh from
   `profile.plane` at every mitred joint. A single-corner test passed only because, in that first
   corner, the incoming frame happened to equal `profile.plane` with no prior rotation — a degenerate
   case that masked the bug. A two-corner (Z-shaped path) test exposed it: the volume came out to
   `200/3` instead of `80`. Root cause and fix: `TurnFrameToTangent` (new helper, `brep.cpp`) rotates
   the *existing* frame through the corner's own turn instead of re-deriving one independently.
2. **`prep[np-1]` not aliased to `prep[0]` on a closed path.** `ringV[np-1]`/`ringE[np-1]` were
   already aliased to ring 0 (TASK-201), but the profile-*prep* data (`LoftProfilePrep`, used to build
   the actual NURBS surface patches, not just the topology) was not — so the last band's **surface**
   was built from the stale, unsheared `prep[np-1]` while its **boundary edges** pointed at the
   correctly sheared `ringV[0]` vertices. A patch that does not match its own boundary is exactly what
   the closed-surface volume check (`brep::Validate`) exists to catch, and did: `NotClosed`, with the
   volume measured from two different reference points disagreeing (19 vs. 16.667, expected 20) —
   proof of a real geometric defect, not quadrature noise. This gap was latent in the TASK-201 closed-
   path code too, masked there because a *smooth* closing seam's own consistency check keeps
   `frame[np-1]` numerically close enough to `frame[0]` that `prep[np-1]` was close enough to
   `prep[0]` by coincidence — a mitred seam is the first case where the two frames genuinely differ,
   exposing it. Fixed by aliasing `prep[np-1] = prep[0]` outright when closed, mirroring the existing
   `ringV`/`ringE` aliasing exactly; this is a strict improvement for the pre-existing smooth case too
   (exact aliasing instead of tolerance-close).

## Test approach

`BrepTests.cpp`, `[req315]` tag. Volume checked against the area × leg-length identity (exact, not a
numerical reference) for: a single 90° corner; a Z-shaped 3-segment path (two corners, not coplanar
with each other — the case that caught bug 1); a closed rectangular path (all four corners mitred,
including the closing seam — the case that caught bug 2). Each of the three refusal reasons gets its
own case, asserted by name. Two pre-existing tests that asserted refusal are rewritten to assert the
new (correct) build-and-measure behavior.

## Architectural-boundary check

- No new `SurfaceKind`; the mitred case reuses the same `RuledLinear`/`RuledCurveToCurve` per-band
  construction Loft and the unmitred Sweep already use — only the vertex *positions* differ.
- Two new `Problem` values, both scoped narrowly to this feature's own refusal reasons; no existing
  `Problem` semantics changed except `SweepPathCorner`'s doc comment (narrowed to "touching an arc
  segment," since a straight-to-straight sharp corner is no longer refused by it).
- `solidpick`, the renderer, and IO are unaffected — a mitred solid is an ordinary `brep::Solid` once
  built.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean.
- **testing** — PASS. `ctest` **1145/1145**. `GoSurveyTests.exe` standalone green.
- **architecture-review** — PASS by inspection (see boundary check above).
- **code-review** — self-run; both bugs above were found and fixed during this same implementation
  pass (numeric verification of the geometry before writing the "expected volume" in a test, the same
  discipline TASK-201 used and documented).

## Final independent review (second pass, a fresh reviewer)

Verified the bisector-plane guard math algebraically (the `SweepMitreCollapsed` threshold is exactly
the shear denominator's own near-zero condition, so nothing slips past it), traced `TurnFrameToTangent`
and the `prep[np-1]` aliasing against every downstream consumer, and — since the existing tests only
cover a *two*-corner path — wrote and ran an additional 3-consecutive-corner test (4 segments, each
turn about a different, non-coplanar axis) to specifically stress whether the running frame stays
correct after two turns in a row rather than just one. It passed, volume exact to 1e-9. Folded into
this PR as `[req315]` case "Sweep mitres three consecutive corners of a helix-like 4-segment path".
No bugs found; `ctest` **1146/1146** after adding it.

## Acceptance criteria status

Against issue #259's "mitred (tangent-discontinuous) path corner" item, scoped by user decision to
straight-to-straight corners:

| criterion | status |
|---|---|
| A straight-to-straight sharp corner mitres to a valid closed solid, correct volume | **met** — single corner, multi-corner, and closed-loop cases |
| The closing seam of a closed path mitres the same way | **met** |
| A corner touching an arc segment is still refused by name | **met** — unchanged, doc comment clarified |
| A profile with an arc edge at a mitred corner is refused by name | **met** — new `Problem::SweepMitreProfileArc` |
| A corner too sharp to mitre is refused by name, not built collapsed | **met** — new `Problem::SweepMitreCollapsed` |

## Not covered by test, stated plainly

- **Command-layer wiring.** `SweepPathFromSelection`/the SWEEP command already build a
  `brep::SweepPath` from a selected polyline with sharp (non-tangent) vertices — no command-layer
  change was needed for this task (the path-building code never checked tangent continuity itself;
  that check lives entirely in the kernel). Not separately re-verified with a headless transcript in
  this task; the kernel change is what carries all the risk and is what's tested here.

## Technical debt

- **DEBT-1 — twist / fixed orientation still refused on any multi-segment path, including a mitred
  one**, unchanged. The `singleStraight`-only guard predates this task and a mitred path is never
  `singleStraight`, so it's excluded by construction — consistent with what REQ-315 already deferred.
- **DEBT-2 — arc-adjacent mitre remains deferred, now for a stated architectural reason** (trimmed
  NURBS, out of scope) rather than "a later increment" with no reason given. Revisit only alongside a
  trimmed-NURBS decision, not as a smaller version of this task.
