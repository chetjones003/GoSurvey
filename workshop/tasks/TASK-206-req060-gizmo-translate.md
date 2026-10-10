# TASK-206 — the TRANSLATE gizmo (REQ-060, GitHub issue #148 Phase 5 slice 4b)

## Requirement authority

REQ-060 (accepted 2026-08-11), whose acceptance is three bullets:

1. translate, rotate and scale each move the selection as displayed, and one Ctrl+Z restores the
   prior state in a single step;
2. a gizmo drag and the equivalent typed command produce coordinates agreeing within REQ-101;
3. no gizmo is drawn when the selection is empty.

**This slice delivers TRANSLATE only**, and the reason is recorded rather than left to inference —
see "Scope" below. D-2026-09-04-g records the four design choices.

## What was built

| file | change |
|---|---|
| `spec/requirements.md` | REQ-060 status, revision and traceability row |
| `spec/project.md` | D-2026-09-04-g |
| `src/commands/CadCommands.{hpp,cpp}` | the gizmo's state and its seven functions; `ApplyTranslationToSelection` promoted to public API |
| `src/util/gizmooverlay.hpp` | new — `CadGizmoOverlay` |
| `src/viewport/TransformPreview.{hpp,cpp}` | `BuildGizmoOverlay`, `BuildGizmoDragGhost` |
| `src/render/ViewportRenderer.{hpp,cpp}` | one new channel, drawn last and never occluded |
| `src/app/main.cpp` | builds the overlay; the drag ghost rides the existing preview channel |
| `src/ui/CadUi.cpp` | hover pre-highlight, first refusal on the click, right-click cancel |
| `tests/headless/HeadlessDriver.cpp` | the `GIZMO` verb; `EXPECT GIZMO` and `EXPECT GIZMOAXIS` |
| `tests/GizmoTranslateTests.cpp` | new — 8 cases |
| `tests/headless/transcripts/req060-gizmo-translate.txt` | new — 75 steps |

## The five choices worth recording

1. **The commit goes through `ApplyTranslationToSelection`, which is what typed MOVE calls.** Bullet
   2 is then true by construction rather than because two implementations agree today. That is the
   whole shape of the feature: `CommitGizmoDrag` is eleven lines, and ten of them are the undo
   snapshot and the log.

   It cost one thing: `ApplyTranslationToSelection` was a definition private to `CadCommands.cpp`'s
   anonymous namespace, which says *"nothing outside this file may depend on this"* — the opposite
   of what REQ-060 needs. The namespace is closed around that one function and reopened after it,
   with the reason written at the seam.

2. **The handles follow the ACTIVE UCS.** The grid, ORTHO and coordinate entry all take their
   directions from it (REQ-154); a gizmo pointing elsewhere would be the only thing in the viewport
   disagreeing with the grid drawn under it. In the World UCS the two are identical, so no existing
   drawing sees a change. A unit case asserts the rotated frame, because "identical in the default
   case" is exactly the kind of thing that stays untested until it is wrong.

3. **Click-arm, click-commit.** What every other grip in this viewport does
   (`mtextGripMoveActive`, `dimGripMoveActive`) — and the only shape a transcript can drive, a
   headless run having no mouse to hold down. That is what makes REQ-060 assertable rather than
   "verified manually", which is what its traceability row said for three weeks.

4. **A handle sighted end-on is refused.** Looking down the Z handle in plan view, every point of it
   projects to one pixel: no distance is being expressed. The near-singular divide the alternative
   takes returns an enormous number, which reads on screen as the selection flying off — a wrong
   answer that looks like a crash. The transcript states this by orbiting before it touches Z, and a
   unit case asserts the refusal directly, including that the out-parameter is left untouched so a
   caller ignoring the `bool` cannot drift silently.

5. **The anchor's precision cannot affect any move.** The drag distance is the *change* in the axis
   parameter between grab and drop, so the anchor appears in both terms and cancels. That is why
   conservative per-type bounds (an arc bounded by its full circle, a table by its insertion point)
   are good enough, and why this is deliberately **not** `ComputeSelectionCentroidWorld` — that
   answers ROTATE's different question (a pivot, in plan, over ROTATE's own type set), and widening
   it would change where ROTATE and ARRAY turn things about.

## Scope held deliberately narrow

**Rotate and scale are not implemented, and they are blocked rather than deferred by preference.**
ROTATE and SCALE are still plan-only and still refuse solids (REQ-322 item 6), so a rotate handle
would have no typed command to agree with — the same gap D-2026-09-04-f found and only half closed.
Lifting them means turning every entity's stored frame about an arbitrary axis: the work REQ-312
needed for one tilted arc, multiplied across every entity type. That is its own requirement.

REQ-060 therefore stays **accepted, partly implemented**, and its status says which part.

## Test approach

**The `GIZMO` verb aims a camera RAY at a world point; it never hands the command layer a
distance.** A verb that passed an offset straight through would assert that two ways of calling one
function agree, which is not a fact about the product. Aiming and letting the skew-line solve
produce the number tests the pick, the projection and the transform together — the chain the user's
mouse goes through. It is the same construction `SUBOBJECT` uses, for the same reason.

The transcript's numbers are exact by construction rather than by tolerance: every GRAB and DROP
point lies **on** the axis it names, so the nearest point of that axis to the ray is that point
whatever the camera is doing, and the drag is the difference of two axis positions.

Two things the transcript found, recorded because a later reader will hit them:

- **`UNDO` drops the selection**, so the gizmo is gone after one. The cancel case therefore runs
  *before* the drag it accompanies, and the block ends by asserting `SELECTED 0` / `GIZMO 0` rather
  than trying to grab again.
- **A primitive's frame origin is the centre of its BASE**, so `BOX 0,0 20 10 8` spans x -10..10,
  y -5..5, z 0..8 — not 0..20 by 0..10. The first draft of the solid case grabbed at a point in
  empty space and the verb failed loudly, which is what it is for.

The solid's move is asserted by **mass properties**, not coordinates: a translation is an isometry,
so volume and area must be unchanged to the last digit the closed forms give. That is the check
that catches a solid which moved only some of its parts — such a solid still passes `brep::Validate`,
which never compares a face's surface against its own boundary. It is paired with a save/reload and
a fence at each position, because mass properties alone would also pass on a solid that never moved.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean tree.
- **testing** — PASS. `ctest` **1162/1162**, including 8 new unit cases and the new transcript.
- **architecture-review** — PASS. The gizmo's meaning is in the command layer, where a transcript
  can reach it; the UI keeps only the two things the command layer cannot have (the cursor ray and
  the pixel aperture). That is the split REQ-318's sub-object pick already uses.
- **code-review** — self-run. It caught one real thing: the first draft placed the whole
  implementation inside `CadCommands.cpp`'s anonymous namespace, giving every function internal
  linkage and an ambiguous-overload error against its own header declarations. Fixed by moving the
  block below the namespace's close rather than by removing the declarations.
- **performance-review** — PASS by inspection. Per frame: one bounding-box walk of the selection and
  three ray-to-segment tests. Deliberately outside the `runHoverPick` gate the entity picks sit
  behind — this is not a walk of the drawing, and an armed drag has to follow the cursor every frame
  or it is not direct manipulation.

## Not covered by test, stated plainly

- **The drawing.** `BuildGizmoOverlay`'s arrowheads and the renderer's colour choices are asserted
  by eye. What *is* asserted is the decision to draw at all (`EXPECT GIZMO`), which is the acceptance
  bullet.
- **Rotate and scale.** Not built — see Scope.

## Technical debt

- **DEBT-1 — no plane handles.** Only the three axes; a drag in a plane (the XY square a gizmo
  usually offers between two axes) is not there. It needs a second kind of hit test and a
  ray-to-plane solve rather than a skew-line one, and no requirement asks for it yet.
- **DEBT-2 — `CadAxisDragParam` will meet a twin.** PR #291's face-grip drag carries
  `CadSubObjectGripAxisDistance`, which is the same skew-line solve under another name. When both
  land, one must go — the branches were cut independently and neither could see the other.
- **DEBT-3 — no numeric entry during a drag.** The gizmo cannot be given a typed distance mid-drag
  the way AutoCAD's can. The typed MOVE covers the case; this is convenience, not capability.
- **DEBT-4 — inherited from REQ-322.** COPY is still 2D, and ROTATE/SCALE/MIRROR/ARRAY/STRETCH still
  refuse solids. DEBT-2 of TASK-209 is now on the critical path: it is what blocks slice 4c.
