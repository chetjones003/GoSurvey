# TASK-199 — sub-object selection: the mode, the store, the highlight (REQ-318 increment 2, issue #148)

## Requirement authority

- **REQ-318** — *Sub-object picking*, accepted, **increment 1 of 2 delivered** (TASK-189, ADR-049,
  D-2026-09-03-c). Increment 1 shipped the shared pick *query* (`ray3d::RayTriangleIntersect`,
  `src/util/solidpick.{hpp,cpp}`, `CadSnap` refactored onto both). Its own status line names what is
  left: *"Increment 2 is the selection: a sub-object selection mode with its own store, the
  highlight treatment, and coexistence with whole-entity selection — which is where #148's
  acceptance criteria 1 and 2 are actually met."* This task is that increment.
- **GitHub issue #148** acceptance criteria **1** (*"Faces, edges and vertices can each be selected
  individually and are visibly highlighted"*) and **2** (*"Sub-object selection does not interfere
  with existing whole-entity selection"*).
- **REQ-064 / ADR-026 (e)** — the visual styles, because 2D Wireframe (the default) draws no solid
  faces and runs with depth testing off, which decides what a face highlight can mean there.
- **REQ-100** — the frame budget; the highlight is rebuilt on the per-frame path.

**A SPEC amendment is part of this task, not a precondition for it.** REQ-318's statement and its
eleven acceptance bullets are all about the *query*: none of them mentions selection state, a
highlight, or coexistence. So increment 2 has a named scope but no acceptance criteria, which is a
gap this task closes rather than works around — REQ-318 gains an increment-2 statement and
acceptance, and the two user decisions below are recorded as **D-2026-09-04-a**. TASK-189 anticipated
this: *"An ADR and accepted REQs will come with slice 2 rather than as a separate spec-only PR."*

## Two decisions, put to the user 2026-09-04

1. **Entry: Ctrl+click, AutoCAD-style.** A plain click keeps selecting the whole solid exactly as it
   does today; holding Ctrl selects the face, edge or vertex under the cursor instead. Chosen over a
   `SUBOBJSELECT` toggle command and over having both. Rationale as put: no new command, no mode a
   user can be left in without noticing, and it is the gesture an AutoCAD user already has. Ctrl is
   unused in the model viewport today (checked: the only `io.KeyCtrl` reads in `CadUi.cpp` are a
   floating-viewport shortcut and one unrelated guard), so nothing is displaced.
2. **Occlusion: faces depth-tested, edges and vertices not.** A highlighted face is hidden by nearer
   geometry so a back face does not glow through the body; edges and vertices stay always-visible
   because they are thin lines and dots that would otherwise disappear into the surface they lie on.
   Chosen over "never occluded" (the existing blanket overlay rule, `ViewportRenderer.cpp:982`) and
   over "everything depth-tested". This is the leaning already recorded on #148, now decided.

   **Qualification the decision needs and did not have:** in **2D Wireframe**, the default style,
   solids draw no faces at all and the depth test is off (`ViewportRenderer.cpp` — *"Faces.
   Depth-on styles only"*). There is therefore nothing for a face highlight to be occluded *by*, and
   it simply draws. That is the correct behaviour rather than a compromise: in wireframe the tinted
   face is the only way a face selection can be shown, and no nearer surface exists on screen to
   contradict it.

## Starting state — verified by grep, not quoted

D-2026-09-03-c closes with a process note earned on this very issue: *"A claim about what the
codebase lacks is a `grep`, not a quotation."* Three checks against `beta` @ `d158da7`:

1. **A selected solid has no highlight at all today.** `AppendEntityHighlight`
   (`src/viewport/TransformPreview.cpp:1285-1367`) handles LineSeg, Circle, Arc, Ellipse, Polyline,
   FeatureLine, Surface and FilledRegion. There is no `Solid` branch, no `Mesh` branch, and nothing
   in `ViewportRenderer.cpp` supplies one. So criterion 1's *"visibly highlighted"* is unmet for a
   whole solid before sub-objects are considered, and this task has to build both. Precedent for the
   shape of the omission is in that same file: REQ-087's feature line *"drew no highlight at all: it
   would pick, box-select and move correctly while giving the user nothing on screen to confirm what
   was selected."*
2. **Whole-solid picking is by EDGES only**, deliberately and documented
   (`CadCommands.cpp:21880`): *"a solid's edges are what is drawn in every visual style … in 2D
   Wireframe, the default, the edges are the ONLY thing on screen."* Sub-object picking also tests
   triangles. The two therefore answer differently for a click in the middle of a face — plain click
   selects nothing, Ctrl+click selects the face — and that is correct rather than inconsistent, but
   it has to be stated.
3. **TASK-189's DEBT-1 comes due here.** `solidpick::PickSubObject` sees one solid at a time, so its
   occlusion rule is per-solid: a vertex hidden behind a *different* solid is still picked. The debt
   entry names the resolution — *"at the caller, which will depth-order per-solid answers using
   `Pick::rayT`"* — and this is the caller.

## Implementation approach

### Domain — the reference and the store

`SelectedSubObject` beside `SelectedEntity` in `CadCommands.hpp`: the owning solid's index, the
`solidpick::Kind`, the sub-object index, and a `std::weak_ptr<const brep::Solid>` for the identity.
ADR-049 already decided the shape and the reason — *"an index keeps its meaning across an edit that
preserves the topology and loses it across anything that changes the counts … expire rather than
silently re-bind to whatever now occupies the index."* A `SubObjectSelectionExpired` sweep drops
entries whose `weak_ptr` no longer matches `cadSolids[solidIndex]`, run wherever the entity selection
is already validated.

Its own store (`AppCommandState::subObjectSelection`), not a variant inside `selection`: every
consumer of `selection` — transforms, DELETE, properties, DXF export, the highlight walk — would
otherwise need a branch for a thing none of them can act on yet, which is how criterion 2 gets
broken by accident rather than by decision.

### Coexistence rule (criterion 2)

The two selections are **mutually exclusive, and each click states which it means**: a plain click
clears the sub-object selection, a Ctrl+click clears the entity selection. Shift+Ctrl+click toggles
within the sub-object selection, mirroring Shift's existing meaning for entities. Nothing that
consumes `selection` sees a sub-object, and nothing that consumes the sub-object store sees an
entity — which is the strongest available reading of *"does not interfere"*.

### UI — the pick

In `CadUi.cpp`'s idle click path, ahead of the entity pick, when `io.KeyCtrl` is held: take the
cursor ray already built there (`cursorRayPtr`), walk visible solids (`SolidVisible`), call
`solidpick::PickSubObject` with each solid's cached tessellation, and keep the answer with the
smallest `Pick::rayT` — the cross-solid depth order DEBT-1 asks for. Tolerances come from the
existing pixel budget converted at the pick depth, the way `CadOffsetEntityPickTolWorld` already
does. A Ctrl+click that hits nothing clears the sub-object selection rather than falling through to
a box drag, so Ctrl is unambiguous.

### Highlight

Built from the per-solid `CadSolidTessellation`, **not** from `CadSolidDisplayBatch` — the batch
coalesces solids into shared buffers for REQ-100's draw-call budget (#194) and has no per-face
channel, so the face's triangles are not recoverable from it.

- **Face** — the triangles whose `triFaceIds` entry is the picked face, emitted as a new
  depth-tested triangle overlay.
- **Edge** — the edge's true curve walked into segments, appended to the existing `highlightLines`,
  which is already the never-occluded accent channel and is therefore exactly the treatment
  decision 2 asks for.
- **Vertex** — a small three-axis cross at the vertex, also into `highlightLines`.
- **Whole solid** — the missing `Solid` branch in `AppendEntityHighlight`: the solid's `edgeVerts`,
  which is what the entity pick already tests against, so the highlight traces what selects.

Only the face fill is a new renderer channel: one optional `const std::vector<float>*` of triangles
drawn with `GL_LEQUAL` and a polygon offset so it does not z-fight the face it covers.
`RenderScene`'s signature is already long and the codebase says so, so this is one parameter and not
three.

### Test approach

- `SolidPickTests` (or a new `SubObjectSelectionTests`) for the pure parts: expiry across a
  topology-changing edit vs survival across a topology-preserving one — the two cases ADR-049
  measured — the mutual-exclusion rule, and cross-solid depth ordering.
- A headless transcript: this is the slice where a transcript finally has something to drive, which
  TASK-189 said explicitly (*"Transcript coverage for #148 belongs to the next slice, where there is
  a selection to drive"*). Needs a driver verb for a Ctrl-modified click and an `EXPECT SUBOBJECT`
  oracle.
- The face-fill draw itself is pixels — GUI verification, stated plainly, as TASK-189 and TASK-193
  both did for their draw paths.

## Architectural-boundary check

- `solidpick` stays pure and is not extended; this task is entirely its caller.
- The store lives on `AppCommandState` beside `selection`; no new subsystem.
- The renderer gains one optional buffer parameter and no knowledge of solids it does not already
  have — it is handed triangles, as it is for every other overlay.
- ADR-049 is **consumed, not amended**: the expiring reference it decided is used here for the first
  time. Whether that needs an ADR addendum for the *selection* semantics is the one open call for
  the spec step below.

## Out of scope, stated

- **Hover pre-highlight for sub-objects.** Increment 2 is the selection; running a sub-object pick
  every frame on hover is a REQ-100 question of its own and wants measuring, not assuming.
- **Anything that edits through a sub-object selection** — grips, the gizmo, fillet, chamfer. Those
  are #148's criteria 3-6 and its later slices.
- **Sub-object selection of meshes or surfaces.** A mesh has no topology to name (ADR-026 (c)) and a
  surface forbids component selection (REQ-070).

## What was built

Spec first, per CLAUDE.md's authority order: REQ-318 gained statement items **8-13** and eleven
increment-2 acceptance bullets, and the two user decisions are **D-2026-09-04-a**. Then the code.

| file | change |
|---|---|
| `spec/requirements.md` | REQ-318 items 8-13 + increment-2 acceptance; status and revisions |
| `spec/project.md` | D-2026-09-04-a |
| `src/commands/CadCommands.hpp` | `SelectedSubObject`; `AppCommandState::subObjectSelection`; four function declarations |
| `src/commands/CadCommands.cpp` | `ExpireSubObjectSelection`, `ToggleSubObjectSelection`, `PickSubObjectAcrossSolids`, `SubmitSubObjectPick`; `ClearCadSelection` and the fence both drop sub-objects |
| `src/ui/CadUi.cpp` | Ctrl+click routing — the ray, the tolerance, and nothing else |
| `src/viewport/TransformPreview.{hpp,cpp}` | the missing `Solid` branch in `AppendEntityHighlight`; `BuildSubObjectHighlight` |
| `src/render/ViewportRenderer.{hpp,cpp}` | one new overlay channel: the depth-tested face tint |
| `src/app/main.cpp` | the once-a-frame expiry sweep; wiring the two highlight halves |
| `tests/SubObjectSelectionTests.cpp` | new, 6 cases / 109 assertions |
| `tests/headless/HeadlessDriver.cpp` | the `SUBOBJECT` verb and four `EXPECT` oracles |
| `tests/headless/transcripts/req318-subobject-selection.txt` | new, 104 steps |

### The one design change made mid-task, and why

**Where the click's meaning lives.** It began inside `CadUi.cpp`'s Ctrl branch. It was moved into
`SubmitSubObjectPick` in the command layer once it became clear the transcript could not otherwise
reach it: idle click-select has no headless equivalent at all (`HeadlessDriver.cpp` says so at
`CLICK`), so the clear-the-other-selection rule, the Shift toggle and the clear-on-miss would all
have been GUI-verified-only — the exact shape of defect TASK-099 found five times over. The UI now
decides one thing and one thing only: that `Ctrl` is what asks for a sub-object. Everything else is
driven by the transcript.

**Two behavioural consequences that fell out of that, both improvements:**

1. **A completed fence clears the sub-object selection.** It has to, for the same reason a plain
   click does — a fence IS an entity selection — and putting it in `SubmitViewportPickImpl` rather
   than only in the viewport handler is what makes half of criterion 2 testable.
2. **An unrelated erase no longer expires a valid reference.** The first cut compared
   `cadSolids[solidIndex]` against the stored `weak_ptr` and expired on any mismatch. Erasing a
   *different* solid shifts every index after it, so that dropped a selection whose object was
   untouched — a defect, not an expiry. Identity now decides and the index is REPAIRED from it;
   expiry fires only when the `weak_ptr` no longer locks, which is exactly "the solid was replaced",
   which is exactly a topology change. Closer to ADR-049's own words than the first version was.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean.
- **testing** — PASS. `ctest` **1121/1121** on `beta` @ `d158da7`.
- **architecture-review** — PASS. `solidpick` is unchanged and still pure — this task is entirely
  its caller. The store is a vector on `AppCommandState` beside `selection`; no new subsystem. The
  renderer gained one optional buffer and no new knowledge. The one deliberate exception to a stated
  rendering rule is documented at the rule, at the draw, in the header, and in the ADR.
- **code-review** — self-run. Its finding is the design change above.
- **performance-review** — PASS by inspection. The expiry sweep early-outs on an empty selection
  (the overwhelming case) and is O(selected × solids) otherwise, with solids in the tens. The
  highlight walks one solid's triangles per selected face. Neither runs a pick on hover — hover is
  deliberately out of this increment.

### Every coordinate was probed before it was asserted

The box's own geometry (`CMD BOX 0,0 20 10 8` → centred on the origin, base at z = 0) was measured
against the shipped kernel before a single expectation was written, and each candidate point was run
through all three kind oracles to see what it actually reported. Two of the first-draft assertions
were wrong and were caught this way rather than by reasoning:

- an exact CORNER with both tolerances zeroed reports **nothing** — from that camera the corner is on
  the silhouette, so the ray meets the triangles sharing it tangentially. Correct, and it is the
  vertex *tolerance* that makes a corner pickable at all rather than a lucky triangle hit. The
  transcript now makes that point at a mid-edge instead, and says why;
- in the two-solid depth test the box at x = 60 is the NEAR one when viewed from +X. The first draft
  asserted index 0 — it would have passed under a caller that simply took whichever solid answered
  first, which is the very thing the case exists to rule out.

## Acceptance criteria status

Against **#148**'s list.

| # | criterion | status |
|---|---|---|
| 1 | Faces, edges and vertices each selectable and visibly highlighted | **met** — selectable and asserted three ways; the highlight geometry is asserted, its pixels are GUI-verified |
| 2 | Sub-object selection does not interfere with whole-entity selection | **met** — mutually exclusive stores, asserted in both directions |
| 3-8 | grips, gizmo, fillet, chamfer, undo, `.gs` round-trip | **not started** — later slices |

## Not covered by test, stated plainly

- **That `Ctrl` is the key.** The one decision left in `CadUi.cpp`. No transcript verb can press a
  modifier; GUI verification.
- **The face tint's pixels** — that it is depth-tested in Hidden/Shaded and simply draws in 2D
  Wireframe. The *geometry* handed to the renderer is asserted; whether the GL state does what the
  comment says is a screenshot, exactly as TASK-189 and TASK-193 recorded for their draw paths.
- **The plain-click half of the mutual-exclusion rule.** The fence half is driven by the transcript;
  the click half lives in the viewport handler, which has no headless equivalent.

## Technical debt

- **DEBT-1 — no hover pre-highlight for sub-objects.** Out of scope by decision. Without it the user
  learns what a Ctrl+click selected only after making it. Running the pick every frame on hover is a
  REQ-100 question that wants measuring, and the broad-phase reject `solidpick` already has is the
  reason to expect it is affordable rather than the evidence that it is.
- **DEBT-2 — the sub-object selection is invisible to the Properties panel and the status bar.** It
  reports only through the command-line log. Nothing acts on it yet, so there is nothing to show;
  revisit with the grips slice.
- **DEBT-3 — TASK-189's DEBT-2 still stands** (no face-interior containment test). Unchanged by this
  task and not made worse: a pick still trusts that a triangle tagged with a face id lies within that
  face's parametric span.
