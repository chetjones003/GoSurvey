# TASK-232 — the gizmo gains rotate and scale handles

## Requirement authority

- **REQ-060**, whose first two acceptance bullets name rotate and scale directly: *"translate,
  rotate and scale each move the selection as displayed, and one Ctrl+Z restores the prior state in
  a single step"*, and *"a gizmo drag and the equivalent typed command produce coordinates agreeing
  within REQ-101"*. Recorded as **blocked** until 2026-09-09; unblocked by REQ-332.
- **REQ-332 increment 2 (TASK-231)**, which made typed ROTATE and SCALE transform a solid — the
  typed commands these handles must agree with.
- **GitHub issue #148 acceptance 4.** Slice 3 of 3, and the one that closes the criterion.

## The rule that decides the whole design

REQ-060's second bullet is the constraint everything else follows from: **a handle may only exist
where an equivalent typed command exists.** Slice 4b's face gizmo already applied it — one handle
along the face normal, not three, because `brep::PushPullFace` takes a distance along the normal and
nothing else, and *"a handle along UCS X on a face whose normal is Z would advertise a move the
kernel cannot make"*.

Applied to rotate and scale, that rule gives two answers that look like under-delivery and are not:

### Rotate gets ONE ring, about the active UCS Z

Not three. **Typed ROTATE is UCS-Z-only** — REQ-329's own traceability row says so in as many words:
*"ROTATE is UCS-Z-only (REQ-328 primitive), a full ROTATE3D is a separate future issue."* There is no
typed command that turns a selection about UCS X or Y, so rings on those axes would have nothing to
agree with, and REQ-060's second bullet could not be satisfied for them. Drawing them and then
refusing the drag is worse than not drawing them.

### Scale gets ONE handle, and it is uniform

Not three. Typed SCALE is uniform on every axis (REQ-329 increment 3) and `brep::Scale` is uniform
because the representation has no ellipsoid to store an unevenly scaled sphere in (REQ-332 item 7).
A per-axis scale handle would advertise a shape the program cannot hold.

### Rotate and scale do not apply to a sub-object FACE

`CadGizmoModeFor` returns `SubObjectFace` only under Translate. No kernel operation rotates or scales
a single face, so under Rotate or Scale a face selection gets **no gizmo at all** — the same answer an
edge or a vertex already gets, for the same reason.

## The one thing that is stored, and why that does not contradict the existing note

`CadGizmoMode` is documented as derived and *"never stored, because the two selections are already
mutually exclusive ... a stored mode would be a third thing that can disagree with them."* That
argument is about the **subject** — which the selection already determines.

The **operation** has no such derivation: nothing in a selection says whether the user wants to move,
turn or resize it. So `CadGizmoOp { Translate, Rotate, Scale }` is stored on `AppCommandState` and
chosen by the user, and the two questions keep their different answers for their different reasons.
Chosen with a new `GIZMO MOVE|ROTATE|SCALE` command, so the app and a transcript drive it the same way.

## Agreement by construction — the extraction this needs

Typed ROTATE and SCALE each do more than call one function:

- ROTATE dispatches on `CadWorkPlaneIsWorldXy` between `ApplyRotationToSelection` (world Z) and
  `RotateSelectionInPlaceAboutAxis` (the UCS Z axis);
- SCALE calls `ApplyScaleToSelection` and then, **only under a tilted UCS**,
  `ScaleSelectionZAboutBase`.

A gizmo that called only the inner functions would agree with half of each command. So both
dispatches are lifted into `ApplyRotationAboutUcsZ` and `ApplyUniformScaleAboutBase`, and
`FinishRotateCommand` / `FinishScaleCommand` and the gizmo all call those. This discharges TASK-231's
DEBT-2.

## A documentation correction this slice must make

`CadGizmoAnchorWorld` says its precision *"does not affect any move — a drag distance is the change
in the axis parameter between the grab and the drop, so the anchor appears in both terms and
cancels, and it decides only where the handles are DRAWN."*

**That stops being true here.** For rotate and scale the anchor is the PIVOT and the BASE — it does
not cancel, it decides the answer. It remains correct to use it, because what REQ-060 requires is
agreement with *the equivalent typed command*, and the equivalent command is ROTATE/SCALE **about
that same anchor**; the anchor must therefore be deterministic, not accurate. But the note as written
would mislead the next reader into thinking the anchor is cosmetic, so it is corrected.

## The two new gesture solves

- **`CadAxisDragAngle`** — intersect the ray with the plane through the anchor whose normal is the
  axis, and take `atan2` of the hit in that plane's own frame. Refused when the ray is near-parallel
  to the plane (no hit) or the hit is within a tolerance of the anchor (no direction to measure), for
  the same reason `CadAxisDragParam` refuses a handle sighted end-on: the honest answer to "this
  gesture has no meaning" is to decline it, not to return a huge number from a near-singular divide.
- **Scale** reuses `CadAxisDragParam` along the handle direction and takes the RATIO of the drop's
  parameter to the grab's. Refused when the grab parameter is ~0 (no baseline) or the ratio is
  non-positive (dragging through the anchor and out the far side is a mirror, which is its own
  operation — REQ-332 item 7).

`gizmoDragDistance` therefore carries the drag's value **in the operation's own units**: a distance,
an angle in radians, or a factor. One field rather than three that could disagree; documented at the
field.

## Files

- `src/commands/CadCommands.{hpp,cpp}` — `CadGizmoOp` + state, the two extractions, `CadAxisDragAngle`,
  the op-aware handle count / axis / pick / drag / commit, the `GIZMO` command.
- `src/util/gizmooverlay.hpp`, `src/viewport/TransformPreview.cpp`, `src/render/ViewportRenderer.cpp`
  — the ring and the uniform handle, and their colours.
- `tests/GizmoRotateScaleTests.cpp` (new) + `CMakeLists.txt`.
- `tests/headless/transcripts/req060-gizmo-rotate-scale.txt` (new).
- `spec/requirements.md` — REQ-060 status and traceability; REQ-332 increment 3.

## Test approach

The agreement is the acceptance, so it is asserted directly rather than described:

1. **A gizmo rotate drag and typed ROTATE about the same base produce the same coordinates** — the
   central claim, run in plan and under a tilted UCS.
2. **A gizmo scale drag and typed SCALE with the same base and factor agree**, including on a solid.
3. One Ctrl+Z restores the prior state in a single step, for each operation.
4. No gizmo when the selection is empty; **no gizmo under Rotate or Scale with a face selected**.
5. Handle counts: 3 translate-entity, 1 rotate, 1 scale, 1 translate-face, 0 rotate/scale-face.
6. Both new refusals: a ray parallel to the rotation plane, and a scale ratio that is non-positive.
7. The op survives nothing it should not — it is a user setting, not per-selection state.

## Verification

- **build-project** — PASS, Release and Debug, no new warnings.
- **testing** — PASS. `ctest` **1384/1384** (1374 + 9 unit cases + 1 transcript).
- **The agreement test was proven to bite, and HOW it fails is the finding.** Wiring the rotate
  commit to `ApplyRotationToSelection` — the inner half — instead of `ApplyRotationAboutUcsZ`:

  | | result |
  |---|---|
  | the 21 World-UCS gizmo cases | **all pass** |
  | the tilted-UCS agreement case | **fails** |

  That is the entire argument for the extraction, measured rather than asserted: a gizmo wired to the
  plan-view function looks completely correct until someone tilts the UCS. The World-UCS agreement
  case, written first, could not have caught it — which is why the tilted one exists.
- **architecture-review** — PASS. The overlay stays a plain data carrier between layers that do not
  include each other's headers (`soloOp` is an `int`, not the enum, for that reason). No new state
  beyond the one stored setting, and its justification against the existing "never stored" note is
  written at the enum.
- **code-review** — self-run. Two things fixed while writing it: `CadGizmoAxisWorld`'s fallback
  branch still indexed by the caller's `axis` after the operation remaps it, which would have
  returned the wrong unit vector for a degenerate UCS; and `PickGizmoAxis` was hit-testing the rotate
  ring as a SEGMENT along the UCS Z, so the widget would have been ungrabbable everywhere it is drawn
  and grabbable along a line it never occupies.

## Technical debt

- **DEBT-1 — no ribbon or keyboard affordance for `GIZMO`.** The command works and is what the
  transcript drives, but a user has to type it. A gizmo-mode control belongs on the ribbon beside the
  other viewport widgets, and that is UI-layout work rather than this slice's geometry.
- **DEBT-2 — rings on UCS X and Y, and rotate/scale for a COPY, are still absent.** Both wait on a
  typed ROTATE3D and on solid duplication respectively (TASK-231 DEBT-1), not on anything here.
  Named in REQ-060 so the absence is a stated boundary rather than a gap.

## Landing note

Third in the chain behind TASK-230 (PR #452) and TASK-231, neither merged. Developed locally on top
of both; the PR is held until they land and this is rebased onto `beta`.
