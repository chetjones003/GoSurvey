# TASK-073 — Fix the surface rebuild worker's lifetime and the boundary Show mask; add surface commands

- Type:    bug (parts A, B, E, F) + feature (part C)
- Status:  submitted
- Opened:  2026-08-19
- Owner:   chetjones003 (review of PR #65 before merge)

## 1. Authority
- Goal:         GOAL-NN — surfaces (REQ-067…075 cluster)
- Requirements: REQ-069 (accepted) — surface definition: point groups, breaklines, boundaries,
                dynamic rebuild. REQ-203 (accepted) — the command layer is drivable without a
                window. REQ-201 (accepted) — every submitted command reports.
- Constraints:  CON-07 (artifacts to the build directory); architecture §8 (one-shot worker),
                §11.5 (replace the shared pointer, never write through it).
- Acceptance (restated, the clauses this task turns from manual into checkable):
    - REQ-069: "boundaries apply in definition order"; a `show` boundary "restores surface" where a
      `hide` removed it; "deleting a polyline used as a breakline removes it from the definition";
      "the in-flight result is discarded" on undo or a further edit.
    - REQ-203: the command layer is drivable with no window — a transcript reaches the behaviour.
- Owning subsystem: `util/tinbuild` (parts B, E, F), `commands/CadCommands` + `app/main` (parts A, C).

## 2. Scope
- In scope:
    - **A.** Join the background surface-rebuild worker instead of destroying it joinable.
    - **B.** Stop a `show` boundary restoring triangles an `outer` boundary clipped away.
    - **C.** Command-line coverage for every surface operation, so REQ-069's acceptance is
      reachable from the REQ-203 driver rather than only from a human at the Surfaces panel.
    - **D.** (adjacent, one line each) SURFELEV's two missing viewport-dispatch entries — the
      pre-existing TASK-055 gap PR #65 found and recorded but did not fix.
    - **E.** Split a constraint at the vertices lying on it, so a breakline through its own shots is
      honoured as a chain instead of being reported as an unenforceable single edge.
    - **F.** Replace the mesh-wide scan in constraint insertion with a corridor walk and a flip
      queue, removing the per-unenforceable-constraint cost cliff. Authorised by the user after the
      finding was reported with measurements — see SPEC GAP 3.
- Out of scope:
    - Genuine cooperative cancellation inside `BuildTin` — see SPEC GAP 2.
    - Reordering definition items (REQ-075's "reorder"); only add/remove are covered.
    - Inserting the intersection VERTEX where two constraints genuinely cross. Both are still
      unsatisfiable together; the mesh keeps whichever was inserted second and the other is now
      correctly reported (part F's verification pass). Doing better means adding a point that is in
      no point group, which changes what a surface's vertices mean — a spec question, not a fix.
- Smallest change: a destructor; a second inclusion mask; six registry entries and one dispatch arm;
  a per-constraint split scan feeding the existing single-edge enforcement.

## 3. Architectural boundary check
- Part A — no. A destructor restoring the class invariant the struct already documented.
- Part B — no. A local correctness fix inside `TinCullByBoundaries`, no signature change.
- Part C — **yes, and it was escalated rather than assumed.** New commands are a public-API
  (command-surface) change, which CLAUDE.md §3 reserves to the Specification layer. Recorded as
  SPEC GAP 1; the user directed the change explicitly, which is the recorded decision.
- Part D — no. Adding an existing `Kind` to the two lists every point-picking command appears in.
- Part E — no. A pre-step feeding the existing single-edge enforcement; no signature change.
- Part F — **yes.** Swapping how constraint edges are located and flipped is an algorithm change.
  Reported with measurements and authorised by the user; recorded as SPEC GAP 3.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Adding surface commands is a command-surface change (CLAUDE.md §3). Proceed? | 2026-08-19 | Yes — user instruction: "add all necessary commands to drive surface creation, surface editing, breaklines etc". Recorded as the decision; see SPEC GAP 1. |

## 5. Assumptions

```
ASSUMPTION-1: Commas separate the arguments of the new multi-argument surface commands.
- Because:       surface and point-group names contain spaces ("Existing Ground", "Ground + Curb"),
                 so whitespace cannot separate them, and DESIGNATEBOUNDARY's "kind is the last
                 token" trick does not generalise to a variable-length group list.
- Risk if wrong: a name containing a comma cannot be typed. No such name exists in any sample.
- Validate by:   the transcript builds surfaces from both a one-word and a spaced group name.
```

## 6. Plan
- Approach: fix A in the struct that owns the thread; fix B inside the culling function; add C as a
  single dispatch arm beside the existing DESIGNATE* arm, with the command bodies in an anonymous
  namespace next to the other surface functions.
- Files touched: `src/commands/CadCommands.hpp`, `src/commands/CadCommands.cpp`,
  `src/util/tinbuild.cpp`, `src/app/main.cpp`, `src/ui/CadUi.cpp`, `CMakeLists.txt`,
  `tests/TinConstraintTests.cpp`, `tests/SurfaceRebuildLifetimeTests.cpp` (new),
  `tests/headless/transcripts/req069-surface-definition-commands.txt` (new).
- Test approach:
    - happy path = the transcript drives create → designate → rebuild → list → undesignate →
      rename → round trip → delete against `samples/surface-demo.gs`.
    - failure mode = each fix's regression test was run against the UNPATCHED code and observed to
      fail (see §8), which is the only thing that makes it an oracle.

## 7. Workflow-specific notes (Bug)
- **A — root cause.** `SurfaceRebuildAsync` owns a `std::thread` and had no destructor. The only
  join site (`TickSurfaceRebuilds`) is reachable only once `done` is already set, so any path that
  dropped a job mid-flight destroyed a joinable thread → `std::terminate`. `main()` holds
  `AppCommandState` as a local (`main.cpp:259`), so `return 0` reached it; `cadGpuRevision` moves on
  every drawing mutation, so "edit, then close the window" was the trigger.
  Reproduced standalone: exit code `0xC0000409`, the line after the scope never printed.
- **B — root cause.** `Outer` and `Show` shared one `included` mask, so `included[t] = 1` could not
  tell which loop had removed a triangle. A `Show` ring outside an `Outer` ring therefore restored
  clipped-away surface. Measured on a 6x6 grid: outer(left half) 50 → 25 triangles; adding a show
  ring in the right half → back to 50.
- Regression tests fail before the fix? **Yes, both, verified by reverting each fix in turn** (§8).

## 8. Implementation log
- 2026-08-19 Reviewed PR #65. Four findings; user selected two to fix (A, B) plus part C.
- 2026-08-19 A: added `~SurfaceRebuildAsync` (sets `cancel`, joins if joinable). Added an explicit
  `surfaceRebuildAsync.clear()` in `main.cpp` after the frame loop — the destructor is the safety
  net for every path, but it runs after `glfwTerminate()`, so waiting there would look like a hang
  with the window already gone.
- 2026-08-19 B: split `included` into `insideOuter` (extent, Outer only) and `shown` (voids,
  Hide/Show in definition order); a triangle is kept when both hold.
- 2026-08-19 C: added SURFACECREATE / SURFACERENAME / SURFACEDELETE / SURFACEREBUILD / SURFACELIST /
  UNDESIGNATE with aliases. SURFACEREBUILD is synchronous (`BuildSurfaceFromSources`) because the
  driver has no frame loop to run `TickSurfaceRebuilds` from — that is what makes the rebuild
  assertable at all headlessly.
- 2026-08-19 D: added `K::SurfaceElevGrade` to `CommandExpectsPointEntry` AND to the viewport
  point-pick dispatch list. It was missing from both, so SURFELEV had neither typed point entry nor
  a working pick.
- 2026-08-19 Oracle check, part B: reverted the mask split, rebuilt → `CHECK( cull({outer,
  showRight}) == clipped )` FAILED with `28 == 16`. Restored.
- 2026-08-19 Oracle check, part A: commented out the join, rebuilt → the whole test binary exited
  `0xC0000409` with no output, exactly the severity being guarded. Restored.
- 2026-08-19 E: extracted single-edge exposure into `enforceEdge(ia, ib)`, then split each
  constraint at the vertices on it and enforced the chain. A constraint counts as unresolved once
  however many of its links failed, so the reported number stays "constraint edges", not flips.
  Enforcement continues past a failed link rather than abandoning the rest of the breakline.
- 2026-08-19 Oracle check, part E: disabled the split, rebuilt → exactly the two intended cases
  failed (`TinConstraintTests.cpp` 335 and 369) while the "genuine failure still reported" and
  "nearby vertex not split at" cases kept passing, which is the right split of responsibilities.
  Restored.
- 2026-08-19 Measured part E's cost before claiming it was free, and found a pre-existing
  performance cliff in the process (recorded below). Part E is a net improvement on it.
- 2026-08-19 F: replaced the mesh-wide scan with a vertex→triangle index, a corridor walk, and
  Sloan's flip queue. The queue was necessary, not stylistic: re-walking after each flip livelocks
  (proved by instrumenting the bail-out paths — none fired — and by a five-million-flip budget that
  still left the same constraints unresolved).
- 2026-08-19 F also moved the unresolved count to a verification pass over the FINISHED mesh, after
  finding that a later constraint can flip an earlier one away and leave the count reading zero.
- 2026-08-19 Measured before/after on the same fixture: 43,093 ms/11 unresolved -> 8.6 ms/0.
- 2026-08-19 Transcript authoring caught three of my own wrong assumptions, each corrected against
  the code rather than guessed: POLYLINE commits with `END` (not bare Enter) and needs a following
  `ESC`; `PICK` takes LOCAL coordinates while typed coordinates are world (this drawing's origin is
  13.607, 0.308); and the "Curb" group is a curb LINE whose points are collinear, so it is correctly
  refused with no partial surface (REQ-001) — the transcript now uses "Ground + Curb" and asserts
  the collinear refusal separately.

## 9. Self-verification
- [x] build-project        — PASS (clean MSVC build; zero warnings in every file touched)
- [x] architecture-review  — PASS for A/B/D; C escalated, not decided by the Workshop
- [x] code-review          — PASS
- [x] dependency-audit     — n/a (no dependency added)
- [x] performance-review   — PASS, measured. Part F removes the cliff entirely: the same fixture
      goes from 43,093 ms with 11 unresolved to 8.6 ms with 0. Unconstrained builds and enforceable
      breaklines are unchanged (6.5 ms and 9.7 ms at 10,000 points), so nothing was traded away.
- [x] testing              — PASS (444 ctest cases; 427 Catch2 cases / 213,766 assertions)

## 10. Verification result
- Submitted: 2026-08-19
- Verdict:   PASS for A, B, D. **SPEC GAP** recorded for C and for cancellation.
- Findings:  the two fixed here came from a review of PR #65; the two NOT fixed are recorded below
             so they cannot be lost.

### SPEC GAP 1 — the surface command surface (part C)
`spec/requirements.md` gives surfaces a panel (REQ-075: "every REQ-069 definition operation is
reachable from the panel") and never a command. REQ-069's acceptance row consequently still reads
"manual", and REQ-083's row already records the same shape of problem for `IMPORTPOINTS`. Adding
commands is a public-API change the Workshop does not get to make on its own. It was directed by
the user (Q1) and is implemented here; `spec/requirements.md` should record the command surface
under REQ-069/REQ-075, and REQ-069's acceptance row should move from "manual" to naming
`transcripts/req069-surface-definition-commands.txt`.

### SPEC GAP 3 — the constrained-insertion algorithm (part F)
Swapping how constraint edges are found and flipped is an algorithm change, which CLAUDE.md §3
reserves to the Specification layer, and it is the kind that can trade correctness for speed if done
carelessly. It was reported to the user with measurements first and authorised explicitly, which is
the recorded decision. `spec/architecture.md`'s note on `util/tinbuild` should record that
constrained insertion is now corridor-walk plus flip-queue (Sloan) rather than a mesh-wide scan, and
that it maintains a vertex→triangle index across flips.

### SPEC GAP 2 — "cooperative cancellation" is not implemented
`AppCommandState::SurfaceRebuildAsync::cancel` was **never set to true anywhere** in the tree before
this task, and is now set only by the destructor added here — which releases a worker that has not
yet started, nothing more. It is checked once, before `RunSurfaceBuild`, so a running triangulation
cannot be interrupted. PR #65 describes this worker as "the first complete implementation of the
architecture doc's one-shot-worker contract (generation staleness + cooperative cancellation)":
generation staleness is genuinely implemented and correct, cancellation is not. Making it real means
threading a cancel token into `BuildTin` — a signature change to a pure util, i.e. an architectural
decision. Consequence today: the destructor's join is bounded by one full triangulation, so closing
during a large rebuild waits rather than crashing.

### FIXED (part E) — a false "constraint edge(s) could not be enforced"
The insertion loop terminated on `edgeExists(ia, ib)` — the constraint as a SINGLE edge — which no
triangulation can contain when a third vertex lies between the endpoints, and it never split the
constraint there. Every such constraint was counted unresolved, and that is the ordinary surveying
case: a breakline drawn along a ridge through the shots that define it.

Measured before the fix: a breakline `(0,0)-(10,0)` with a shot at `(5,0)` reported 1 unresolved;
two breaklines crossing at a shared grid vertex reported 2 — **while both were fully present in the
mesh, edge for edge**. The geometry was right and the diagnostic was wrong, and it reached the user
through `BuildSurfaceFromSources`' log line.

Now each constraint is split at every vertex within `kTinPlanEpsilon` perpendicular of it and
strictly between its endpoints (sorted along the segment), and each link is enforced by the existing
single-edge routine. Both cases now report 0. The tolerance is deliberately the same one that
decides two points are the same site: a vertex the rest of the system cannot distinguish from the
segment must not be treated as off it, or the flip loop chases an edge through a vertex it can never
cross.

Guarded against becoming a false NEGATIVE, which was the real risk of this fix: a new test asserts
the counter still fires for two breaklines crossing at (15,15), a non-vertex where nothing can be
split and no triangulation holds both edges. Another asserts a vertex 0.5 off the segment (fifty
times the epsilon) is still crossed rather than split at, so the breakline stays straight.

### FIXED (part F) — the flip loop's cost on an unenforceable constraint
Enforcing one edge called `edgeExists` — an O(triangles) scan — once per flip, and found the edge to
flip by another O(triangles) scan, inside a loop bounded by `4 * triangles + 64`. A constraint that
could not be enforced burned the whole budget at O(T) each.

Three things replaced that, all local rather than mesh-wide:
1. a **vertex→triangle index**, rebuilt once before insertion and repaired by each flip (four
   entries), so an edge lookup costs the vertex's degree instead of the triangle count;
2. a **corridor walk** — from `ia`, find the triangle the segment leaves through, then follow the
   adjacency links across each crossed edge to `ib` — so finding the crossing edges costs the number
   of crossings instead of the triangle count;
3. **Sloan's flip queue**, holding crossing edges as vertex pairs (a slot/edge index does not
   survive a flip): pop one, flip when its quad is convex and re-queue the new diagonal only while
   it still crosses, otherwise re-queue it to retry once neighbours have opened it up.

Point 3 was not optional and cost a full diagnostic cycle to find. Re-deriving the corridor after
every flip — the obvious approach, written first — **livelocks**: a fresh walk keeps re-proposing the
edge just flipped back. Measured with a five-million-flip budget: the same constraints stayed
unresolved and the fixture took 750 ms instead of failing fast. Instrumenting the three bail-out
paths showed none of them firing, which is what proved it was the budget and therefore a cycle,
rather than a corridor that could not be built.

Measured on the same fixture, 10,000 points / ~20,000 triangles:

| | before (HEAD, part E) | after (part F) |
|---|---|---|
| unconstrained build | 6.3 ms | 6.5 ms |
| 99 enforceable breakline segments | 10.2 ms | 9.7 ms |
| 60 segments, some unenforceable | **43,093 ms**, 11 unresolved | **8.6 ms**, 0 unresolved |

About 5,000x on the pathological case, and it now enforces constraints the old loop gave up on
rather than trading accuracy for speed.

### Also fixed by part F — a constraint could vanish with nothing reported
Found while testing the above, and a defect in its own right. `constraintsUnresolved` was incremented
from what each insertion reported **at the time it ran**, but enforcing a later constraint can flip an
earlier one away: two breaklines that genuinely cross cannot both be edges, and whichever is inserted
second wins. So a breakline could disappear from the mesh with the count still reading zero — and
`TinFindCrossingConflicts` stays silent by design when their elevations agree. The count is now taken
at the end, over the finished mesh, by checking every chain link of every constraint. That is the
only count that matches what the message claims.

### Finding not fixed — world coordinates through a float (pre-existing, not PR #65)
`TinBuildResult::vertsXyz` is `std::vector<float>` holding WORLD coordinates. At state-plane
magnitude with fractional coordinates the worst emitted-vertex error measured 0.0150 ft against
REQ-101's 0.01 ft. Integer-valued coordinates are exactly representable, which is why it hides. It
predates this PR but is newly relevant: `TinCullByBoundaries` decides a triangle by comparing a
float-derived centroid against an exact-double boundary ring.

## 11. Outcome
- Requirements satisfied: REQ-069 (A, B, E — Acceptance met: yes), REQ-203 (C — the definition
  operations are now drivable), REQ-074 (D — SURFELEV's picks work at all).
- Tests added: `tests/SurfaceRebuildLifetimeTests.cpp` (2 cases); six cases in
  `tests/TinConstraintTests.cpp` (one for B, four for E, one scale/consistency case for F);
  `transcripts/req069-surface-definition-commands.txt` (48 steps).
- Docs updated: this task log.
- Done: pending user review.
