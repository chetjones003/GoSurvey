# TASK-189 — Sub-object picking: one shared pick for the snap and the selection (issue #148, REQ-318)

## Requirement authority

- **REQ-318** (new) — sub-object picking for B-rep solids.
- **ADR-049** (new) — one shared pick, the expiring sub-object reference, and what the projection
  does and does not fix.
- **D-2026-09-03-c** — the recorded decision behind both.
- REQ-313 / ADR-045 (the kernel and its analytic faces), REQ-314 / ADR-046 (the solids worth
  picking), REQ-058 (off plan view the viewport is a projection), REQ-101 (±0.01 ft), REQ-201
  (refuse, never repair silently), REQ-100 (frame budget).
- GitHub issue **#148** — Phase 5 of #120, slice 1. Follows #147 (Phase 4), merged 2026-09-03.
- Unblocks GitHub issue **#156**, which PR #180 deferred onto this work.

## Two process notes, both recorded rather than smoothed over

**1. The task file came after the code.** CLAUDE.md §3 puts it first. The plan itself was settled and
parked before any code (`Desktop\Notes for claude\issue148-direct-modeling\GAMEPLAN.md`, with probe
evidence), but outside the repo rather than here.

**2. The plan was built on a stale premise, and it survived to code review.** Both the issue body and
the first draft of REQ-318/ADR-049 claimed that solid faces were unpickable, citing PR #180:
*"`src/util/ray3d.hpp` has ray/plane and ray/segment tests but no ray/triangle."* That PR is dated
2026-08-31 — **before REQ-313 landed on 2026-09-01** — and it was quoted without being re-checked
against `beta`. In fact `src/viewport/CadSnap.cpp` has picked solid faces, edges and vertices since
REQ-313: `RayHitSolidFace` (Möller–Trumbore over the same display-cache triangle buffer, with the
`triFace` lookup and the `ClosestPointOnSurface` projection), `ClosestRayPointToEdge`, and
`RayNearBounds`. The first version of this slice therefore re-derived a shipping pipeline, and with
*different* numerics.

The defence that was skipped is the cheap one: a claim about what the codebase does not have is a
`grep` against `beta`, not a quotation from a PR. Carried into ADR-049's consequences so the next
reader sees why the ADR is shaped as a consolidation.

## What the issue asked for

> Select individual faces, edges and vertices of a solid — a new selection mode alongside
> whole-entity selection. Needs its own picking path and highlight treatment, and must not disturb
> existing whole-entity selection behaviour.

This slice is the **picking path**, and — given note 2 — specifically the job of making one pick
serve both the snap and the coming selection. The selection *mode*, the highlight treatment and the
grips that consume it are the slices after this one. #148's acceptance criteria 1 and 2 are not
claimed complete; see **Acceptance criteria status**.

## What it does

- **`ray3d::RayTriangleIntersect`** — Möller–Trumbore in `double`, the one ray/triangle test.
  Scale-relative degeneracy threshold, so a 0.25 ft triangle at easting 2e6 is not rejected, and a
  small outward barycentric slack so a ray through a face/face boundary of the deliberately unwelded
  tessellation hits both triangles rather than falling through the crack between them.
- **`src/util/solidpick.{hpp,cpp}`** — `PickSubObject(...)` returns the nearest `Face`, `Edge` or
  `Vertex` and the point **on** that geometry, plus `RayNearBounds`, the padded broad-phase reject
  lifted out of `CadSnap`.
- **`CadSnap.cpp` refactored onto both.** Its two file-private copies are gone.

## Design

**One pick, and the snap moves onto it.** The alternative — leave both implementations and reconcile
only the numerics — was put to the user and rejected. It is duplicate architecture (CLAUDE.md §7) and
the divergence was already real: the snap copy's absolute `1e-12` determinant epsilon and exact
barycentric bounds made it fall through the crack between two faces exactly where the shared test
reports a hit. One cursor would have produced a snap marker and a sub-object highlight naming
different things, and #156's "UCS from the picked face" would have aligned to a face object snap said
was not there.

**The projection places the point — and fixes one error, not two.** The triangle only locates the
sub-object. A raw triangle hit sits **0.00986 ft** off a cylinder's true surface at the shipping
chord tolerance: inside REQ-101's ±0.01 ft, which is the trap, while spending 98.6% of the budget
before anything else joins in.

The sharper half, which the first draft got wrong: the projection removes
**distance-from-surface** error and does nothing for **position-along-surface** error. So a test
asserting only the picked radius on a cylinder *cannot fail* — `ClosestPointOnSurface` rescales any
nearby point to exactly `r`. The tests assert the picked **azimuth** as well, and that is what makes
them bite. The same distinction is the module's precondition: the display buffers are `float`, which
is adequate only because storage coordinates are document-local and stay at model magnitude. Fed
absolute state-plane coordinates they quantize to 0.125 ft, and at oblique incidence that lands
*along* the surface where the projection is blind to it.

**Precedence is Vertex → Edge → Face**, bounded by occlusion measured against the nearest
**triangle** rather than the nearest usable face — a triangle with a corrupt face id still proves a
front surface is there, and using the next valid face's depth would put the baseline on the far side
of the solid.

**The ray is normalized on entry.** `RayTriangleIntersect`'s parameter scales as `1/|dir|` and
`ray3d::RayPointDistance`'s as `|dir|`, so on a non-unit ray a face depth and a vertex depth are a
factor of `|dir|²` apart and occlusion becomes meaningless rather than merely imprecise. A caller
unprojecting a near and far point — `dir = far - near` — would get no vertex or edge pick at all.
Normalizing beats documenting a precondition because the failure is silent.

**Curved-edge search keys on the curve KIND, not on `sweep`.** `brep::Edge::sweep` is documented for
`Arc` and `Ellipse` only, so a `CurveKind::Intersection` edge (the procedural surface-crossing curve
the B2b-2 booleans produce) leaves it zero. Keying on `sweep` gave those edges one straight chord end
to end — up to 0.29·r off a seam quarter-curve — so the refinement started from a bad seed,
over-estimated the distance, and the edge silently dropped out of the pick. `CadSnap` already keyed on
the kind.

**Everything is in the solid's own storage coordinates**; the caller converts the ray. A world ray
plus an offset would put the local/world seam inside a geometry module.

## Files affected

| File | Change |
|---|---|
| `src/util/ray3d.hpp` | **+`RayTriangleIntersect`**; one clamp fixed in `RaySegmentDistance`'s degenerate-segment branch (it returned a negative `outT` for a point behind the origin, which sorts as nearer than everything in front). |
| `src/util/solidpick.hpp` / `.cpp` | new — the shared pick and its broad phase. |
| `src/viewport/CadSnap.cpp` | **behaviour changes, deliberately** — `RayHitSolidFace` and `RayNearBounds` delegate to the shared versions. |
| `tests/Ray3dTests.cpp` | +10 cases for the new primitive. |
| `tests/SolidPickTests.cpp` | new — 21 cases. |
| `CMakeLists.txt` | two source-list entries. |
| `spec/requirements.md` | REQ-318 + its traceability row. |
| `spec/architecture.md` | ADR-049. |
| `spec/project.md` | D-2026-09-03-c. |

**An earlier version of this file claimed "no existing source file changes behaviour". That is no
longer true and the claim is withdrawn.** `CadSnap`'s solid pick now reports a hit on a face/face
boundary where it previously fell through, and accepts small triangles at survey magnitude its
absolute epsilon rejected. Both are fixes; neither is invisible, which is why they are in the ADR's
consequences rather than left for someone to discover.

## Architectural-boundary check

- **ADR-002 layering.** `solidpick` depends on `brep` and `ray3d` only — no GL, no ImGui, no
  `AppCommandState`. It links into the test target, which is the proof. Note the shared pick lives in
  `src/util`, not `src/viewport`: snapping is a *consumer* of the query, not its owner.
- **CLAUDE.md §7, "no abstraction without at least two current concrete uses."** Now satisfied
  concretely rather than prospectively: `CadSnap` is a real second consumer **today**, and issue #156
  is a third that PR #180 already committed to this design. The first version of this slice could
  only cite one future consumer, which was the weaker position and — with hindsight — the signal
  that the existing implementation should have been looked for first.
- **The `float` display buffers are consumed, not re-derived**, so there is no second tessellation to
  drift from the first.

## Test approach

`tests/SolidPickTests.cpp`, 21 cases, on the real kernel with a helper that assembles the display
triangles as `RefreshSolidDisplayGeometry` does.

- **Precision that bites** — a cylinder picked from 24 azimuths with **radius and azimuth** both
  asserted, plus the arc-length error against REQ-101 in its own units.
- **The precondition, pinned by evidence** — the same oblique ray geometry run twice: at storage
  magnitude it stays inside REQ-101, and at absolute easting 2e6 it exceeds it. Prose in a header
  rots; a pair of tests that fail in opposite directions does not.
- **Depth** — the near face wins from above, the far one from below.
- **Precedence** — a corner picks the vertex, a mid-edge the edge, zero tolerance disables each kind,
  an occluded far-side vertex is refused, and it stays refused when the occluding triangle's face id
  is corrupt.
- **Regressions for each bug review found** — a non-unit ray (three magnitudes) giving an identical
  index and an unchanged depth; a ray just outside the silhouette still reaching the edges; a curved
  edge walked as a curve.
- **Refusals** — a miss, a solid behind the cursor, a degenerate ray, a null out-param, mismatched
  buffers, an empty solid, a corrupt face id.

`tests/Ray3dTests.cpp` adds the primitive's 10 cases, including the shared-edge case that decides
whether a click on a silhouette selects anything, and a small triangle at state-plane coordinates.

Every positive case is paired with a negative one, so the suite cannot pass by returning a constant
in either direction.

**One test was wrong and was corrected before merge**, which is worth recording because it is the
same class of error as note 2. A characterization test asserted that a *radially* aimed pick at
absolute state-plane magnitude would show tangential error beyond REQ-101. It failed — correctly. For
a ray meeting the surface head-on the quantization displaces the hit almost entirely *along the ray*,
which the projection removes exactly. The claim only holds at oblique incidence, and the test now
uses a 60° approach and carries the explanation.

## Verification

- `./dev/test` — **1062/1062 pass** (42.6 s). The full suite, not just the new group: on this project
  a clean build has twice let a real behavioural break through.
- The refactored snap path is covered by the pre-existing `GoSurveySnapTests` and the
  `req313-solid-picked` headless transcript, both unchanged and green — which is the evidence that
  moving `CadSnap` onto the shared pick did not disturb snapping.
- All new `TEST_CASE` names are pure ASCII, checked deliberately — a non-ASCII name passes when run
  directly and **fails** under `ctest`, which has bitten this repo before.
- `cmake -S . -B build` re-run so the new sources and cases are discovered (`file(GLOB)` has no
  `CONFIGURE_DEPENDS`); confirmed present in `ctest -N`.
- Every edited file scanned for stray mid-line CR and for a CRLF-stored blob; both defects appeared
  during this task and were repaired. See **Technical debt DEBT-3**.
- `/code-review high` run before commit; its findings are what shaped this file's notes 2 and the
  design section above.

**No headless transcript, deliberately.** A transcript drives *commands*, and this slice adds none —
the pick is a query whose only new caller is `CadSnap`, which the existing transcript already
exercises. Transcript coverage for #148 belongs to the next slice, where there is a selection to
drive and the coordinate oracle (`EXPECT VERTEX` / `EXPECT ELEVATION`) has something to assert.

## Acceptance criteria status

Against **#148**'s list. This slice deliberately completes none of them on its own.

| # | criterion | status |
|---|---|---|
| 1 | Faces, edges and vertices each selectable and visibly highlighted | **partial** — the pick answers all three; selection state and highlight are the next slice |
| 2 | Sub-object selection does not interfere with whole-entity selection | **not started** — no selection state exists yet. Note the *snap* path did change, which is adjacent to this criterion and is why the snap tests and transcript are called out above |
| 3–8 | grips, gizmo, fillet, chamfer, undo, `.gs` round-trip | **not started** — later slices |

## Assumptions

- **The tolerances are the caller's to convert.** `Tolerance` is in the solid's units; the caller
  turns a pixel budget into world units at the pick depth, as `CadOffsetEntityPickTolWorld` does.
  Zero means "do not offer this kind", tested.
- **Two refinement passes are enough** for the edge's closest approach — an alternating projection
  between ray and curve, monotone, starting within a fraction of a degree. Not measured against a
  convergence criterion; the loop bound is one constant if a pathological edge ever needs more.
- **64 chords is enough to search a curve with no sweep parameter.** Chosen to match the order
  `CadSnap` already used for the same job rather than measured.

## Technical debt

- **DEBT-1 — the occlusion rule is per-solid.** A vertex hidden behind a *different* solid is still
  picked, because the pick sees one solid at a time. Correct resolution is at the caller, which will
  depth-order per-solid answers using `Pick::rayT` — a distance, for exactly this. Harmless until
  more than one solid can be picked at once, which arrives with the selection mode.
- **DEBT-2 — no face-interior containment test.** A pick trusts that a triangle tagged with a face id
  lies within that face's parametric span, which holds for everything `brep::Tessellate` produces.
  `ClosestPointOnSurface` is documented as deliberately unbounded for this reason.
- **DEBT-3 — this repo's `\ref` in a shell-driven edit is a trap.** Two doc comments in `CadSnap.cpp`
  acquired a stray carriage return mid-line when a `\ref` lost a backslash in shell marshaling,
  producing `CR + "ef"`. Caught by scanning for mid-line CR and repaired. Not a code defect, but the
  scan belongs in the routine for any edit to this repo made through a shell.
