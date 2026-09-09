# TASK-231 — typed ROTATE and SCALE stop refusing solids

## Requirement authority

- **REQ-332** (the kernel `brep::Rotate` / `brep::Scale` this slice consumes — TASK-230, slice 1).
- **REQ-322 item 6**, which this slice AMENDS: it currently says *"ROTATE, SCALE, MIRROR, Polar ARRAY
  and STRETCH keep refusing solids and keep working in plan"*, with Rectangular ARRAY carved out.
  ROTATE and SCALE are now carved out too, for the reason that item itself gave — *"widening them
  means rotating every entity's stored frame about an arbitrary axis ... a separate requirement"* —
  which REQ-328 and REQ-332 have since supplied.
- **REQ-060**, whose rotate/scale handles are recorded as *blocked* precisely because these two
  commands refuse solids and so give a handle nothing to agree with.
- **REQ-329 increments 2 and 3**, the UCS-aware ROTATE and SCALE this must extend without changing.
- **GitHub issue #148 acceptance 4**. Slice 2 of 3.

## The shape of the change

`TranslateSelectedSolids` (REQ-322) is the template, and the parallel is exact: de-duplicate the
selection by solid index, call the kernel, and **replace** rather than edit, because `CadSolidPtr` is
`shared_ptr<const brep::Solid>` and that immutability is what makes an undo snapshot a refcount bump.

Two new statics beside it — `RotateSelectedSolids`, `ScaleSelectedSolids` — and then four of the nine
`DropSolidsFromSelectionForTransform` call sites give way to them:

| site | command | after |
|---|---|---|
| `ApplyRotationToSelection` | ROTATE, plan / plan-rotated UCS | `RotateSelectedSolids` about world Z through (bx, by) |
| `RotateSelectionInPlaceAboutAxis` | ROTATE, tilted UCS | `RotateSelectedSolids` about the UCS Z axis |
| `ApplyScaleToSelection` | SCALE | `ScaleSelectedSolids` about (bx, by, bz) |
| rotate-**copy**, tilted | ROTATE with Copy | **unchanged — still refuses** (see below) |

MIRROR (2 sites), Polar ARRAY and STRETCH keep refusing, unchanged.

## Two decisions this slice has to make

### 1. ROTATE-copy still refuses a solid, and that is a boundary rather than an oversight

`DuplicateCadSelectionRotated` and `DuplicateCadSelectionTranslated` contain **no solid handling at
all** — grepping either for `Solid` or `cadSolids` returns nothing. Duplicating a solid is a
different operation from transforming one: it appends a new entity, which is the entity-id, layer and
selection bookkeeping Rectangular ARRAY had to write separately (issue #400 increment 3). This slice
transforms solids **in place**, which is exactly what REQ-060's gizmo needs — a gizmo drags a
selection, it never copies it. Rotate-copy keeps its named refusal.

### 2. A solid scales in Z even in plan view, where a line does not

This is a real asymmetry and it is deliberate. `ApplyScaleToSelection` scales X/Y about (bx, by), and
`ScaleSelectionZAboutBase` scales Z about `bz` **only under a tilted UCS** — plan view keeps the
pre-REQ-329 "elevations untouched" behaviour byte-for-byte.

A solid cannot join that carve-out. `brep::Scale` is uniform because the representation has no
ellipsoid and no elliptical cylinder (REQ-332 item 7), so "scale X and Y but not Z" is not a
half-measure, it is unrepresentable. And a solid has no legacy behaviour to protect: it was refused
outright until this slice, so there is no drawing anywhere whose solids scaled in plan without their
elevations. So **a selected solid always scales uniformly about (bx, by, bz)**, and the plan-view
carve-out remains what it always was — a compatibility rule for 2D entities.

`ApplyScaleToSelection` therefore gains a `bz` parameter. That is the same move REQ-322 made when it
gave `ApplyTranslationToSelection` a `dz`, and it has exactly one caller.

## Files

- `src/commands/CadCommands.cpp` — the two new statics, the four call sites, the `bz` parameter.
- `tests/RotateScaleSolidCommandTests.cpp` (new) + `CMakeLists.txt`.
- `tests/headless/transcripts/req332-rotate-scale-solid.txt` (new).
- `spec/requirements.md` — REQ-322 item 6 + its acceptance line amended; REQ-060's blocked note
  updated; REQ-332 gains the command increment and its traceability row is extended.
- `spec/architecture.md` — ADR-046 amendment (m) gains the command-layer note.

## Test approach

The kernel numbers are TASK-230's and are not re-derived. What is new here is **the wiring**, so the
cases are about what only the command layer can get wrong:

1. ROTATE 90° in plan turns a box: volume and area unchanged (still an isometry through the command),
   and the box's extents swap.
2. ROTATE under a **tilted** UCS turns the solid about the UCS Z axis, not world Z — the case that
   would silently pass if the axis were hard-coded.
3. SCALE by 2 in **plan view** scales the solid in Z as well: volume `* 8`. This is decision 2 above,
   asserted rather than described.
4. A 2D entity in the same selection keeps the old plan-view behaviour in the same command — the
   asymmetry is deliberate, so it is pinned, not left to drift.
5. ROTATE and SCALE no longer emit "transforming a solid is not supported yet".
6. MIRROR, Polar ARRAY and STRETCH still do — the refusal was narrowed, not deleted.
7. One Ctrl+Z restores the prior solid exactly (#148 acceptance 7).
8. A rotated and a scaled solid survive `.gs` save/reopen (#148 acceptance 8).
9. A selection holding the same solid twice transforms it **once** — the de-duplication
   `TranslateSelectedSolids` needed for the same reason.

## Verification

- **build-project** — PASS, Release and Debug, no new warnings.
- **testing** — PASS. `ctest` **1374/1374** (1373 + the new transcript).
- **Both new assertions were proven to bite**, which matters because the transcript passed on its
  first run and a test that has never failed has not been shown to test anything:

  | mutation | result |
  |---|---|
  | hard-code the rotation axis to world Z | fails the tilted-UCS case at `mnX -5.0 vs 0.0` |
  | hard-code the scale base to `bz = 0` | fails the elevation-4 case at `mnZ 8.0 vs 4.0` |

  The second case was **added because of this check**: the original plan-view scale case had its base
  elevation at 0, so a hard-coded zero would have passed it. The assertion did not earn its place
  until the drawing was moved to elevation 4.
- **One existing test failed, correctly.** `headless.req322-move-3d` asserted ROTATE's refusal of a
  solid — REQ-322's own acceptance line, and exactly the behaviour this task amends. The assertion
  was **rewritten onto MIRROR** (which still refuses) rather than deleted, because the block's point
  is unchanged: "MOVE works now" must not read as "every transform works now", and the list of which
  transforms accept a solid is precisely the fact that drifts.
- **architecture-review** — PASS. No new layering: the command layer calls the kernel exactly as
  REQ-322 does, and the kernel gained nothing in this slice.
- **code-review** — self-run. One thing recorded rather than changed: `TransformSelectedSolids`
  reports the LAST refusal reason when several solids are refused for different reasons. Lossy in
  principle, accepted in practice and documented at the call site — both callers normalize the axis
  and clamp the factor positive, so the only reachable refusal is a validity failure that an isometry
  or a positive scale cannot produce on a solid that was valid going in.

## Technical debt

- **DEBT-1 — ROTATE with Copy still refuses a solid**, by decision (see above), and so does Polar
  ARRAY. Both want the same missing piece: duplicating a solid as a new entity. Rectangular ARRAY
  already does it, so the third caller is the point at which it should become a shared helper rather
  than a third copy.
- **DEBT-2 — `ApplyRotationToSelection` and `ApplyScaleToSelection` are still not declared in
  `CadCommands.hpp`.** Slice 3 has to expose them for the gizmo handles to commit through the same
  function the typed command calls, which is how REQ-060 makes "the gizmo agrees with the typed
  command" a property rather than a hope. Left to that slice rather than widened speculatively here.

## Landing note

Depends on TASK-230 (PR #452), which is open and not yet merged. Developed on a local branch stacked
on it; the PR is held until #452 merges and this is rebased onto `beta`, so that no PR is opened that
says "merge #452 first".
