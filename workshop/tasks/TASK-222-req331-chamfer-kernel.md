# TASK-222 — CHAMFER a solid edge: the kernel (REQ-331 increments 1 and 2)

## Requirement authority

- **REQ-331** — accepted 2026-09-08, D-2026-09-08-f. Increments 1 (one straight convex edge) and 2
  (chains and corners).
- **ADR-046 amendment (k)** — the architectural decision behind it; amendment (i) for the
  bring-your-own-precondition rule, amendment (j) for the topology-adding operation and the one-pass
  rule this reuses.
- **GitHub issue #148 Phase 5, slice 7** — the last of the seven slices the issue's own
  pre-implementation survey listed. Closes acceptance 5 (*"fillet and chamfer ... on single edges
  and on edge chains"*) for the chamfer half; REQ-323 closed the fillet half.

## Files affected

- `src/util/brep.hpp` — eleven new `Problem` values, `ChamferEdge` / `ChamferEdges` declarations.
- `src/util/brep.cpp` — `ProblemMessage` cases; the `ChamferEdgesGeneral` builder.
- `tests/ChamferEdgeTests.cpp` (new) — the closed-form and refusal cases.
- `CMakeLists.txt` — register it.

**As planned, the command half was to be a separate task**, matching how TASK-210 (kernel) and
TASK-217 (command) split the fillet.

**Revised during implementation: it came in here instead.** The fillet needed the split because both
halves were substantial. The chamfer's command half is the fillet's own three command functions with
a different verb, and splitting it would have shipped a kernel no user could reach. Files added to
the list on that decision: `src/commands/CadCommands.{hpp,cpp}`, `src/ui/CadUi.cpp` (one term, which
discharges TASK-221 DEBT-1), and `tests/headless/transcripts/req331-chamfer-solid.txt`.

## Approach

Mirror `FilletEdgesGeneral`'s skeleton, because REQ-331 item 3 / amendment (k)(1) is that the
topology delta is identical. What changes:

| | fillet | chamfer |
|---|---|---|
| new face | `Cylinder` radius `r` | `Plane`, outward normal `-normalize(u_A + u_B)` |
| end closure | `Arc` about the axis point | `Line` between the two tangent points |
| setback | `r / tan(theta/2)`, derived | `d`, given |
| tangent point, open end | `axisPt[k] + r * n[which]` | `p[k] + d * u[which]` |
| tangent point, corner | `centre + r * n[which]` | `p[k] + d*u1 + d*u2` (both edges' `u` in that face) |
| corner | +1 `Sphere` octant face | +1 vertex, +3 edges, **no face** |
| bevel/fillet face loop | 4 uses (2 lines, 2 arcs) | 4 or 6 uses (V-notch per corner end) |

`u_A` / `u_B` — each face's in-face unit perpendicular to the edge, pointing into the material —
already exist in the fillet's own precondition block (its concavity test is built on them), so
nothing new is derived.

**One pass over the original solid**, per amendment (j)(4), for the identical reason: after the
first chamfer the shared vertex is gone.

## Test approach

Arithmetic written before the code, per REQ-331's acceptance, which is the discipline that caught
two wrong numbers in REQ-323 increment 1:

- 20 x 10 x 8 box, one 20-long edge, `d = 2` → volume **1560**, area **796 + 40*sqrt(2)**,
  topology 10/15/7, `Validate` = `Ok`;
- all twelve edges, `d = 2` → volume **1344**, area **368 + 232*sqrt(2)**, topology 32/48/18,
  6 + 12 planar faces and no other surface kind, `V - E + F = 2`;
- a wedge ridge → setback exactly `d` at a 68.199-degree dihedral (item 2's proof case);
- every refusal by name: `d <= 0`, non-finite, too large (including exact equality at `d = 8`),
  concave, curved edge, curved adjacent face, parallel faces, partial corner, non-orthogonal
  corner, oblique end face.

**Cross-check available to nothing else:** the increment-2 decomposition shares its `1120` inner-box
+ slabs constant and its `368` planar-face total with REQ-323's `1120 + 344*pi/3` and `368 + 120*pi`.
Two independent operations landing on the same constants.

## Architectural-boundary check

`src/util/brep.{hpp,cpp}` stays GL-free and dependency-free; the new code adds no include and no
type. Commands gains one transient bool (`chamferSolidAwaitingDistance`) and reuses `chamferDist1`
as the remembered value rather than adding a parallel setting — the way `filletRadius` serves both
the 2D and the solid fillet. UI gains one term in an existing predicate. No abstraction introduced:
`ChamferEdge` is one call into `ChamferEdgesGeneral`, the shape `FilletEdge` already has.

## Status

Planned 2026-09-08. Implementation and verification recorded below on completion.

---

# Completion report — 2026-09-08

## What shipped

Both increments, and the command half with them, in one task. The fillet needed six tasks
(TASK-210, 217, 218, 219, 220, 221); this needed one, because REQ-331 item 3 turned out to be
literally true — the topology delta is the fillet's, so the builder is the fillet's skeleton with a
`Plane` where it has a `Cylinder` and a `Line` where it has an `Arc`, and the command half is
the fillet's own three command functions with a different verb. What was genuinely new was the corner.

**Kernel** (`brep.cpp`, +457): `ChamferEdgesGeneral`, with `ChamferEdge` / `ChamferEdges` as one call
each into it — the shape `FilletEdge` / `FilletEdges` already have. Eleven new `Problem` values with
their own `ProblemText` sentences.

**Command** (`CadCommands.cpp`, +207): `CadApplyChamferToSelectedEdges`,
`CadChamferReportEdgeSelection`, `CadChamferSolidEdges`, the `chamferSolidAwaitingDistance` prompt
state, the idle dispatch on `chamfer`/`cha`, the in-command edge-gather branch in
`HandleChamferText`, blank-Enter, ESC clearing, and the dynamic cursor text.

**UI** (`CadUi.cpp`, 1 line): `CHAMFER` joined the hover exception list, discharging TASK-221 DEBT-1.

## Three findings worth keeping

**1. The corner is a POINT, and the first version of this task's own reasoning said otherwise.** The
session's initial answer to the user was that three bevels meeting at a square corner leave a flat
triangle. Working the algebra before writing any code showed they do not: the three bevel planes
`x+y=d`, `y+z=d`, `z+x=d` are concurrent at `(d/2, d/2, d/2)`. Three planes in general position meet
at exactly one point — three cylinders do not, which is exactly why the fillet needs a spherical
patch. So the corner adds **one vertex, three edges and no face**, and each bevel becomes a
**hexagon** (a rectangle V-notched at both ends by its neighbours). Caught by derivation, not by a
test failing.

**2. An oblique end face is NOT free, and that was the second wrong first answer.** Expected to come
free — a plane cuts a plane in a straight line at any angle, where the fillet's cylinder gives an
ellipse. It is not: the cut point `p + d*u` lies on the end face only while that face is square to
the edge, because `u` is perpendicular to the edge direction and so the point keeps its position
along it. Off-square the cut lines need trimming to the bevel-plane crossing instead — a second
construction. The chamfer keeps the fillet's restriction. Recorded in REQ-331 item 10, ADR-046
amendment (k)(5), and its own test case, all three of which say the first answer was wrong.

**3. The acceptance numbers cross-check REQ-323's, and neither could do that alone.** The bevelled
box decomposes as inner box `384` + six slabs `736` + twelve triangular prisms `208` + eight corner
pieces `16` = **1344**. The first two terms sum to **1120** — the identical constant in REQ-323's
`1120 + 344*pi/3` — and the six planar faces total **368**, the identical constant in its
`368 + 120*pi`. A fillet and a chamfer share their inner box and slabs exactly and differ only in
what fills the twelve edge channels and the eight corners. Two independently derived acceptances
confirming each other.

## Verification

- **build-project** — PASS. MSVC/Ninja release, no warnings.
- **testing** — PASS. `ctest` **1325/1325** (was 1309: +15 `ChamferEdgeTests` cases, +1 transcript).
  Every closed form matched on the first run: 1560 / `796+40*sqrt(2)`, 1344 / `368+232*sqrt(2)`,
  1530 / `734+67*sqrt(2)`, 1520 / `712+80*sqrt(2)`, and topologies 10/15/7, 32/48/18, 14/21/9,
  12/18/8.
- **architecture-review** — PASS. Domain stays GL-free and dependency-free; no new include, no new
  type, no new abstraction. `ChamferEdge` is one call into the general builder, which is the shape
  the fillet already established. Commands gains one transient bool and reuses `chamferDist1` as the
  remembered value rather than adding a parallel setting — the way `filletRadius` serves both the 2D
  and the solid fillet.
- **dependency-audit** — PASS. Nothing added.
- **performance-review** — PASS. One pass over the solid, `O(E)` in requested edges with `std::map`
  lookups keyed by `(vertex, face)`; no per-frame cost, and the hover term shares
  `HoverPickGateShouldRun`'s existing ~30 Hz budget rather than adding one.
- **code-review** — `/code-review high`, findings recorded below.

## Not covered by test, stated plainly

- **The hover.** No headless verb drives a modifier-held cursor (TASK-221 DEBT-2). The CHAMFER term
  reaches byte-for-byte the same code TASK-221 verified by hand for FILLET, so it was **not
  re-verified in the GUI** — an argument from similarity, not a measurement, and recorded as such in
  TASK-221's own appendix.
- **The two-distance form.** Deferred by decision, not attempted.

## Technical debt

- **DEBT-1 — the AutoCAD two-distance / base-face form**, deferred by D-2026-09-08-f item 13. The
  coupling is the part to remember: with `D1 != D2` the corner is still a single point, but the cut
  vertex `p + d*u1 + d*u2` is no longer the meet of the two cut lines, and increment 2's closed forms
  are replaced by a second set.
- **DEBT-2 — the pick cannot name all twelve edges from one camera.** The transcript needs two view
  angles, and a first draft silently got nine because a point on an occluded face resolves to
  whatever the ray reaches first rather than missing. That is a REQ-318 property, not a REQ-331 one,
  but it is a trap the next transcript will hit too.
- **DEBT-3 — concave edges, curved edges, oblique and curved end faces, partial and non-orthogonal
  corners.** Each refused by name, each its own increment, each with its reason recorded.

---

## Code-review findings (`/code-review high`, 2026-09-08) and what was done with each

**Finding 3 — dead state. FIXED in this task.** `cornerOfVertex` was written and never read: the
fillet reads its copy back to move each plan's axis point onto the ball centre, and the chamfer has
no such step because its corner does not move the bevel planes. Carried over from the template.
Removed; suite still 1325/1325.

**Finding 1 — the distance pre-check is per-edge and per-face, so overlapping cuts are accepted.
NOT fixed here; raised to the user, because it is a SPEC question and it is not this task's bug.**

`reach()` measures how far each adjacent face extends from *this* edge along `u`. Nothing accounts
for material another requested edge takes out of the same face, or for the two ends of one edge
eating each other. `Validate` is topological, so `ChamferResultInvalid` never fires. Reproduced on a
20 x 10 x 8 box, both top edges along X (10 apart in a 10-wide top face):

| | result |
|---|---|
| `CHAMFER 6` | **accepted**, volume 880 — the two cut lines land at y = -1 and y = +1, crossed |
| `FILLET 6` | **accepted**, volume 1290.97336 — same crossing |
| `CHAMFER 5` / `FILLET 5` | refused, but only because at exactly `d = W/2` the top face becomes
  ZERO-area and `DegenerateFace` catches it. One unit either side and it is accepted. |

**The shipped FILLET has the identical defect**, because the chamfer inherited `reach` from it
verbatim. So this is a defect against **REQ-323 as well as REQ-331**, and against **#148 acceptance
6** ("a fillet or chamfer that cannot be built is refused with a clear message and leaves the solid
unchanged") for both. It was not introduced here and it is not fixable here without deciding how far
the precondition should reach — which changes two accepted requirements and touches five PRs already
open for review. Recorded, reproduced, and put to the user rather than guessed at (CLAUDE.md §5).

**Finding 2 — a refused distance is followed by a contradictory "could not parse" line. NOT fixed
here; same reason, smaller stakes.** `HandleChamferText` returns false after the kernel refuses, and
the caller appends `"Could not parse CHAMFER input — see command hints."` So a user typing `8` at
the prompt sees the refusal by name, then "specify a different distance", then a third line saying
the input did not parse — which is false, it parsed fine and the kernel refused it. **FILLET does
exactly the same thing today**, so fixing it for CHAMFER alone would make the two diverge on a
shipped message. One line each; bundled with Finding 1 for the user's call.

## Technical debt (continued)

- **DEBT-4 — the precondition does not see other requested edges (Finding 1).** HIGH. Shared with
  the shipped fillet.
- **DEBT-5 — the contradictory "could not parse" trailer (Finding 2).** LOW. Shared with FILLET.

## What the review checked and found correct

Worth recording, because these are the parts that would have been expensive to get wrong: the bevel
faces' loop orientation (including the corner spokes coming out opposite between the two bevels that
share them, verified analytically in both `face[0]`/`face[1]` orderings); the bevel plane being
exactly equidistant from both cut lines; the three-plane corner point genuinely lying on all three
bevel planes, so the hexagon is planar; the gap-closing insertion handling the last-to-first wrap and
running before the bevel faces are appended; and the arc re-anchor block being unreachable here (the
end-face check forces a planar end face, so every unrequested edge at an open end is a straight
plane-meets-plane line).

---

**DEBT-4 CLOSED 2026-09-08** by TASK-223 (D-2026-09-08-g, ADR-046 amendment (l)), on the user's
instruction to fix it for the fillet as well as the chamfer rather than only where it was found.
REQ-323 and REQ-331 item 4 are both amended as item 4b, the two operations now share one
precondition implementation, and four new refusals name the two new failures. Suite 1333/1333.

**DEBT-5 stands** — the contradictory "could not parse" trailer after a refused value, shared with
FILLET.

**DEBT-5 CLOSED 2026-09-08** by TASK-224, on the user's instruction. `Handle*Text`'s return value now
means "was this input understood" rather than "did the command advance" — the contract that was never
written down, which is how the two readings coexisted — so a refused value no longer earns a "Could
not parse" trailer. FILLET and CHAMFER fixed together. The headless driver gains `EXPECT NOLOG`,
scoped to the most recent command, and the assertion was proven to fail when the defect is put back.
