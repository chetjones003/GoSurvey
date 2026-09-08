# TASK-222 — CHAMFER a solid edge: the kernel (REQ-329 increments 1 and 2)

## Requirement authority

- **REQ-329** — accepted 2026-09-08, D-2026-09-08-b. Increments 1 (one straight convex edge) and 2
  (chains and corners).
- **ADR-046 amendment (k)** — the architectural decision behind it; amendment (i) for the
  bring-your-own-precondition rule, amendment (j) for the topology-adding operation and the one-pass
  rule this reuses.
- **GitHub issue #148 Phase 5, slice 7** — the last of the seven slices the issue's own
  pre-implementation survey listed. Closes acceptance 5 (*"fillet and chamfer ... on single edges
  and on edge chains"*) for the chamfer half; REQ-323 closed the fillet half.

## Files affected

- `src/util/brep.hpp` — five new `Problem` values, `ChamferEdge` / `ChamferEdges` declarations.
- `src/util/brep.cpp` — `ProblemMessage` cases; the `ChamferEdgesGeneral` builder.
- `tests/ChamferEdgeTests.cpp` (new) — the closed-form and refusal cases.
- `CMakeLists.txt` — register it.

**As planned, the command half was to be a separate task**, matching how TASK-210 (kernel) and
TASK-217 (command) split the fillet.

**Revised during implementation: it came in here instead.** The fillet needed the split because both
halves were substantial. The chamfer's command half is the fillet's own three command functions with
a different verb, and splitting it would have shipped a kernel no user could reach. Files added to
the list on that decision: `src/commands/CadCommands.{hpp,cpp}`, `src/ui/CadUi.cpp` (one term, which
discharges TASK-221 DEBT-1), and `tests/headless/transcripts/req329-chamfer-solid.txt`.

## Approach

Mirror `FilletEdgesGeneral`'s skeleton, because REQ-329 item 3 / amendment (k)(1) is that the
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

Arithmetic written before the code, per REQ-329's acceptance, which is the discipline that caught
two wrong numbers in REQ-323 increment 1:

- 20 x 10 x 8 box, one 20-long edge, `d = 2` → volume **1560**, area **796 + 40*sqrt(2)**,
  topology 7/15/10, `Validate` = `Ok`;
- all twelve edges, `d = 2` → volume **1344**, area **368 + 232*sqrt(2)**, topology 32/48/18,
  6 planes + 12 planes and no other surface kind, `V - E + F = 2`;
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
(TASK-210, 217, 218, 219, 220, 221); this needed one, because REQ-329 item 3 turned out to be
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
construction. The chamfer keeps the fillet's restriction. Recorded in REQ-329 item 10, ADR-046
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

- **DEBT-1 — the AutoCAD two-distance / base-face form**, deferred by D-2026-09-08-b item 13. The
  coupling is the part to remember: with `D1 != D2` the corner is still a single point, but the cut
  vertex `p + d*u1 + d*u2` is no longer the meet of the two cut lines, and increment 2's closed forms
  are replaced by a second set.
- **DEBT-2 — the pick cannot name all twelve edges from one camera.** The transcript needs two view
  angles, and a first draft silently got nine because a point on an occluded face resolves to
  whatever the ray reaches first rather than missing. That is a REQ-318 property, not a REQ-329 one,
  but it is a trap the next transcript will hit too.
- **DEBT-3 — concave edges, curved edges, oblique and curved end faces, partial and non-orthogonal
  corners.** Each refused by name, each its own increment, each with its reason recorded.
