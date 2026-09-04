# TASK-201 — push/pull a solid's face (REQ-319, issue #148 Phase 5 slice 3)

## Requirement authority

- **REQ-319** — new, written for this task. The first operation that edits a solid.
- **ADR-046 amendment (i)** — a modifying kernel operation, and a precondition `Validate` cannot
  enforce. Recorded as **D-2026-09-04-c**.
- **GitHub issue #148** criteria **3** (grips move faces — this is the geometry and the typed
  command; the drag is increment 2), **7** (one undoable step) and **8** (`.gs` round-trip).
- **ADR-049 / REQ-318** — the face was named by a Ctrl+click before the command was typed.

## Two facts established before anything was designed

Both by reading the code, which is D-2026-09-03-c's process note applied to a third task.

1. **The kernel had no operation that modifies an existing solid.** `Extrude`, `Revolve`, `Loft` and
   `Sweep` build from a profile; the Booleans combine two solids; `Slice` cuts one. Push/pull is a
   new *kind* of operation, which is why it took an ADR amendment rather than a note.
2. **`brep::Validate` cannot be relied on to catch how it goes wrong.** Its thirteen checks —
   `NoShell`, `EmptyShell`, `IndexOutOfRange`, `LoopNotClosed`, `EmptyLoop`, `EdgeNotUsedTwice`,
   `EdgeOrientationInconsistent`, `FaceHasNoLoop`, `DegenerateFace`, `DegenerateEdge`,
   `NonFiniteCoordinate`, `NotClosed`, `UnusedVertex` — are every one about **topology**. None
   checks that a face's vertices lie on that face's surface.

## The claim was measured, and the first version of it was wrong

This is the part worth reading. The precondition is: every neighbour of the moved face must be a
plane whose normal is perpendicular to the push. I wrote it up asserting that a **wedge's** slanted
neighbour was the case `Validate` would miss. Then I removed the check and measured:

| shape | with the precondition removed | what it proves |
|---|---|---|
| **Wedge** end face, distances 0.001 → 2.0 | **Refused anyway**, by `Validate`, at every distance | the pre-check is *not* what saves this case |
| **Cylinder** cap, pushed 3 | **Builds. `Validate` returns Ok.** Analytic volume **863.938** against a true **1021.02** for r=5 h=13 — **15% wrong** — because the wall surface still reports `height = 10` while its top boundary sits at 13 | the pre-check is load-bearing, and this is the case that proves it |

So the claim is true, but the wedge was the wrong evidence for it. Both halves are now recorded — in
the ADR, the REQ, the header and the test — because **the difference between them is the content**:
some geometric breakage happens to trip a topological check and some does not, and only measurement
tells them apart. On a wedge the pre-check buys an accurate sentence rather than safety: without it
the user is told *"that push would turn the solid inside out or flatten it"*, which is simply false
for a 0.001 ft push, and REQ-201 asks for a reason the user can **read**.

Had I shipped the first draft, the ADR would have carried a confident, checkable, wrong claim — the
exact failure mode D-2026-09-03-c's process note was written about.

## Scope, put to the user

Planar faces with parallel neighbours / also curved walls via radius / recipe-based parameter edit.
**The user chose planar with parallel neighbours.** It is the real topological operation: it works on
a solid with **no recipe**, which is every Boolean and slice result Phase 4 just built — the recipe
route would have refused all of them outright. A curved wall is a second geometry problem (a cap's
boundary arc must be re-solved, not translated) and belongs in its own increment.

## What was built

| file | change |
|---|---|
| `spec/requirements.md` | REQ-319, seven statement items + eleven acceptance bullets |
| `spec/architecture.md` | ADR-046 amendment (i) and delivery item 9 |
| `spec/project.md` | D-2026-09-04-c |
| `src/util/brep.hpp` | `PushPullFace`; four `Problem` values |
| `src/util/brep.cpp` | the operation; four `ProblemText` sentences |
| `src/commands/CadCommands.{hpp,cpp}` | `CadPressPull`; the `PRESSPULL` / `PP` verb |
| `tests/PushPullTests.cpp` | new — 4 cases, 155 assertions |
| `tests/headless/transcripts/req319-presspull.txt` | new — 84 steps |

### Three choices worth recording

1. **The recipe is dropped, never updated.** A pushed box is not the box its recipe describes, and a
   recipe that no longer describes its solid reads as authoritative while being false. ADR-045
   already made it optional and never consulted by validity, mass properties or tessellation, and
   `.gs` already stores topology — so this costs no format change and nothing downstream misses it.
2. **The selection follows the edit.** A solid is immutable and *replaced*, and the sub-object
   reference is keyed on the solid's identity (ADR-049) — so after a push the reference would expire
   on the next sweep and the user would have to re-pick the face before every single push. The
   command re-points it at the replacement. This is the practical reason REQ-319 item 5 states that
   the topology is preserved, rather than leaving it as an observation.
3. **Two selected faces are refused, not applied in turn.** The second push would be computed
   against the first's result while the user was picturing the original — a compound edit nobody
   asked for.

## Test approach

`PushPullTests` — the kernel's own arithmetic. Volume by hand for a **top** face and a **side** face
(a push that silently worked along Z would pass the first and fail the second); push-then-pull
restoring **every vertex**, not merely the volume, since a volume-only check passes on a sheared
solid; the moved face's own plane travelling with its boundary, asserted by putting every boundary
vertex back through the plane equation; and every refusal by name, including that each has a sentence
of its own rather than falling through to `ProblemText`'s generic ending.

Faces are found by **normal direction**, not by hard-coded index, so a face-ordering change in
`MakeBox` fails the tests rather than silently making them assert something else.

`req319-presspull.txt` — what a transcript can see and a unit test cannot: the command finding its
face from the sub-object selection, the selection surviving so a second push continues from the
first, one undo per push walking back through three of them, every refusal leaving `SOLIDPROPS`
unchanged, and the `.gs` round-trip reloading the **pushed** geometry rather than the box it was
built as.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean.
- **testing** — PASS. `ctest` **1133/1133**.
- **architecture-review** — PASS. The operation is pure `brep`, takes a `const Solid&` and returns a
  fresh one, and never mutates its input — which ADR-046 (d) requires and which
  `shared_ptr<const Solid>` makes load-bearing for undo. The command layer owns the undo snapshot and
  the store swap, as every other feature operation does.
- **code-review** — self-run. Its finding is the wedge-vs-cylinder correction above.
- **performance-review** — PASS by inspection. The precondition is O(faces × loop length) over one
  solid, run once per command; nothing is on a per-frame path.

## Acceptance criteria status (#148)

| # | criterion | status |
|---|---|---|
| 1, 2 | selection and highlight | met by TASK-199/200 |
| 3 | 3D grips move faces, edges and vertices; the solid stays valid | **geometry and typed command met for FACES.** The grip DRAG is increment 2; edges and vertices move nothing yet |
| 7 | every direct edit is one undoable step | **met** for push/pull |
| 8 | edited solids survive `.gs` save/reopen | **met** |
| 4, 5, 6 | gizmo, fillet, chamfer | later slices |

## Not covered by test, stated plainly

- **The "Validate misses it" measurement is not a regression test**, and cannot be: asserting it
  would mean shipping the code path without the precondition. The numbers are recorded in the ADR,
  the REQ, the test header and here; the *refusal* is what the suite pins.
- **No GUI path yet.** `PRESSPULL` is typed. The drag is increment 2.

## Technical debt

- **DEBT-1 — edges and vertices cannot be moved.** #148 criterion 3 names all three. A vertex drag
  is a different geometric problem again (every face meeting it must be re-solved), and an edge drag
  is a third. Only faces are done.
- **DEBT-2 — a curved wall cannot be pushed.** Refused by name. It is a radius change, and the caps'
  boundary arcs must be re-solved rather than translated.
- **DEBT-3 — the precondition is conservative.** A neighbour that is a plane *containing* the push
  direction is accepted; everything else is refused, including cases a cleverer implementation could
  handle by re-solving one neighbour. Refusing is the right default while the alternative is silent
  geometric corruption.
