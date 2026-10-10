# TASK-230 — `brep::Rotate` and `brep::Scale`: the kernel learns to place a solid

## Requirement authority

- **REQ-332** (this task's requirement — rigid rotation and uniform scale of a `brep::Solid`),
  drafted locally and implemented against per the standing arrangement for an unaccepted REQ.
- **ADR-046 amendment (m)** — the architectural decision that these join `brep::Translate` as
  whole-solid placement transforms owned by the kernel, rather than a per-field sweep at a call site.
- **REQ-328** — supplies `ray3d::RotateVectorAboutAxis` / `RotatePointAboutAxis`, and names solid
  rotation as explicit follow-on scope: *"its own kernel-shaped problem ... not a footnote to this
  requirement."* This is that follow-on.
- **GitHub issue #148 acceptance 4** — "the gizmo operates on a sub-object selection and matches the
  equivalent typed command". This is slice 1 of the three that criterion needs.
- User instruction, 2026-09-09: fix #148 criteria 3 and 4, landed as separate slices, starting with
  the criterion-4 chain `brep::Rotate` → ROTATE/SCALE → gizmo.

## Why this is slice 1 and not part of the command work

`REQ-060` records rotate and scale as **blocked, not deferred by preference**: ROTATE and SCALE are
plan-only and refuse solids, so a rotate handle would have no typed command to agree with. Tracing
that back gives one root cause, and it is in the kernel, not the command layer:

**`brep.hpp` declares exactly four operations that touch an existing solid** — `Translate`,
`PushPullFace`, `FilletEdge(s)`, `ChamferEdge(s)`. Grepping the header for rotation returns nothing
at all. There is no way to rotate or scale a solid, so `DropSolidsFromSelectionForTransform`
(`CadCommands.cpp:9144`, called from **9 sites**) is not a policy that can simply be deleted — it is
covering for a capability that does not exist.

So the kernel op comes first, alone, and the command wiring and the gizmo handles follow as their own
slices. That is the same order REQ-322 used: `brep::Translate` already existed, which is precisely why
3D MOVE was command wiring rather than a geometry problem.

## Approach

### The map of every place a coordinate hides

`Translate`'s own documentation states why it lives in the kernel: *"only this header knows every
place a coordinate hides in a `Solid` ... open-coded at a call site, adding a field to `Surface`
later would silently miss it, and a solid that half-moved is not a shape at all."* That map is the
specification for both new functions:

| # | Where | Under `Translate` | Under `Rotate` | Under `Scale` |
|---|---|---|---|---|
| 1 | `vertices[].p` | moves | rotates (point) | scales about base |
| 2 | `edges[].frame` (non-Line) | origin moves | origin rotates (point), **all three axes rotate (directions)** | origin scales, axes unchanged |
| 3 | `edges[].radius` / `radius2` | unchanged | unchanged | **`*= k`** |
| 4 | `edges[].isectSurfaces[]` | origin moves | full surface rotation | full surface scale |
| 5 | `faces[].surface.frame` | origin moves | origin rotates, **axes rotate** | origin scales, axes unchanged |
| 6 | `faces[].surface.radius/radius2/height` | unchanged | unchanged | **`*= k`** |
| 7 | `faces[].surface.patch` (NURBS) | `nurbs::Translate` | **`nurbs::Rotate`** (new) | **`nurbs::Scale`** (new) |
| 8 | `recipe.frame` | origin moves | origin rotates, axes rotate | origin scales, axes unchanged |
| 9 | `recipe.length/width/height/radius/radius2` | unchanged | unchanged | **`*= k`** |
| 10 | `recipe.path` (2D, in frame plane) | unchanged | unchanged (rides the frame) | **`*= k`** |

Two rows are the ones a per-field sweep at a call site would get wrong, and they are why this is a
kernel function:

- **Rows 2, 5, 8 — a frame is a point AND three directions.** The axis-point translation applies to
  positions only. This is exactly REQ-328's point/direction split, and the reason it ships two
  primitives rather than one with an ignored parameter: a plane's *centre* and a plane's *normal*
  share one angle and one axis and are still rotated by two different calls.
- **Rows 9, 10 — a uniform scale must resize the recipe.** The recipe is description and never truth
  (ADR-050 (f)); nothing in validity, mass properties or tessellation reads it. But a *wrong*
  description is still a defect: the recipe exists so the Properties panel can report "Radius 12",
  and a scaled solid whose recipe still says the old radius would report a number that is simply
  false. Under rotation these are lengths and do not move; under scale they must.

### Shapes, and the one deliberate divergence from `Translate`

**Both new functions refuse by name** (`bool` + `Problem* outWhy`), where `Translate` returns a
`Solid` directly. `Translate` can afford that because a delta cannot be degenerate — every `Vec3` is
a valid translation. Neither of these has that property:

- **A zero axis is not a no-op, it is a silent shrink.** Rodrigues' formula with `axisUnit = {0,0,0}`
  reduces to `v * cos(angle)` — a uniform scale by `cos θ`. Passing a non-unit axis of length `L`
  is worse still: the cross-product and projection terms scale differently, so the result is a
  *sheared* solid that still validates. `ray3d`'s primitive documents its axis as trusted, and that
  is right for `ray3d`, whose callers hold a stored plane normal or a UCS Z axis. It is wrong here,
  where the axis will come from a user's two picked points. Measured in the tests rather than
  asserted (see below).
- **A zero scale factor collapses the solid and a negative one mirrors it**, leaving left-handed
  frames that `ucs::IsRightHandedOrthonormal` would then reject somewhere far away from the command
  that caused it. Refused at the door, by name.

`Rotate` still cannot fail on a *valid* input — a rotation about a unit axis is an isometry — so its
only refusals are the degenerate-input ones plus the family's standard "should not happen" guard.

### New `Problem` values

`RotateAxisNotUnit`, `RotateResultInvalid`, `ScaleFactorNonPositive`, `ScaleResultInvalid`. A
non-finite angle, axis point or base point reuses the existing `NonFiniteParameter` rather than
adding a fifth.

### Files

- `src/util/nurbs.hpp` / `.cpp` — `Rotate`, `Scale` on a `Patch` (control points only; weights,
  knots and degrees are unchanged, so a rational patch scales and rotates exactly).
- `src/util/brep.hpp` / `.cpp` — `Rotate`, `Scale`, the four `Problem` values and their
  `ProblemText` sentences.
- `tests/RotateSolidTests.cpp` (new) + `CMakeLists.txt` — its own file, matching `PushPullTests` /
  `FilletEdgeTests` / `ChamferEdgeTests`, one per kernel operation.
- `spec/requirements.md` — REQ-332 and its traceability row.
- `spec/architecture.md` — ADR-046 amendment (m).

`brep.cpp`'s file-private `RotateAbout` (SWEEP/LOFT framing) is **not touched** — REQ-328 already
decided the two stay separate symbols, and nothing here changes that.

## Test approach

The acceptance is built on the two invariants that make these transforms checkable exactly rather
than within a tolerance, which is the same argument REQ-322 used for translation:

- **A rotation is an isometry**, so volume and surface area are unchanged *to the last digit the
  closed forms give*. Any drift is a defect, not a tolerance.
- **A uniform scale by `k`** multiplies volume by `k^3` and area by `k^2`, exactly.

Cases:

1. A box rotated 90° about world Z lands on hand-computed coordinates; volume and area unchanged.
2. **Four 90° turns are the identity** — every vertex back to its original coordinates.
3. A rotation about a **genuinely tilted** axis (not world-Z-parallel), off the origin, on a cylinder:
   volume/area unchanged, and the cylinder's own axis direction rotated by the same angle.
4. **Every frame stays right-handed orthonormal** after rotation (`ucs::IsRightHandedOrthonormal` on
   every face surface, every curved edge and the recipe) — the check that catches a mirrored or
   skewed frame, which is exactly what a non-unit axis produces.
5. A rotated solid still `Validate`s.
6. Scale by 2: volume `* 8`, area `* 4`, and the **recipe's** radius/height doubled.
7. Scale by `k` then by `1/k` returns the original coordinates.
8. A NURBS-faced solid survives both (control points moved, weights untouched).
9. **The measurement that justifies the refusal**: with the unit check removed, a zero axis shrinks
   the solid by `cos θ` and a non-unit axis shears it — recorded as numbers, so the refusal is shown
   to be necessary rather than merely tidy.
10. Refusals by name: non-unit axis, zero axis, NaN angle, zero factor, negative factor, NaN factor.
11. At **state-plane magnitude** (easting 2e6), the precondition the kernel documents elsewhere.

## Verification

- **build-project** — PASS. Release and Debug both clean. The only warnings in the Debug build are
  five pre-existing `C4005` macro redefinitions inside `third_party/imgui_test_engine/`, untouched
  by this work.
- **testing** — PASS. `ctest` **1373/1373**, up from 1359 — the 14 new cases and no regressions.
- **The tests were proven to bite, not assumed to.** Deleting the three axis lines from
  `RotateFrameInPlace` and rebuilding fails **3 of the 14 cases**, and the way it fails is the
  finding worth keeping:

  | rotation | result with the axes not rotated |
  |---|---|
  | 90° about **world Z** | `Validate` catches it — `brep::Rotate` refuses with `RotateResultInvalid` |
  | 0.7 rad about a **tilted** axis | **`Validate` returns `Ok`** — a closed, manifold, positive-volume solid reporting volume **1142.5693570452 against a true 1600, 28.6% wrong** |

  That split is the argument for the whole amendment: the defect a per-field sweep at a call site
  produces is invisible to `Validate` in exactly the general case, and visible only in the
  axis-aligned one a hand-written test would reach for first. Restored and re-verified green.
- **architecture-review** — PASS. Both functions are pure kernel additions in the layer that already
  owns `Translate`; nothing in `src/commands/` or `src/ui/` is touched by this slice, and no existing
  call site changes behaviour. `brep.cpp`'s file-private `RotateAbout` is untouched, per REQ-328.
- **code-review** — self-run. One finding, fixed: both functions guarded a null `out` with
  `Fail(Problem::NonFiniteParameter, outWhy)`, which reports a user-facing reason that misdescribes a
  caller bug. `brep.cpp` already has an explicit and dominant convention for this — five sites
  reading `return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left
  alone` — and both now match it.

## Technical debt

- **DEBT-1 — `Rotate` and `Scale` both call `Validate` on the result, and a live gizmo drag will call
  them per frame.** Correct and consistent with the operation family (push/pull, fillet and chamfer
  all validate), and right for this slice, where every caller is a one-shot command. But `Validate`
  includes a volume integral over every analytic face, and slice 3's drag loop is a different cost
  profile against REQ-100. Flagged here so it is a **measured** decision in slice 3 rather than a
  silently inherited one — the answer may be to validate on commit and not on each drag frame.
- **DEBT-2 — no `.gs` round-trip case in this slice.** Not an omission: a rotated or scaled solid is
  an ordinary solid, and `GsIo` writes topology rather than a recipe, so there is no new format
  surface. #148 acceptance 8 is already covered for edited solids by the REQ-319 / REQ-323 / REQ-331
  transcripts, and slice 2 adds the command-level round trip where a user can actually produce one.

## Out of scope, by name

- **The ROTATE / SCALE commands and `DropSolidsFromSelectionForTransform`** — slice 2.
- **The gizmo's rotate and scale handles** — slice 3, and it needs slice 2 first for the same reason
  REQ-060 gives: a handle must have a typed command to agree with.
- **Non-uniform scale.** A sphere scaled unevenly is an ellipsoid, which `SurfaceKind` cannot
  represent. Refusing the whole operation is not required here because the signature only offers one
  factor — there is no non-uniform request to refuse.
- **`brep::Mirror`.** A reflection is not a rotation and produces left-handed frames that every
  surface normal and the volume integrand would have to be reconsidered against. MIRROR keeps
  refusing solids (REQ-322 item 6), unchanged.
