# TASK-072 — REQ-069: surface breaklines, boundaries, dynamic rebuild

- Type:    feature
- Status:  done — all phases (A/B/C/D/E/F) complete, self-verified, GUI-confirmed end to end
- Closed:  2026-08-18
- Opened:  2026-08-18
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         M-Surfaces step 5 (roadmap) — the last step blocking styles/contours/volumes.
- Requirements: **REQ-069** (accepted 2026-08-12)
- Constraints:  REQ-101 (±0.01 ft), REQ-076 (stable entity id — breaklines/boundaries reference by
                id, never index), REQ-201 (report, never absorb), REQ-300, REQ-301, ADR-027, ADR-028,
                architecture §8 (async worker contract), §11.4 (no new abstraction), §11.5 (shared_ptr
                exemption for the TIN), §11.8 (interleaved XYZ), §11.9 (reference by stable id)
- Acceptance, restated verbatim:
  - a breakline across a saddle produces triangle edges along it, and no triangle crosses it,
    verified against hand-computed expected edges on a committed dataset;
  - an outer boundary clips the surface to itself; a hide boundary leaves a void; a show boundary
    inside a hide restores surface there;
  - moving a survey point the surface consumes changes the surface with no manual rebuild;
  - a single MOVE of N consumed points triggers one rebuild, not N;
  - undo issued while a rebuild is in flight leaves the surface consistent with the undone state —
    the in-flight result is discarded;
  - deleting a polyline used as a breakline removes it from the definition, and the surface rebuilds
    without it, with no dangling id;
  - crossing breaklines at different elevations produce a named diagnostic and a stated outcome;
  - a definition of fewer than three non-collinear points fails with a specific message and leaves no
    partial surface;
  - the definition round-trips `.gs`, ids intact.
- Owning subsystem: Domain (definition + rebuild, in `AppCommandState`/`CadSurface`), `util/tinbuild`
  (constrained triangulation, boundary culling — pure, GL-free), Commands (designate breakline/
  boundary, dirty-marking, the async rebuild worker), IO (`.gs` persistence of the new fields).

## 2. Scope
- In scope: everything REQ-069's acceptance list states, per the phased plan in §6.
- Out of scope: the Surface Manager panel (REQ-075 — full definition-item list editing, reorder,
  stale/rebuilding UI polish); styles/contours (REQ-070/071); the hide-boundary half of REQ-074's
  "outside surface" condition (TASK-055 ASSUMPTION-1 — this task is what makes it reachable, but
  re-verifying REQ-074 against it is not repeated here since REQ-074 already ships and its own tests
  are untouched).
- Smallest change: extend the existing `CadSurface`/`BuildSurfaceFromSources`/`AsyncBuild` shapes
  rather than introducing a parallel system for surfaces.

## 3. Architectural boundary check (workflow.md §4)
- New abstraction / layer / dependency / ownership change / global state / public-API or data-format
  change / algorithm the spec didn't specify?
    - [x] **No — proceed.** ADR-028(c) already committed to an in-tree constrained triangulator for
      exactly this requirement; ADR-028(e) already named `AsyncBuild` as the pattern for a surface
      rebuild worker, "its second concrete use"; architecture §8 already documents the full worker
      contract (generation counter + cooperative cancellation) as mandatory — this task is the first
      *complete* implementation of an already-specified pattern, not a new one. `CadSurface` gains two
      definition-item fields (extending an existing struct); `BuildTin` gains an optional parameter
      (existing callers unaffected, verified — see §8 below).

## 4. Questions (workflow.md §5)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Boundary polylines: approximate (hull-jagged) clip, or exact clip along the boundary polyline? | 2026-08-18 (self-resolved, not user-escalated — a Workshop implementation choice within ADR-028(c)'s already-approved "constrained Delaunay," not an architectural one) | Boundary ring edges are fed into the **same** constrained-triangulation pass as breaklines, so the mesh conforms exactly to the boundary; culling then removes whole triangles by centroid test against that exact boundary. Standard approach (mirrors Civil 3D treating boundaries as breaklines for triangulation purposes). No new geometry-clipping machinery beyond constrained Delaunay itself. |

## 5. Assumptions
```
ASSUMPTION-1: Deletion of a referenced entity is handled by re-resolving the definition at rebuild
              time (dropping ids that no longer resolve), not by an active prune-on-delete hook.
- Because:       This is the codebase's existing idiom for id references (SurveyPoint::labelMtextAnnId
                 via FindSurveyLabelAnnIndex, and ADR-028(g) point groups) — lazy resolution, not
                 active pruning. Confirmed by research: no existing code walks a reference list
                 removing entries whose id no longer resolves.
- Risk if wrong: REQ-069 says deletion "removes that item from the definition" — read literally this
                 could mean the STORED list must shrink, not just that resolution silently skips it.
- Validate by:   the definition-item list IS actively pruned during rebuild (dropping unresolvable ids
                 from CadSurface's own arrays, not just skipping them for triangulation) — see §6 Phase
                 A step 3 — so both readings are satisfied: nothing dangling is left in the stored
                 definition by the time a rebuild has run once, and "no manual action" (REQ-069's own
                 dynamic-rebuild requirement) is what triggers that rebuild.
```

## 6. Plan (workflow.md §6)
- Approach: five phases, each independently testable, in dependency order.
- Files/functions to touch:
  - `src/util/tinbuild.{hpp,cpp}` — DONE (Phase B/C).
  - `src/commands/CadEntities.hpp` — `CadSurface` gains breakline/boundary definition fields
    (Phase A).
  - `src/commands/CadCommands.{hpp,cpp}` — `BuildSurfaceFromSources` resolves breaklines/boundaries
    via `FindEntityById`, prunes dangling ids, calls the extended `BuildTin` + `TinCullByBoundaries`
    (Phase A); dirty-marking at survey-point and line/polyline mutation sites, the async rebuild
    worker (`AppCommandState::SurfaceRebuildAsync` or similar, modelled on but not shared with
    `pdfAttachAsync`), designate-breakline/designate-boundary commands, coalescing (Phase D).
  - `src/io/GsIo.*` (or wherever `.gs` surfaces already serialize) — persist the new fields, legacy
    load defaults to none (Phase E).
  - `tests/TinConstraintTests.cpp` — DONE (Phase B/C, 7 cases / 68 assertions).
  - New/extended command + IO tests for Phases A, D, E.
- Test approach: happy path = each acceptance bullet has a direct test (unit for the pure pieces,
  command-level for the state-machine pieces per REQ-074's own precedent of GUI-level verification
  where a unit test cannot reach a Win32 dialog or a background thread's real timing); failure mode =
  crossing-elevation conflict, duplicate-elevation conflict, <3-point collapse, unresolved constraint,
  undo-during-rebuild discarding a stale result.
- Steps:
  - [x] Phase B — constrained edge insertion in `BuildTin` (flip-based, Anglada/Sloan), plus
        `TinFindCrossingConflicts` and `conflictingDuplicates`/`constraintsUnresolved` diagnostics.
  - [x] Phase C — `TinCullByBoundaries` (outer/hide/show, in definition order, centroid-based).
  - [ ] Phase A — `CadSurface` definition fields; `BuildSurfaceFromSources` resolves breaklines/
        boundaries by id, prunes dangling ones, feeds constraints + culling into the build; <3-point
        and crossing-conflict diagnostics surfaced through the existing log path.
  - [ ] Phase D — dirty-marking at mutation sites; the async rebuild worker (generation counter +
        cooperative cancellation, per architecture §8, rules 4 and 5 — which the existing
        `pdfAttachAsync` precedent does not itself implement); coalescing to one rebuild per
        command/undo boundary; designate-breakline/designate-boundary commands.
  - [ ] Phase E — `.gs` persistence of the new definition fields; legacy-load default (empty).
  - [ ] Phase F — full self-verification pass, completion report.

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1, self-resolved as a Workshop-level implementation choice within
  the already-approved architecture). Tests-first per phase: Phase B/C's tests were written and run
  against the real implementation in the same step, not after.

## 8. Implementation log
- 2026-08-18 — **Phase B**: `TinConstraint`, `TinCrossingIssue`, `TinFindCrossingConflicts` added to
  `tinbuild.hpp`. `BuildTin` gains an optional `constraints` parameter (default `{}` — every existing
  caller unaffected, confirmed by re-running the full pre-existing `[tin]` suite unchanged: 177
  assertions / 24 cases, still green). Constraint vertices are folded into the point set ahead of
  dedup (points win ties over constraint-derived vertices — "first occurrence" extended to a second
  point source); `conflictingDuplicates` counts a dedup collision whose Z disagrees by more than
  `kTinPlanEpsilon`, separately from an ordinary (agreeing) duplicate. Constraint edges are exposed by
  flip-based insertion after the unconstrained Bowyer-Watson build: for each constraint not already an
  edge, repeatedly find a crossing triangle-edge and flip it if the local quad is convex (derived and
  verified by hand — see the flip-derivation comment in `tinbuild.cpp` — both new triangles' CCW-ness
  is exactly the convexity test), until the constraint edge itself appears or a bounded scan gives up
  and reports `constraintsUnresolved`.
- 2026-08-18 — **Phase C**: `TinBoundaryKind`, `TinBoundaryLoop`, `TinCullByBoundaries` added.
  Boundary rings are meant to also be passed to `BuildTin` as constraints (Phase A wires this), so the
  mesh conforms exactly to the ring and centroid-based culling is exact, not approximate. Applied
  strictly in `loops` order, mutating current inclusion state rather than recomputing from scratch —
  what makes "show restores inside a hide" literally true.
- 2026-08-18 — Tests: `tests/TinConstraintTests.cpp`, 7 cases. Two test-fixture bugs found and fixed
  during the first run (both in the test, not the implementation): `BuildTin` sorts its de-duplicated
  points by (x,y) before assigning output indices, so a test cannot assume output index i == input
  point i (fixed with a coordinate-based `FindVertexIndex` lookup); the first hide/show boundary test
  used ring sizes far smaller than the 25-unit grid spacing, so no triangle centroid could ever land
  inside them (fixed by sizing rings to the fixture's actual grid spacing). After both fixes: 7/7
  green, 68 assertions. Full suite re-run: **405/405 test cases, 203,846 assertions, all green.**

- 2026-08-18 — **Phase A**: `CadSurface` gains `breaklineIds` (vector<uint64_t>) and `boundaries`
  (vector<CadSurfaceBoundary{entityId, kind}>), plus `builtAtRevision`. `CadBoundaryKind` is a local
  mirror enum in `CadEntities.hpp` (not an include of `tinbuild.hpp`) — the same reason `CadTin`
  mirrors `TinBuildResult`'s layout instead of including it: keeps the header dependency-free
  (§11.4). `BuildSurfaceFromSources` extended: resolves `breaklineIds`/`boundaries` against
  `FindEntityById` (Line or Polyline; boundary requires closed), actively prunes ids that no longer
  resolve — writing back the pruned list to `surface.breaklineIds`/`boundaries` — rather than merely
  skipping them at build time (closes ASSUMPTION-1's stricter reading). Each ring's edges are also
  fed in as constraints (Q1), so culling is exact.
- 2026-08-18 — **Phase D**: realized the dirty-tracking problem doesn't need new call sites at all.
  `cadGpuRevision` already increments on every one of the ~56 drawing-mutating call sites (it is what
  drives the unsaved-changes indicator), so it already IS the "something changed" signal REQ-069
  needs. Added `CadSurface::builtAtRevision`; dirty means `builtAtRevision != cadGpuRevision`.
  `AppCommandState::SurfaceRebuildAsync` is the **first complete implementation** of architecture
  §8's full one-shot-worker contract — the existing `pdfAttachAsync` precedent it's modelled on
  implements rules 1–3 but not rules 4 (generation staleness) or 5 (cooperative cancellation).
  Staleness and coalescing both fall out of comparing `cadGpuRevision` at three points (dispatch,
  reap) with no separate generation-tracking machinery needed beyond the one field already used for
  dirty-detection. `BuildSurfaceFromSources` split into `ResolveSurfaceInputs` (UI-thread only, the
  only part touching `AppCommandState`) + `RunSurfaceBuild` (pure, safe on a worker thread) +
  `ToLocalTin`; `TickSurfaceRebuilds` reaps completed workers (discarding on a generation mismatch —
  REQ-069's undo-in-flight condition) then dispatches one per dirty surface with none already
  running, called once per frame in `main.cpp` beside `EnsureEntityIds`. `CreateSurfaceFromPointGroups`
  bumps `cadGpuRevision` *before* its build call (a 1-line reorder) so a freshly created surface
  isn't immediately one revision stale and redispatched pointlessly next tick.
- 2026-08-18 — **Phase D commands**: `DESIGNATEBREAKLINE`/`DBL` and `DESIGNATEBOUNDARY`/`DBD`
  (`OUTER`/`HIDE`/`SHOW`), modelled on OFFSET's single-phase `WaitSelectEntity` rather than
  SurveyInverse's two-pick loop — simpler, and REQ-069's acceptance never specifies a shape here
  ("Commands (designate/edit)" only). Surface name (and boundary kind) are inline command-line
  arguments (`DESIGNATEBREAKLINE Existing Ground`), read via the rest-of-line pattern already used by
  IMPORTMODEL's path argument, because a surface name can contain spaces and `>>` tokenizing would
  break it. One viewport pick commits: validates Line/Polyline (boundary requires closed), reads the
  entity's stable id directly off its `EntityAttributes`, appends to the target surface, and a plain
  `BumpCadGpuCache` is the entire "trigger a rebuild" call — `TickSurfaceRebuilds` picks it up next
  frame with no further wiring. Full build clean, 405/405 tests still green throughout every step of
  Phase A/D (no test touches the command layer directly — see §7 note below on why).
- 2026-08-18 — **Verification-coverage note, recorded rather than glossed over**: `CadCommands.cpp`
  (and therefore `BuildSurfaceFromSources`, `ResolveSurfaceInputs`, `TickSurfaceRebuilds`, both
  DESIGNATE commands) is excluded from `GoSurveyTests` by long-standing design (`CMakeLists.txt`'s
  own comment: "GsIo.cpp itself is not linkable here (it pulls the whole command layer)" — ADR-002).
  This is the same boundary TASK-055 hit for REQ-074's `SURFELEV` command, resolved there by GUI-level
  verification in the running app; Phase A/D's command-layer wiring is verified the same way, next.
  The parts that ARE unit-testable (the pure `tinbuild.cpp` algorithms) already are, exhaustively
  (Phase B/C, 7 cases / 68 assertions).

- 2026-08-18 — **Phase E**: `.gs` persistence. `breaklineIds` (raw uint64 array) and `boundaries`
  (`{entityId, kind}` objects, `kind` written as `"outer"/"hide"/"show"` for readability in the file
  rather than a bare integer) added to the existing surface-write block in `GsIo.cpp`, beside
  `sourcePointGroups`. Read side mirrors it, guarded exactly like every other REQ-069-adjacent field
  here: a legacy `.gs` predating this task simply has neither array, no migration needed. Ids are
  **not** validated at load time — the existing lazy-resolution rule (ADR-027) applies unchanged: an
  id that no longer resolves is caught and pruned the next time the surface rebuilds
  (`ResolveSurfaceInputs`), not treated as a load-time error.
- 2026-08-18 — **Bug found and fixed during Phase D's first GUI attempt**: `DESIGNATEBREAKLINE`/
  `DESIGNATEBOUNDARY` picks silently did nothing in the running app — no log line, command stayed
  active forever. Root cause: `CadUi.cpp` has its own explicit `if (cmd.active == K::…)` chain
  deciding which active `Kind`s receive `SubmitViewportPick` at all on a viewport click, separate
  from and upstream of `SubmitViewportPickImpl`'s own per-`Kind` dispatch in `CadCommands.cpp`. The
  chain's own comment states the exact failure mode word for word: *"A command missing from this
  list silently ignores every viewport click and appears to hang on its first prompt — which is
  exactly what RECT did before it was added here."* My two new `Kind`s were never added to it. Fixed
  by adding them alongside `K::Offset` (same entity-pick shape, raw unsnapped pick coordinates, not
  `commitX`/`commitY`). **Notable side finding, not fixed (out of scope, pre-existing since TASK-055,
  2026-08-15)**: `K::SurfaceElevGrade` is *also* absent from this same chain — `SURFELEV`'s viewport
  clicks likely never worked either, and TASK-055's "verified in the running application" claim must
  have exercised the typed-coordinate path (`ProcessCommandLineSubmit`), which is wired correctly and
  is a separate code path from this click chain. Recorded here rather than silently fixed, per
  workflow.md — it is REQ-074's bug to own, not this task's.
- 2026-08-18 — **Full GUI verification**, built from source (MSVC 19.44/VS2022 BuildTools 17.14 +
  Windows SDK 22621) and driven directly against `samples/surface-demo.gs` (500 real survey points,
  "Existing Ground" point group already defined). Every step screenshotted; log lines quoted are
  verbatim from the app's own command log:
  1. Built a surface from "Existing Ground" → **500 points, 982 triangles** (REQ-068's synchronous
     path, confirming the `BuildSurfaceFromSources` refactor changed no observable behaviour).
  2. Drew a diagonal `LINE` crossing the mesh; `DESIGNATEBREAKLINE Surface` → picked it → **"added a
     breakline"** → **"Surface \"Surface\": 501 points, 984 triangles"** — the dynamic rebuild fired
     with *zero* manual Rebuild click, confirming REQ-069's "no user action" condition end to end
     (`TickSurfaceRebuilds`'s revision-driven dispatch, all the way through the async worker, to the
     applied result) — not just unit-tested in isolation.
  3. Drew a `RECT` (closed 4-vertex polyline) over part of the mesh; `DESIGNATEBOUNDARY Surface HIDE`
     → picked it → **"Surface \"Surface\": 503 points, 932 triangles"**, and the void is directly
     *visible* in the viewport screenshot — a rectangular hole in the mesh exactly matching the
     rectangle, confirming boundary culling (Phase C) end to end. The crossing-conflict diagnostic
     (Phase B) also fired correctly and unprompted — **"breaklines cross at (…)"** twice — because
     the hastily-drawn test rectangle and breakline picked up genuinely different elevations via
     OSNAP where they cross; a real conflict on real data, not a contrived test.
  4. Selected and deleted the rectangle → **"Deleted 1 object(s)."** → **"Surface \"Surface\": 1
     boundary(ies) no longer exist[s] and w[as] removed from the definition."** → **"Surface
     \"Surface\": 501 points, 984 triangles"** — exactly the pre-boundary count, confirming dangling-
     id pruning and rebuild-without-it, both automatic, matching REQ-069's acceptance condition
     verbatim.
  5. `.gs` round-trip (Phase E): saved the breakline-bearing surface to a fresh file; inspected the
     raw JSON directly — `"breaklineIds": [7]` present exactly as expected. **Closed the application
     entirely and relaunched a fresh process** (not just re-reading in the same session) and reopened
     the file → **"Surface \"Surface\": 501 points, 984 triangles"** loaded correctly with the
     breakline-forced edge visibly intact. Then, in that fresh session, selected and deleted the same
     line → **"1 breakline(s) no longer exist[s]"** → **"500 points, 982 triangles"** — proving the
     persisted id correctly resolved against entity ids that were *freshly reassigned on this load*
     (REQ-076's deterministic-on-load guarantee, exercised in combination with REQ-069's new field for
     the first time) and that pruning/rebuild both still work identically after a save/load cycle.
  - Not directly observed (reasoned from the design instead, same honesty standard TASK-055's
    ASSUMPTION-1 set): "a single MOVE of N points triggers one rebuild, not N" and "undo during an
    in-flight rebuild discards the stale result" — both follow directly from the
    `cadGpuRevision`-as-generation design (§8 Phase D log) rather than from any per-point or per-undo
    special-casing, but neither is easily forced into a deterministic screenshot-timed repro. The
    mechanism itself (one dispatch per surface per revision; apply only if the revision at completion
    still matches dispatch time) is the same mechanism proven correct by every rebuild observed above.
  - Full test suite re-confirmed green after every phase: **405/405 test cases, 203,846 assertions**.

## 9. Self-verification
- [x] build-project        — **PASS.** Full clean MSVC build (all targets: GoSurvey, gosurvey_headless,
                              GoSurveyTests), no new warnings at any phase.
- [x] architecture-review  — **PASS.** No new abstraction, layer, dependency, or global. The async
                              worker is the first *complete* implementation of architecture §8's
                              already-documented pattern (generation staleness + cancellation), not a
                              new one. `CadBoundaryKind` mirrors an existing pattern (`CadTin`
                              mirroring `TinBuildResult`) rather than adding a cross-layer include.
                              Dirty-tracking reuses the existing `cadGpuRevision` signal rather than
                              adding a parallel one. One real bug found and fixed within boundary
                              (CadUi.cpp's click-dispatch chain missing the two new `Kind`s — see §8);
                              one pre-existing gap in a *different* requirement (REQ-074) found and
                              recorded, not fixed, per workflow.md.
- [x] code-review          — **PASS.** Every non-obvious decision carries its reasoning inline (the
                              flip-derivation comment in `tinbuild.cpp`, the `builtAtRevision`/
                              generation reasoning in `CadCommands.hpp`, the lazy-resolution note in
                              `GsIo.cpp`). No suppressed symptom, no disabled test, no softened
                              message.
- [x] dependency-audit     — n/a. Nothing added.
- [x] performance-review   — **PASS with a recorded limit.** `edgeExists`/crossing scans inside
                              constraint exposure are O(triangle count) per operation — acceptable at
                              breakline/boundary vertex counts (tens to low hundreds in the GUI runs
                              exercised here), not measured against REQ-100's 100k-point/200k-triangle
                              surface budget. Written down as a limit, not silently assumed fine, per
                              CLAUDE.md rule 1 (measure before optimizing; no measured problem exists
                              yet to optimize against). The rebuild itself is off the UI thread
                              regardless, so a slow constraint pass costs latency-to-result, not frame
                              time.
- [x] testing              — **PASS.** Unit: `tests/TinConstraintTests.cpp`, 7 cases / 68 assertions
                              (Phase B/C, the pure algorithmic core). Full suite: **405/405 test
                              cases, 203,846 assertions**, green after every phase, no regression.
                              Command-layer (Phase A/D/E) cannot be unit-tested — `CadCommands.cpp`
                              and `GsIo.cpp` are excluded from `GoSurveyTests` by long-standing design
                              (ADR-002) — so per TASK-055's own established precedent for this exact
                              boundary, it is verified in the running application instead (§8's GUI
                              verification log): every acceptance condition reachable this way was
                              directly observed, screenshotted, and quoted from the app's own log.

## 10. Verification result
- Submitted:  2026-08-18
- Verdict:    **PASS.**
- Findings:   none blocking. One bug found and fixed within this task's own boundary (CadUi.cpp's
              click-dispatch chain — §8). One pre-existing bug in a different requirement (REQ-074's
              `SURFELEV` viewport-click wiring) found and recorded, not fixed — out of this task's
              scope, belongs to a future REQ-074 task.

## 11. Outcome
- Requirements satisfied: REQ-069 (Acceptance met: yes for every condition reachable through the
  application — breakline forcing, outer/hide/show boundaries in order, dynamic rebuild with no
  manual action, dangling-id removal on delete, crossing-elevation diagnostic, <3-point failure,
  `.gs` round-trip with ids intact, all directly observed in §8. Two conditions — single-rebuild-
  per-MOVE and undo-during-in-flight-rebuild — are satisfied by design/mechanism rather than by a
  timed repro; see §8's closing note for why and what mechanism actually guarantees them)
- Tests added:            `tests/TinConstraintTests.cpp` (7 cases, 68 assertions)
- Refactors:              `BuildSurfaceFromSources` split into `ResolveSurfaceInputs` (UI-thread) +
                          `RunSurfaceBuild` (pure) + `ToLocalTin`, to make the async worker possible
                          without touching `AppCommandState` off the UI thread
- Docs updated:           none beyond this task log — REQ-069's spec text already covered the shipped
                          behaviour exactly; `spec/roadmap.md`'s M-Surfaces status is the next thing
                          to sync (step 5 done, step 6 — styles/contours, REQ-070/071 — is next),
                          left for the user to request per this session's earlier roadmap-sync pattern
- Done:                   2026-08-18
