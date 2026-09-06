# TASK-207 — push/pull a solid's face (REQ-319, issue #148 Phase 5 slice 3)

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

---

## Increment 2 — the grip drag

Criterion 3's wording ("3D grips move faces") in the form it implies: a handle on the selected face,
dragged along its normal, committed with a click.

### Interaction

Click-arm, move, click-commit — **the idiom `entityGripMoveActive` already uses**, so a solid's grip
behaves like every other grip in the program rather than being the one that wants a held button.
`Esc` cancels, and it is a *true* cancel: nothing in the store moves until the commit, so abandoning
a drag costs no undo step. The grip check runs **before** everything else on a click, in both
directions — a live drag consumes the click as its commit, and an idle click on the handle arms one
— because the handle sits ON the face it belongs to, and checked later the plain-click branch would
clear the very selection the handle belongs to.

A click without having moved is treated as a cancel rather than a zero-distance push. The kernel
would refuse that by name, and "I changed my mind" should not produce an error message.

### Three things worth recording

1. **The drag reads the cursor every frame, ungated.** The hover pick is behind
   `HoverPickGateShouldRun`; this deliberately is not, because a handle that lags the pointer reads
   as a stuck drag. It is affordable for the reason the gate exists to protect against does not
   apply: a skew-line solve searches no geometry.
2. **The preview translates the face's boundary; it does not rebuild the solid.** `PushPullFace`
   copies the whole solid and validates it — the right cost once on commit, the wrong cost on every
   frame of a drag. Translating the boundary shows exactly where the face will land at a cost that
   does not grow with the solid, and the real geometry is still computed once where a refusal can
   be reported (ADR-046 (d)).
3. **The drag and `PRESSPULL` commit through one function.** `CadApplyPushPull` was extracted from
   the command for this, so the two cannot diverge about what a push does — REQ-318 item 1's
   single-implementation rule applied to the edit rather than to the pick.

Also: a handle appears only when **exactly one** face is selected. `PRESSPULL` refuses to move two
faces at once, and offering a gesture the commit would decline is worse than offering none.

### Test

`SubObjectSelectionTests` gains three cases (12 total, 241 assertions). The drag itself is a mouse
gesture and stays GUI-only, but everything it computes is asserted: the handle sits **on** the face
at its centroid with the axis pointing outward; a side face's axis follows the face rather than the
world (a grip that always slid along Z would pass the top-face case and fail this one); the distance
is the unclamped, signed closest approach of the cursor ray to the axis, unchanged by sideways offset
so a drag works from any camera angle; a ray sighting straight down the axis is **refused** rather
than answered, because there is no closest point and the caller holds its last value instead of
snapping to zero as the camera swings through; and the shared commit replaces rather than mutates,
carries the selection forward, and leaves the document untouched on a refusal.

`ctest` **1136/1136**.

### Debt

- **DEBT-4 — no numeric entry during a drag.** AutoCAD lets you type a distance mid-gesture. Here the
  drag is mouse-only and `PRESSPULL` is keyboard-only; they meet at the same commit but not in the
  same gesture.
- **DEBT-5 — no ORTHO or snap interaction.** The distance is whatever the cursor ray resolves to.

---

## Increment 3 — a cap beside a curved wall (D-2026-09-04-d)

Reported by the user: push/pull not working on cylinders or cones. A cap is a plane, so it always
passed the face test; what refused it was the wall beside it.

### A third kind of move

A curved surface cannot be **intersected** the way a plane can — but it can be
**re-parameterised**, and that turned out to be a move this operation did not have:

| move | what it applies to |
|---|---|
| translate | the plane being pushed |
| re-solve | a corner, as the meeting point of the planes around it (increment 2) |
| **re-parameterise** | a curved wall: a cylinder's stored height, a cone's end radius |

A cylinder's `height` grows or shrinks, and its frame **origin travels too** when the moving cap is
the base — that origin *is* the base centre, and moving only one of the two leaves the wall spanning
the wrong interval. The volume is what distinguishes those two mistakes from each other, which is why
the bottom-cap case asserts it.

### The taper, put to the user

Pushing a cone's cap could keep the **slope** (so the radius changes) or keep the **radius** (so the
slope changes). **The user chose keep the slope**, so a push extends the same cone rather than
bending its wall. Not a rounding preference: on the worked example — base 5, top 2, height 10 pushed
to 12 — the two answers are **426.75 against 490.09**, 13% apart. The test asserts the first and
names the second, so the assertion distinguishes the decision that was made from the one that was not.

### The number that measures the whole thing

Leave the wall's height alone while its boundary moves and the push **still builds and still passes
`Validate`**, reporting **863.938 against a true 1021.02**. That figure now appears in the tests as
the value the result must *not* be, so a regression that reintroduces it is recognisable on sight
rather than merely failing.

The guarantee is also now **positive rather than defensive**: the wall is made to match its boundary,
instead of the move being declined because it might not.

### Still refused, each by name

A curved **wall** pushed along its own normal (a radius change, not a translation of anything); a
**sphere or torus** (no height or taper to follow); an axis **oblique** to the push; and a cap whose
corners also touch an unrelated **plane** — that carries a plane constraint and a surface one at
once, which is a different solve.

### Coverage now

| solid | increment 1 | increment 2 | increment 3 |
|---|---|---|---|
| Box | 6/6 | 6/6 | 6/6 |
| Wedge | 2/5 | 5/5 | 5/5 |
| Pyramid | 0/6 | 6/6 | 6/6 |
| Cylinder | 0/4 | 0/4 | **2/4** (both caps) |
| Cone | 0/4 | 0/4 | **2/4** (both caps) |

### Verification

`ctest` **1157/1157**. Volumes asserted as closed forms — `pi r^2 h`, `pi h/3 (R^2 + Rr + r^2)` —
not figures recorded from the output, which matters more than usual because the wrong answer this
replaces was plausible to four significant figures.

**Two transcript details recorded rather than worked around:** the wedge case needs
`VIEWANGLES 135 20` and the cylinder-wall case needs the point at `(0,-5,5)`, because at the default
camera those faces are on the far side and a ray aimed at them picks the near face instead — exactly
what happens to a user who has not orbited. And the `PYRAMID` **command's** radius argument is the
inradius where `brep::MakePyramid`'s is the circumradius, so the same numbers build a frustum of
twice the volume; neither is wrong, and the discrepancy looks like an error until you know which each
takes.

### Debt

- **DEBT-6 — a curved wall still cannot be pushed.** It is a radius change: the wall's radius and
  both caps' boundary arcs move together, and the caps' planes stay. Well-defined, and its own slice.
- **DEBT-7 — a sliced cylinder's cap is refused.** Its corners touch both a plane and a curved
  surface, and satisfying both at once is a solve neither existing path performs.

---

## Increment 4 — a cylinder wall pushes by changing its radius (D-2026-09-04-e)

The fourth kind of move, and the one least like a push.

### A wall push is not a translation

The outward normal points a different way at every point of the surface, so there is no single
direction to move along. What the gesture means is that **every point moves along its own normal by
the same amount** — which is exactly what adding to the radius does. That is why it needs its own
path rather than a wider tolerance somewhere.

The operation's four moves are now:

| move | applies to |
|---|---|
| translate | a plane |
| re-solve | a corner, as the meeting point of the planes around it |
| re-parameterise | a wall's height or taper, when a cap moves |
| **re-radius** | a cylinder wall |

### The sign, which would have shipped wrong quietly

`Surface::inward` (REQ-314 B2a) says the material is on the −radial side. **A hole's wall has its
outward normal pointing at the axis**, so pushing it outward makes the hole *smaller* and the solid
*heavier*. Taken from the stored axis without honouring the flag, a boss would still grow correctly
and every hole would grow backwards — a feature that works on the half of the cases anyone demos.

Measured on a real `BooleanSubtract` result rather than a hand-set flag, because the flag alone is
not the case: what has to hold is that this reads the same geometry the Booleans produce. A
20×20×10 box with an r=3 hole is 3717.26; pushing the hole's wall +1 gives **3874.34** (r=2), where
the wrong sign gives **2977.4** (r=4). Both plausible, one right.

### Both halves follow together

A full cylinder is stored as two half-faces sharing one surface. Updating one would leave two
different radii meeting along a seam — and `Validate` would not object, because it never compares a
face's surface against its neighbour's. The same blind spot this whole amendment is built around,
surfacing a third time.

### The cone stays refused, and that is a decision

Offsetting a cone along its own normal moves **both** radii by `d / cos(half-angle)` and leaves the
apex where it was. That is an offset surface, not a radius change, and which of the two a drag should
mean is exactly the fork D-2026-09-04-d had to put to the user for the cap taper. Not settled, so not
guessed.

### Renamed

`PushPullFaceNotPlanar` → `PushPullFaceKindUnsupported`. It no longer means "not a plane" now that
one curved kind is supported, and a refusal whose *name* is wrong is worse than one whose message is
vague.

### The grip

A wall gets a handle too, sitting **on** the surface at the middle of the face's own angular span and
half way up, with a **radial** axis. Mid-span rather than anywhere, because a handle at the edge of
the span sits on the seam and reads as belonging to the neighbouring half. A grip that reused the
surface frame's `zAxis` would point up the cylinder and drag the wall along its own length, which
changes nothing at all — so the test asserts the axis is perpendicular to Z rather than merely
non-zero.

### Coverage

| solid | inc 1 | inc 2 | inc 3 | inc 4 |
|---|---|---|---|---|
| Box | 6/6 | 6/6 | 6/6 | 6/6 |
| Wedge | 2/5 | 5/5 | 5/5 | 5/5 |
| Pyramid | 0/6 | 6/6 | 6/6 | 6/6 |
| Cylinder | 0/4 | 0/4 | 2/4 | **4/4** |
| Cone | 0/4 | 0/4 | 2/4 | 2/4 |

`ctest` **1158/1158**.

### Two older test sections inverted rather than deleted

Both asserted the wall was refused. Kept and turned around, because the cap and the wall side by side
are what show they are **different operations**: a cap push moves a plane and leaves the radius; a
wall push changes the radius and leaves the planes. Same gesture, same command, mirror geometry.

### Debt

- **DEBT-8 — a cone wall.** Needs the offset-vs-radius fork decided first.
- **DEBT-7 still stands** — a sliced cylinder's cap, whose corners carry a plane constraint and a
  surface one at once.
