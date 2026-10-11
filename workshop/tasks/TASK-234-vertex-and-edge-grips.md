# TASK-234 — the gizmo gets handles on an edge and a vertex

## Requirement authority

- **REQ-333 increment 2** — the UI half of TASK-233's kernel operations.
- **REQ-060**, which currently says an edge or vertex selection *"deliberately gets no gizmo. The
  kernel has no operation that moves one — `brep::PushPullFace` is the only solid edit there is."*
  **That premise is now false**, and this task is what makes it false. The note is corrected rather
  than deleted, because its reasoning was right and it is the reasoning that changed.
- **GitHub issue #148 acceptance 3** — this is the half that makes it a *grip* rather than a kernel
  capability nothing calls.

## The shape, and the one honest gap

`CadGizmoMode` gains `SubObjectEdge` and `SubObjectVertex`. Everything else follows the face grip
that already exists: the anchor is where the sub-object is, the axes are what the kernel can actually
move it along, and the commit goes through one function.

**Handle counts, by the same rule that gave a face one handle:**

| selection | handles | why exactly that many |
|---|---|---|
| face | 1 | `PushPullFace` takes a distance along the normal and nothing else |
| **vertex** | **3** | three planes meet, so the move has three degrees of freedom — every direction is reachable, and the UCS axes are the natural basis |
| **edge** | **2** | two planes, two degrees of freedom. The handles are the two adjacent faces' own outward normals, which span exactly the plane perpendicular to the edge. **There is deliberately no third**: the along-the-edge direction is not a motion at all (REQ-333 item 4), so a handle there would advertise a drag that does nothing |

**The gap, stated rather than hidden.** REQ-060's second acceptance bullet asks a gizmo drag to agree
with *"the equivalent typed command"*. For a face that command is PRESSPULL. **For a vertex or an
edge there is no typed command**, because REQ-333 defines kernel operations and no requirement asks
for a verb. So the bullet has nothing to compare against here, and the discipline it exists to
enforce is kept the only way it can be: `CadApplyMoveVertex` / `CadApplyMoveEdge` are the single
implementation, so a typed command added later calls them rather than growing a second one.

**A handle is only drawn where the drag would succeed.** `CadSubObjectVertexGrip` returns false
unless exactly three planar faces meet; `CadSubObjectEdgeGrip` unless the edge is straight with
exactly two planar faces. That is `CadSubObjectFaceGrip`'s existing discipline — it already declines
a non-planar face — and it matters more here, because the refusals REQ-333 carries (a pyramid apex, a
cylinder rim) are ones a user can easily pick.

## Files

- `src/util/brep.{hpp,cpp}` — two public topology queries, `FacesAtVertex` and `FacesAlongEdge`, so
  the command layer can ask what meets where without re-deriving loop walking. `MoveEdge` uses the
  second itself rather than keeping a private copy.
- `src/commands/CadCommands.{hpp,cpp}` — the two modes, the grips, the axes, `CadApplyMoveVertex` /
  `CadApplyMoveEdge`, and the commit branch.
- `tests/GizmoSubObjectMoveTests.cpp` (new) + `CMakeLists.txt`.
- `tests/headless/transcripts/req333-vertex-edge-grips.txt` (new).
- `spec/requirements.md` — REQ-333 increment 2; REQ-060's stale note corrected.

## Test approach

1. Handle counts: 3 on a vertex, 2 on an edge, 1 on a face, and **0 where the kernel would refuse** —
   a pyramid apex and a cylinder rim get no gizmo at all rather than one that fails on drop.
2. A vertex grip drag along UCS X moves the box exactly as `brep::MoveVertex` with that delta does.
3. An edge grip drag along the first face normal likewise.
4. One Ctrl+Z restores the prior solid in a single step (#148 acceptance 7).
5. The sub-object selection SURVIVES the edit — the reference is re-pointed at the replaced solid,
   as push/pull does, or a second drag would be impossible without re-picking.
6. End to end through the camera in a transcript, including a `.gs` round trip (#148 acceptance 8).

## Verification

- **build-project** — PASS, Release and Debug, no new warnings. (One was introduced and fixed: an
  empty controlled statement, `C4390`, from a deliberate fall-through written as `if (...) ;`.)
- **testing** — PASS. `ctest` **1397/1397** (1391 + 5 unit cases + 1 transcript).
- **Proven to bite.** Deleting the three-planes guard from `CadSubObjectVertexGrip` makes a pyramid's
  apex sprout a handle that the kernel then refuses on release — caught by both the unit case and the
  transcript.
- **Two existing tests failed, and both were right to.** They asserted the premise this task removes:
  *"an EDGE or a VERTEX selection gets no gizmo, the kernel has no operation that moves either."*
  Rewritten rather than deleted, because the reasoning was sound and it is the premise that changed.
- **The second one had been passing for the wrong reason, which is the finding worth keeping.**
  `req148-gizmo-subobject`'s "a VERTEX gets no gizmo" block did not hold a vertex — it held **two**
  sub-objects, because a second `SUBOBJECT` pick ADDS to the selection rather than replacing it. The
  old code returned `None` for any selection that was not a single face, so "a lone vertex has no
  gizmo" and "two sub-objects have no gizmo" were indistinguishable and the block asserted the
  weaker one without saying so.

  It only surfaced because the new behaviour made the two cases differ. Split into a fresh-drawing
  vertex case and a separate two-sub-objects case, with `EXPECT SUBOBJECTS 1` now pinning which is
  which — the assertion that had been missing all along.
- **architecture-review** — PASS. The kernel gained two topology queries and nothing else; the
  command layer gained the grips and two commits shaped exactly like `CadApplyPushPull`, with the
  shared half (`CadCommitSolidEdit`) extracted so the undo step, the re-pointed reference and the
  refusal sentence are stated once rather than three times.
- **code-review** — self-run. One thing fixed beyond the warning: `SubmitGizmoClick` captured its
  sub-object through `CadGizmoSubObjectFace`, which returns false for anything but a face, so an edge
  or vertex drag would have armed with an empty reference and committed against the wrong target.

## Technical debt

- **DEBT-1 — no typed command for either operation.** Stated in REQ-060 and above rather than left
  implicit: the "agrees with the equivalent typed command" bullet has nothing to compare against
  here. If a verb is ever wanted, it calls `CadApplyMoveVertex` / `CadApplyMoveEdge` rather than
  growing a second implementation.
- **DEBT-2 — the edge grip's two handles are drawn in the entity gizmo's axis colours.** Handle 0
  takes red and handle 1 green, which name UCS X and Y — and these are face normals, not UCS axes.
  The face grip already solved this for itself by drawing purple; the edge deserves the same
  treatment, and it is presentation-only work left out of a slice that is otherwise about geometry.

## Landing note

Sixth in the chain, none merged. Held for rebase onto `beta` behind TASK-230/231/232/233.
