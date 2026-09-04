# TASK-200 — holding Ctrl shows what a click would take (REQ-318 item 14, issue #148)

## Origin

Asked for by the user immediately after TASK-199 shipped, with an AutoCAD rollover screenshot:
*"when holding ctrl make it so that a preview is shown for the object in the pick box … also add a
subtle highlight color for the object / object face that is about to be selected."*

**This reverses TASK-199's DEBT-1**, which read:

> **DEBT-1 — no hover pre-highlight for sub-objects.** Out of scope by decision. Without it the user
> learns what a Ctrl+click selected only after making it. Running the pick every frame on hover is a
> REQ-100 question that wants measuring, and the broad-phase reject `solidpick` already has is the
> reason to expect it is affordable rather than the evidence that it is.

The deferral's real content was the **cost**, not the value — and the user asking for it within
minutes settles the value half. Reversed deliberately and recorded as **D-2026-09-04-b**, because a
debt entry that is silently un-deferred is a debt list nobody can trust.

## Requirement authority

- **REQ-318** gains statement **item 14** and six acceptance bullets. Increment 2 already owned "the
  highlight treatment"; this states the *pre*-highlight and the readout, which it did not.
- **REQ-089** (the surface rollover) is the pattern the readout follows, dwell included — it is the
  requirement that established "a rest, not a cursor-follower" for this kind of panel.
- **REQ-036 / REQ-039** already require a whole-entity hover pre-highlight. This is the same idea for
  the weaker-to-see case.
- **REQ-100** is what the deferral was about, and item 14 states the gate as part of the requirement
  rather than as an implementation note.

## Why it is not decoration

Precedence is vertex → edge → face, each within a screen-derived tolerance (REQ-318 item 3). On a box
corner **a few pixels decide between three different answers**, and that distance is not something a
user can measure by eye. Without a pre-highlight the only way to discover what a click will take is
to click and read the command line — every pick becomes a guess followed by a correction. TASK-199's
own transcript had to state, as a documented behaviour, that a plain click in the middle of a face
selects nothing while a Ctrl click selects the face; that asymmetry is exactly what a pre-highlight
makes obvious and its absence makes baffling.

## The cost question, answered rather than re-deferred

The pre-highlight pick runs behind **`HoverPickGateShouldRun`** — the movement tolerance, minimum
interval (~30 Hz) and 0.25 s idle ceiling the entity hover pick already uses — and inherits
`solidpick`'s broad-phase bounds reject. So it is not a second per-frame walk beside the existing
hover: while Ctrl is held it **replaces** the entity hover pick rather than joining it. That is what
the deferral was actually worried about, and it is answered structurally instead of by a measurement
that would have had to be repeated every time the scene changed.

The readout costs nothing to build — the pick has already run — so unlike `BuildSurfaceHoverRows`
only the *drawing* is dwell-gated, and REQ-089's one-shot latch machinery is not needed here.

## Implementation

| file | change |
|---|---|
| `spec/requirements.md` | REQ-318 item 14 + six acceptance bullets |
| `spec/project.md` | D-2026-09-04-b |
| `src/commands/CadCommands.hpp` | `subObjectHoverValid` / `subObjectHover` / `subObjectHoverDwell`; `SubObjectHoverRow` + `BuildSubObjectHoverRow` |
| `src/commands/CadCommands.cpp` | `BuildSubObjectHoverRow` — resolves the four strings, refuses an expired reference |
| `src/util/cadsolid.hpp` | `CadSubObjectOverlay` — the two face-tint buffers as one struct |
| `src/viewport/TransformPreview.{hpp,cpp}` | `AppendSubObjectGeometry` extracted; `BuildSubObjectHoverHighlight` added |
| `src/render/ViewportRenderer.{hpp,cpp}` | the tint draw takes both buffers; hover first, selection over it |
| `src/ui/CadUi.cpp` | the Ctrl-gated hover pick; `DrawSubObjectRolloverReadout`; readout precedence |
| `src/app/main.cpp` | hover linework into the hover channel, hover tint into the overlay |
| `tests/SubObjectSelectionTests.cpp` | two new cases, 8 total / 158 assertions |

### Four choices worth recording

1. **The pre-highlight and the click are the same query.** Both call
   `PickSubObjectAcrossSolids` with the same tolerance, so what lights up is what selects by
   construction — the rule REQ-318 item 1 already states for the snap and the selection, applied
   once more. A test asserts it by running both on one ray and comparing.
2. **The pre-highlight suppresses the entity hover** rather than drawing beside it. Two highlights
   answering one cursor is the defect, not the feature — the same logic D-2026-09-04-a used for the
   selections themselves.
3. **A hovered sub-object that is already selected draws nothing.** The selection highlight is the
   stronger statement and a quieter one over it only muddies the colour. `BuildHoverHighlight`
   already applies exactly this rule to entities.
4. **One struct, not a second parameter.** `CadSubObjectOverlay` carries both face-tint buffers,
   because `RenderScene`'s signature is already 30 long and the codebase says so — the argument
   `CadSolidDisplayGeometry` records for itself. The *linework* needed no new channel at all: the
   selection's rides the existing highlight channel and the hover's the existing hover channel, and
   each gets the right colour and the never-occluded treatment for free.

### Colours

Hover is the entity-hover blue (`0.45, 0.72, 1.0`) at **0.20** alpha against the selection's yellow
at 0.38 — the same hue the hover stroke already uses, at about half the opacity. It has to read as
*this is what you would get*, not as *this is what you have*.

## Test approach

Two new cases in `SubObjectSelectionTests`:

- **the pre-highlight names what a click would take** — the hover pick and the click pick run on one
  ray and are asserted equal; the face hover fills triangles and emits no linework; once selected the
  pre-highlight goes silent while the selection highlight does not; no hover and an expired reference
  each draw nothing;
- **the rollover** — title, 1-based solid number, layer, and the *stored* `ByLayer` colour and
  linetype rather than resolved values (what the Properties panel shows, and what the user would
  change); an expired reference and a kindless reference are both refused rather than described.

**Not extended to the transcript.** The verb would have to press a modifier key, which no headless
driver verb can do, and every command-layer function underneath is already asserted by the unit
cases above. `EXPECT SUBOBJECTS`-style coverage of a hover would be asserting the test's own setup.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean.
- **testing** — PASS. `ctest` **1123/1123**.
- **architecture-review** — PASS. No layer moved. The one new draw path shares the tint block the
  selection already had; the row builder is pure and in the command layer, so the readout is
  presentational, as REQ-089's is.
- **performance-review** — PASS by construction rather than by inspection: the pick is behind the
  existing gate and replaces the entity hover for the frames it runs. `perfHoverPickMs` already
  instruments that block, so a regression is visible in the existing counter.

## Not covered by test, stated plainly

- **That `Ctrl` arms it** — the one decision left in `CadUi.cpp`, unchanged from TASK-199.
- **The tint's pixels and the tooltip's layout.** GUI verification, as for every draw path here.
- **The dwell's interaction with the surface and survey-point readouts.** The precedence is three
  branches in one `if`; `HoverDwellTests` pins the timer itself, and which panel wins is GUI-checked.

## Technical debt

- **DEBT-1 — the rollover describes the owning SOLID's properties, not the sub-object's.** A face has
  no colour or layer of its own today, so there is nothing else truthful to show; AutoCAD does the
  same. If per-face attributes ever arrive, this is where they surface.
- **DEBT-2 — no rollover for whole entities.** AutoCAD shows one for any object under the cursor
  without a modifier; this shows one only while Ctrl is held, which is what was asked for. Extending
  it is a gate change, not new machinery.
- **TASK-199's DEBT-2 and DEBT-3 stand** — the sub-object selection is still invisible to the
  Properties panel and the status bar, and TASK-189's face-interior containment gap is untouched.

---

## Follow-up, same day — the face preview was invisible, not absent

Reported by the user with a reference image: *"the selection preview for the faces does not work …
the line and point preview worked, it's just the faces that isn't working"*, plus *"make the face
preview purple to tell apart from lines and points that are blue"*.

### Diagnosis — it was drawing the whole time

Not a missing draw, and worth stating because "does not work" and "cannot be seen" want opposite
fixes. Three checks, none of them a guess:

- the geometry was produced — the existing test asserted `hoverFaceTris` non-empty and passed;
- the shader honours alpha (`FragColor = uColor;`), so the fill was not being forced opaque;
- nothing enables `GL_CULL_FACE` anywhere in the renderer, so it was not being culled.

What was left is arithmetic. The tint blends `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` over whatever is
behind it, and in **2D Wireframe — the default style — solids draw no faces at all**, so what is
behind it is the empty viewport. `0.20 × (0.45, 0.72, 1.0)` over black is **RGB(23, 37, 51)**: black,
to any eye, beside the white wireframe two pixels away. The line and vertex previews were opaque, so
they were fine — exactly matching the report.

**A translucent fill is the wrong primary treatment for this.** It only reads where there is
something to tint, which in the default style there is not. The user's reference image shows the
answer every CAD package uses: **outline the face**.

### Changes

- **A highlighted face now draws its BOUNDARY**, walked from the face's own loops — every loop, so a
  face with a hole shows the hole, and each edge through `AppendSolidEdge`, shared with the edge
  highlight, so a curved face outlines as a curve rather than a chord. The fill stays as the
  supporting act and its alpha is raised (0.20 → 0.30 hover, 0.38 → 0.42 selected) for the shaded
  styles where it does have something behind it.
- **The face preview is PURPLE** (`0.72, 0.45, 1.0`), against the blue an edge or a vertex gets.
  Three kinds share one cursor and precedence decides between them within a few pixels, so telling
  them apart has to be possible at a glance rather than by reading the command line. The fill and
  the outline take the constant from one place, or they read as two different things.
- **The outline is NOT depth-tested**, unlike the fill. It is linework one pixel wide, and the rule
  the fill has to break is the rule this one obeys: sunk into the surface it traces, it vanishes.

### Test

`A highlighted face draws its boundary, not only a fill` — the boundary is non-empty, is **exactly
four segments** for a box's quadrilateral top face (a mere non-empty check would pass if the whole
solid's wireframe were emitted, which would look almost right on screen), every vertex of it lies on
that face's own plane at `z = 8` (what fails if the loop walk strays onto an adjacent face), the
hover path outlines too, and an edge or vertex selection contributes no face boundary.

This is the half that cannot be verified from a screenshot after the fact: a fill with no outline
looks exactly like a bug report.

`ctest` **1124/1124**.

### Debt

- **DEBT-4 — the tint's visibility is not asserted anywhere, and cannot be.** The failure was a
  colour arithmetic result on a particular background, which no unit test sees. What is now pinned
  is that a face emits linework at all — the property whose absence caused this. A genuine guard
  would need a rendered-pixel comparison, which this project has no harness for.
