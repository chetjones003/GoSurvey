# TASK-209 — MOVE in three dimensions, and a solid that can be moved (REQ-322, issue #148 slice 4a)

## Origin: a SPEC GAP found while starting the gizmo

Slice 4 is REQ-060, the manipulation gizmo. Reading the requirement against the code found that
**neither half of its acceptance was reachable**, and no requirement had recorded why.

REQ-060 asks for a gizmo that operates on the selection *"in 3D"*, and whose drag must *"produce
coordinates agreeing with the equivalent typed command"*. Measured against `beta`:

| function the typed commands use | what it can express |
|---|---|
| `ApplyTranslationToSelection(dx, dy)` | X and Y only — **not one** Z touch across the ten entity types it walks |
| `ApplyRotationToSelection(bx, by, rad)` | about a vertical axis only |
| `ApplyScaleToSelection(bx, by, sc)` | about a flat point |

And **all six** transform commands — MOVE, ROTATE, SCALE, MIRROR, ARRAY, STRETCH — call
`DropSolidsFromSelectionForTransform`, which erases every solid from the selection and reports
*"transforming a solid is not supported yet"*.

So a gizmo's up-down handle would have had **no typed command to agree with**, failing REQ-060's own
second acceptance bullet the moment it worked; and the objects Phase 5 exists to edit could not be
moved **at all, by any means**.

That is a SPEC GAP in CLAUDE.md §5's sense — the requirement is unambiguous and still not buildable —
so it was put to the user rather than resolved here.

**The user chose:** 3D translate first, gizmo after; and typed MOVE moves solids too, rather than the
gizmo being the only way. The second answer is what makes REQ-060's acceptance testable at all, and
it finishes a gap the code itself already called "not supported yet".

**A requirement can be accepted, unambiguous, and still not buildable.** REQ-060 has been accepted
and unimplemented since 2026-08-11, and this is why. REQ-060 now carries a starting-state note so the
next reader does not rediscover it.

## What was built

| file | change |
|---|---|
| `spec/requirements.md` | REQ-322, six statement items + seven acceptance bullets; REQ-060 starting-state note and revision |
| `spec/project.md` | D-2026-09-04-f |
| `src/commands/CadCommands.{hpp,cpp}` | `ApplyTranslationToSelection` gains `dz`; `TranslateSelectedSolids`; `PeelTypedElevation`; `modifyBaseZ` |
| `tests/headless/transcripts/req322-move-3d.txt` | new, 92 steps |

### Three choices worth recording

1. **A solid moves through `brep::Translate`, not a per-field sweep.** That function already moves
   every vertex, every arc edge's centre, every face's surface origin, a NURBS patch's control points
   and the recipe's placement frame — and its own header says why it must stay one function:
   *"open-coded at a call site, adding a field to `Surface` later would silently miss it, and a solid
   that half-moved is not a shape at all."* Replaced rather than edited, because
   `shared_ptr<const brep::Solid>` is what makes an undo snapshot a refcount bump.
2. **Feature lines get their own Z pass.** `TransformSelectedFeatureLinesInPlace` takes an `(x, y)`
   lambda and is shared with ROTATE, SCALE and MIRROR — which are deliberately plan-only. Widening
   the shared helper would hand them a Z they have no business with, so the elevation pass is
   separate and gated on `dz != 0`.
3. **The PICKED MOVE path stays plan-only.** It has always ignored its two points' elevations;
   quietly making it 3D would change what every existing drag does rather than adding something new.
   Typed entry is where the Z arrives (REQ-322 item 4), and the gizmo will be the other 3D route.

### Scope held deliberately narrow

ROTATE, SCALE, MIRROR, ARRAY and STRETCH keep refusing solids and keep working in plan. Widening them
means turning every entity's stored frame about an arbitrary axis — the work REQ-312 needed for a
single tilted arc, multiplied across every type — and is a separate requirement, not a footnote.

## Test approach

`req322-move-3d.txt`, 92 steps. **The no-Z case is asserted first**, because a regression there is
worse than the feature is worth: every existing drawing and habit must behave exactly as before.

Then a typed absolute Z, a relative `@dx,dy,dz`, and an absolute *pair* where the offset is the
difference of the two elevations (base at 10, destination at 4, so the line drops 6 rather than
rising to 4 — the case that distinguishes "difference" from "set").

**A solid's move is asserted by mass properties, not coordinates.** A translation is an isometry, so
volume and surface area must be unchanged to the last digit the closed forms give — any drift at all
is a defect rather than a tolerance. That is a stronger check than comparing a few coordinates, and
it is the one that catches a solid which moved only *some* of its parts: such a solid still passes
`brep::Validate`, which never compares a face's surface against its own boundary.

Mass properties alone would pass on a solid that never moved, so they are paired with fence
selections at the old and new positions. **A fence accumulates into the current selection**, so the
negative half ("nothing where it used to be") is asserted in the reload block, which starts from an
empty selection and can therefore say it — recorded rather than worked around, because the first
draft of this file asserted it where it could not hold.

Finally: ROTATE still refuses a solid by name, so *"MOVE works now"* cannot be read as *"transforms
work now"*.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean.
- **testing** — PASS. `ctest` **1153/1153** on `beta` @ `c3c89e3`.
- **architecture-review** — PASS. No layer moved; the solid translation goes through the kernel's own
  single-home function rather than a second sweep in the command layer.
- **code-review** — self-run. It caught one real defect: the annotation Z edit first landed in a
  block-paste function that has no `dz`, because the `insX/insY` pattern occurs twice. Compile error,
  found and corrected, but worth noting as the risk of pattern-matched edits in a file this size.
- **performance-review** — PASS by inspection. One extra float add per entity, and one solid copy per
  selected solid per move.

## Not covered by test, stated plainly

- **The picked MOVE path's Z** — it deliberately has none.
- **The gizmo** — slice 4b, not started.

## Technical debt

- **DEBT-1 — COPY is still 2D.** `DuplicateCadSelectionTranslated` is its own per-type sweep and
  duplicating a solid needs the same `brep::Translate` treatment. MOVE and COPY now disagree about
  what a destination means, which is the kind of asymmetry a user finds quickly.
- **DEBT-2 — ROTATE, SCALE, MIRROR, ARRAY and STRETCH are still plan-only and still refuse solids.**
  REQ-060's rotate and scale gizmo modes cannot be built until at least rotation is lifted, so this
  is on the path rather than beside it.
- **DEBT-3 — the picked MOVE path ignores elevations.** Consistent with its history, inconsistent
  with typed entry. Wants a decision rather than a fix.
