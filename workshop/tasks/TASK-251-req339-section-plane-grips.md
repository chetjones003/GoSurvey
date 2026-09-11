# TASK-251 — Grabbing the section plane: slide, flip, resize

- Type:    feat (new requirement + ADR extension)
- Status:  review
- Opened:  2026-09-11
- Owner:   Workshop
- GitHub:  #479 slice 2 of 4 — acceptance 5, 6, 7 and the selection half of 4.

## 1. Authority

- **REQ-339** (new, accepted 2026-09-11, D-2026-09-11-c) — the requirement this delivers.
- **ADR-058** extended with **(g)** the frozen drag axis and **(h)** selecting a view state by a
  bool rather than through `selection`.
- **REQ-338 / D-2026-09-11-b** — the plane this manipulates. Its revision note is amended: the "no
  grips" increment is now delivered, and the section line has moved.
- **REQ-060 / ADR-025** — the translate gizmo, whose grab/update/commit shape this follows and whose
  `CadAxisDragParam` it reuses. Reused, not copied: the skew-line solve has one implementation.
- **REQ-318 / D-2026-09-04-a** — the argument for a separate selection store, applied a second time.
- **REQ-201** — refuse with a stated reason; here, sighting straight down a drag axis.
- Constraints: CON-06 smallest change.

## 2. Problem

The user's request, 2026-09-11, with four AutoCAD screenshots:

> add the line in the center of it like autocad has. i want the sectionplane to be able to grab the
> plane and move it orthographicaly on that object from the face. moving the plane will cut off one
> side of the object, i want another feature in the plane like autocad has to be able to flip the
> plane and view the other side ... there are also the horizontal and vertical grab points that can
> be used to stretch the sectionplane

Four things, and REQ-338 delivered none of them: the plane could only be aimed by typing an offset,
and its section line sat on the lowest edge where it added nothing.

## 3. Approach

`SectionClip.hpp` keeps everything that can be stated without a window (ADR-002), which by now is
the whole geometry of the feature: `SectionClipPlaneBasis` (factored out, so the rectangle, the
hatch, the handles and the handle PICK cannot disagree about which way u points), `SectionPlaneExtent`,
`SectionPlaneGripsFor`, `SectionPlaneExtentFromQuad`.

The command layer owns interaction: `SubmitSectionPlaneClick` / `UpdateSectionPlaneGripDrag` /
`CancelSectionPlaneGripDrag`, in the same grab-update-commit shape as the gizmo, so a test can drive
a drag without a mouse.

**`CadSectionClipIndicator` is now the single source of the rectangle**, called by the renderer and
by the pick. `main.cpp` had a model-bounds walk inline; that is what made two copies possible, and
two copies of "where is the rectangle?" is how a user clicks the plane they can see and grabs
nothing.

## 4. What the tests found

Both of these were found by tests failing, not by reading the code, and neither is visible to a test
that calls the drag once.

**BUG-1 — the drag axis collapsed after one frame.** `UpdateSectionPlaneGripDrag` re-derived the
axis from `CadSectionPlaneGrips`, i.e. from the handle's CURRENT position. But the handle sits on
the plane, and the drag moves the plane — so frame 2 measured its delta from where the plane had
already got to, the delta came out zero, and the plane snapped back to the grab point. A held cursor
would have flicked it back; a moving one would have oscillated.

Caught by the assertion that the click which *drops* a drag changes nothing: it reported `0.0` where
`-5.0` was expected. The axis is now recorded at the grab (`sectionPlaneGripAnchor` /
`sectionPlaneGripAxis`), and the guard is **five no-op frames with the cursor still** — which is the
assertion that actually describes the failure, rather than the one that happened to expose it.

**BUG-2 — a stretch moved the grabbed edge twice as far as the cursor.** Applying the full delta to
both the half-size and the centre moves that edge by `2 * delta`. Reported as `12.0 == Approx(6.0)`.
Moving one edge while the opposite stays put changes the half-size by *half* the delta.

**A third thing the tests had to be corrected for, which is worth recording as a test-design note
rather than a bug:** the first fixtures aimed their rays *across* a horizontal plane, so the ray lay
IN the plane and grazed all six handles at once — the pick answered with whichever was nearest the
sight line rather than the one aimed at. Rays at handles are now oblique, and the reason is written
where the helper is defined. Straight down is no better: the Move handle drags along the plane's
normal, and sighting down an axis is the one case `CadAxisDragParam` refuses outright.

## 5. What was built

| file | change |
|---|---|
| `src/render/SectionClip.hpp` | `SectionClipPlaneBasis` factored out; `SectionPlaneExtent`; extent override in `SectionClipIndicatorQuad`; centre section line; `SectionPlaneGrip`, `SectionPlaneGrips`, `SectionPlaneGripsFor`, `SectionPlaneExtentFromQuad`, `kSectionPlaneMinHalfExtent` |
| `src/commands/CadCommands.hpp` | the selection flag, the stored extent, the frozen drag axis; ten declarations |
| `src/commands/CadCommands.cpp` | `CadSectionClipPlane`, `CadSectionClipIndicator`, `CadSectionPlaneGrips`, `PickSectionPlaneGrip`, `PickSectionPlaneQuad`, `SubmitSectionPlaneClick`, `UpdateSectionPlaneGripDrag/Hover`, `CancelSectionPlaneGripDrag`, `ToggleSectionClipFlip`; extent reset on re-aim; `ClearCadSelection` deselects the plane |
| `src/render/ViewportRenderer.{hpp,cpp}` | the handles, drawn in the plane's own axes, larger when hovered or grabbed |
| `src/ui/CadUi.cpp` | the click, after the gizmo and before the ordinary pick; the live drag and the pre-highlight beside the gizmo's |
| `src/app/main.cpp` | the inline bounds walk removed; everything comes from the command layer |

## 6. Verification

**Full suite 1506/1506**, up from 1489.

| | |
|---|---|
| `SectionClipTests` `[sectionplane]` | 14 cases / 485 assertions |
| `SubObjectSelectionTests` `[sectionplanegrip]` | 10 cases / 97 assertions |

Proven to bite: reinstating the live-derived drag axis fails the held-cursor guard on frame 1.
BUG-2's fix is proven by the failure that produced it (`12.0` where `6.0` was expected).

`ToggleSectionClipFlip` is shared between the typed `SECTIONCLIP FLIP` and the flip handle, so the
two cannot drift into meaning different things.

## 7. Assumptions and debt

- **DEBT-1.** Manipulating the plane makes **no undo entry**. Consistent with it being a view state
  — there is no geometry to restore — but it means `UNDO` will not step a slide back. Written into
  REQ-339 rather than left to be discovered.
- **DEBT-2.** No automated test drives a real mouse drag, or asserts that any handle is drawn. There
  is no GL context in the test suite, and the devshell's engine drives items rather than pixels.
  Every number here is geometry; the pixels are the user's check.
- **DEBT-3.** The section line has no direction arrows, so the plane shows which half is kept only
  by what has disappeared. The arrows belong with the ribbon slice.
- **ASSUMPTION-1.** The flip handle at a quarter of the way along the section line is far enough
  from the centre handle not to be grabbed by accident at ordinary zooms. Both are picked with the
  same aperture, so at a sufficiently distant zoom the two could compete; not observed, and the
  nearest-handle rule makes the outcome stable rather than random.

## 8. Result

**PASS** for #479 acceptance 5, 6, 7 and the selection half of 4.

Remaining for later slices: the plane as a real entity (Properties, `.gs`, undo), direction arrows,
and the contextual ribbon.
