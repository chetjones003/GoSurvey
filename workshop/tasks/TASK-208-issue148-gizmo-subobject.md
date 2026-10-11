# TASK-208 — the gizmo on a sub-object selection (GitHub issue #148 acceptance 4, Phase 5 slice 4c)

## Requirement authority

GitHub issue #148 (Phase 5 of #120), acceptance 4:

> The gizmo operates on a sub-object selection and matches the equivalent typed command within
> REQ-101.

Built on REQ-060 (the gizmo, slice 4b / TASK-206), REQ-318 (the sub-object selection) and REQ-319
(push/pull, TASK-207 — the "equivalent typed command" here is `PRESSPULL`). D-2026-09-05-b records
the four choices.

## What was built

| file | change |
|---|---|
| `spec/project.md` | D-2026-09-05-b |
| `spec/requirements.md` | REQ-060 status extended to name the face mode |
| `src/commands/CadCommands.{hpp,cpp}` | `CadGizmoMode` + `CadGizmoModeFor` / `CadGizmoSubObjectFace` / `CadGizmoAxisCountFor`; face branches in the anchor, the axis and the commit; `CadSubObjectGripAxisDistance` deleted |
| `src/util/gizmooverlay.hpp` | `faceMode`, so the single handle can take its own colour |
| `src/viewport/TransformPreview.{hpp,cpp}` | `BuildSubObjectGripGeometry` → `BuildSubObjectFaceGhost`, driven by the gizmo's state |
| `src/render/ViewportRenderer.cpp` | the face handle draws PURPLE, not the X handle's red |
| `src/ui/CadUi.cpp` | the face grip's own arm / commit / drag paths removed; the gizmo covers them |
| `src/app/main.cpp` | ESC cancels a gizmo drag; the face ghost rides the preview channel |
| `tests/headless/HeadlessDriver.cpp` | `EXPECT GIZMOAXES` |
| `tests/SubObjectSelectionTests.cpp` | 3 new cases; the skew-solve cases re-pointed at `CadAxisDragParam` |
| `tests/headless/transcripts/req148-gizmo-subobject.txt` | new — 56 steps |

## The four choices

1. **One handle, not three.** `brep::PushPullFace` takes a distance along the face normal and
   nothing else. A handle along UCS X on a face whose normal is Z would name a direction the kernel
   cannot move the face in, and drawing it only to refuse the drag is worse than not drawing it.
   `CadGizmoAxisCountFor` returns 1 here, and the transcript asserts that number — a face gizmo that
   quietly grew a second handle is exactly the regression this shape invites.

2. **The commit is `CadApplyPushPull`, the function typed `PRESSPULL` calls.** The acceptance line is
   then true by construction, the same way slice 4b tied the entity gizmo to typed MOVE. The
   one-line consequence: the drag inherits PRESSPULL's undo step, its re-pointing of every
   sub-object reference that named the replaced solid, and the kernel's own sentence on a refusal.
   None of those should be said twice.

3. **An edge or a vertex gets no gizmo.** There is no kernel operation that moves either, so a
   handle would advertise an edit that cannot happen. Issue #148 criterion 3 asks for grips on all
   three kinds; two thirds are unbuilt, and the gizmo says so by not appearing rather than by
   refusing after the gesture.

4. **The face gizmo replaced the face grip** TASK-207 shipped days earlier. Two handles for one
   operation is worse than either. The grip's kernel path, its anchor/axis resolver
   (`CadSubObjectFaceGrip`) and its ghost survive unchanged; only its own handle, hit test and state
   are gone. That also collapsed `CadSubObjectGripAxisDistance` into `CadAxisDragParam` — the same
   skew-line arithmetic under two names, written on branches that could not see each other
   (TASK-206's DEBT-2, closed).

**The mode is derived, never stored.** The two selections are already mutually exclusive
(D-2026-09-04-a); a stored mode would be a third thing that can disagree with them.

## Test approach

The transcript's central assertion is the acceptance line itself: the same box, pushed 12 by a gizmo
drag and by `PRESSPULL 12`, reports identical mass properties. The drag never states a distance — it
aims two camera rays at points on the face normal, 5 and 17 units along it, and the skew-line solve
produces the 12. That is the same construction slice 4b used, and for the same reason: a verb that
handed the command layer an offset would assert only that two ways of calling one function agree.

The unit cases own the mode DERIVATION, which a transcript can only see two numbers of: one face →
`SubObjectFace` with one handle on the centroid along the normal; an edge or a vertex → `None`; two
faces → `None` (no single normal, and PRESSPULL refuses two faces anyway); an entity selection →
`Entity` with three. Plus the equality asserted **vertex for vertex**, not by volume — a solid that
moved the right amount the wrong way can share a volume with one that did not — and the capture
rule: a drag armed on a face still applies to that face after the selection is cleared mid-drag.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean tree.
- **testing** — PASS. `ctest` **1173/1173**.
- **architecture-review** — PASS. The mode, the anchor, the axis and the commit are all in the
  command layer; the UI still keeps only the cursor ray and the pixel aperture. `CadUi.cpp` got
  *smaller* — the grip's ~45-line arm/hit-test block is gone.
- **code-review** — self-run. It found the task-number collision below, and one real ordering
  question: the gizmo must take the click **before** the plain-click clear, or a click on the handle
  would clear the very face the handle belongs to.
- **performance-review** — PASS. `CadGizmoModeFor` walks the sub-object selection, which holds at
  most a handful of entries, and the face branch resolves one face's centroid. The entity branch is
  unchanged.

## A defect found on the way, fixed here

**Two tasks were numbered TASK-201.** `TASK-201-issue148-pushpull-face.md` (the push/pull stack, PR
#291) was numbered on a branch cut from the merge that had *already* landed
`TASK-201-issue259-closed-sweep-path.md`. Renamed to **TASK-207**, with the three D-2026-09-04
decisions that cite it updated. It surfaces here because this is the first branch to hold both.

## Not covered by test, stated plainly

- **The drawing.** The purple face handle and the arrow are asserted by eye; what is asserted
  mechanically is the decision to draw and how many handles (`EXPECT GIZMO`, `EXPECT GIZMOAXES`).
- **A curved face.** `CadSubObjectFaceGrip` resolves only planar faces, so a cylinder wall selected
  as a sub-object gets no gizmo even though `brep::PushPullFace` can now push one by radius
  (D-2026-09-04-e). Debt, below.

## Technical debt

- **DEBT-1 — a curved face has no gizmo.** The kernel gained a cylinder-wall push (radius) in
  TASK-207, but `CadSubObjectFaceGrip` still refuses anything but a plane, so only the typed
  `PRESSPULL` can reach it. The handle would need an anchor and a direction that mean something on a
  surface whose normal varies — probably the radial direction at the picked point, which is a
  decision rather than a fill-in.
- **DEBT-2 — no plane handles, inherited from TASK-206.** Entity mode still offers only three axes.
- **DEBT-3 — edge and vertex grips.** Issue #148 criterion 3's other two thirds. They need kernel
  operations that do not exist, so they are a requirement of their own, not a gizmo change.
- **DEBT-4 — rotate and scale**, inherited from TASK-206 and still blocked on plan-only
  ROTATE/SCALE (REQ-322 item 6).
