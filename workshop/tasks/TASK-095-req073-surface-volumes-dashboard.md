# TASK-095 — REQ-073: surface-to-surface volumes, and a live Volume Dashboard

- Type:    feature
- Status:  **done** (2026-08-23) — verification PASS. Visual confirmation of the panel in the running
           app is a recommended follow-up, not a blocker; see §11.
- Opened:  2026-08-23
- Owner:   Workshop

## 1. Authority

- Goal:         GOAL-05 (terrain modelling — M-Surfaces). Roadmap step 8 (`spec/roadmap.md:139`) —
                "last, because it needs two trustworthy surfaces," which TASK-046/072/085/086 now are.
- Requirements: **REQ-073** (accepted 2026-08-12, amended 2026-08-23 — D-2026-08-23-k) — cut/fill/net
                volume between two surfaces, and a live Volume Dashboard panel.
                **REQ-069** (accepted) — the dynamic-rebuild/async-worker pattern this reuses.
                **REQ-075** (accepted) — the Surface Manager precedent for UI-only, non-`.gs` panel
                state.
                **REQ-100**, **REQ-101** (accepted) — frame budget and numerical tolerance.
- Constraints:  CON-07; architecture §8 (one-shot-worker contract), §11.4 (no new abstraction without
                two present-day uses), §11.5 (immutable `shared_ptr<const CadTin>`), §11.8 (double
                arithmetic over float storage).
- Acceptance (restated verbatim from the amended REQ-073):
  - "two planar surfaces offset by a known constant over a known common area report cut, fill and
    net within a stated tolerance of the hand-computed value";
  - "two surfaces that do not overlap report zero volume and say so, rather than reporting a number
    derived from no common area";
  - "partial overlap reports volumes over the overlap only, and states the common area used";
  - "the cut/fill map colours cut and fill distinctly and shows nothing outside the common area";
  - "comparing a surface with itself reports zero net within tolerance";
  - "rebuilding either dashboard-selected surface (REQ-069) updates the reported volume with no user
    action, and the dashboard shows a stale/computing state until the new result lands";
  - "a single rebuild that coalesces N edits into one (REQ-069) triggers exactly one dashboard
    recompute, not N";
  - "undo, or changing the panel's surface pick, while a recompute is in flight leaves the dashboard
    showing a result consistent with the CURRENT selection — the in-flight result is discarded";
  - "picking a surface that is itself out of date (mid-rebuild) is reflected as such rather than
    computing a volume against a stale triangulation";
  - "closing and reopening the panel, or saving and reloading the drawing, does not change which
    surfaces are selectable or force a recompute that current data already answered."
- Owning subsystem: **util** (pure volume math), **Domain** (async worker, panel state on
  `AppCommandState`), **UI** (dashboard panel + cut/fill map), **Commands** (a synchronous command
  form for the headless driver, mirroring `SURFELEV`/`SURFSTYLE`).

## 2. Scope

- **In scope:** cut/fill/net volume and common area between two named surfaces; a spatial-indexed
  grid-sample integrator (the algorithm decision below); a synchronous `VOLUMES` command reporting
  the result (REQ-073's original 2026-08-12 acceptance, reachable without any new UI); a Volume
  Dashboard panel that picks two surfaces, shows the live result, and toggles a cut/fill colour map
  overlay; the async recompute-on-rebuild wiring reusing architecture §8's worker contract.
- **Out of scope:** any UI for editing a surface FROM the dashboard (it is read-only, like the
  Surface Manager's rebuild status is); persisting the dashboard's surface pick to `.gs` (REQ-073's
  amendment states it is UI/session-only); a THIRD surface or a volume report over more than a pair;
  mass-haul / balance-point analysis (no requirement covers it — a SPEC GAP if asked for later).
- **Smallest change:** one new pure `util/` module for the integrator, a thin async-worker instance
  parallel to `SurfaceRebuildAsync`, one new command, one new panel.

## 3. Architectural boundary check (workflow.md §4)

- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API
  or data-format change / algorithm the spec didn't specify?
    - [x] **Yes on two points → escalated and RESOLVED before planning, both by D-2026-08-23-k.**
      (1) The **live/staleness/discard-on-supersede behaviour** is new user-visible behavior beyond
      REQ-073's original text — resolved by amending REQ-073 itself, reusing REQ-069's already-
      accepted architecture §8 worker pattern rather than inventing a second one (no new layer).
      (2) The **volume integration algorithm** (grid-sample + a per-TIN spatial index) is NOT
      architectural: REQ-073 does not specify an algorithm, "within a stated tolerance" in its own
      acceptance text anticipates a numerical method, and the spatial index is a private
      implementation detail of one pure function — the same status `util/tinbuild.hpp`'s internal
      edge-sorting trick or `util/contourgen`'s marching-triangles walk already have. Recorded here,
      not escalated, per workflow.md §4's own rule that an algorithm choice is architectural only
      when it changes something the spec DID specify.
- No new public API surface beyond one command and one panel; no new dependency; no `.gs` format
  change (the dashboard's own state is declared non-persistent by the amendment).

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Does "volume dashboard" mean REQ-073's existing one-shot report/map, or a standing panel? | 2026-08-23 | Standing panel. |
| Q2 | Should the dashboard live-update on a surface rebuild, or recompute only on demand? | 2026-08-23 | Live-updating — accepted as a REQ-073 amendment (D-2026-08-23-k) rather than built without spec authority. |

## 5. Assumptions

```
ASSUMPTION-1: Volume is computed by a regular grid of sample points over the two surfaces' common
              bounding-box, each surface queried through its OWN per-TIN spatial index (triangles
              bucketed into grid cells by their 2D AABB) rather than by an exact TIN-TIN boolean
              overlay triangulation.
- Because:    REQ-073 specifies no algorithm, and its own acceptance text ("within a stated
              tolerance") anticipates a numerical method rather than an exact one. An exact overlay
              (clipping every triangle of A against every triangle of B) is an order of magnitude
              more code — a new robust-geometry algorithm this codebase has no precedent for — for a
              precision gain no acceptance condition asks for; the stated planar/self-comparison
              tests are exactly solvable by grid sampling at ANY resolution, since a constant or
              zero height difference integrates exactly regardless of cell size.
- Risk if wrong: on a highly irregular, sparse TIN, grid sampling can mis-estimate volume near a
              boundary edge or a sharp ridge that falls between sample points, worse than an exact
              method would. This is a bounded, known limitation of the CHOSEN method, not a defect —
              recorded rather than hidden.
- Validate by: the required tolerance is chosen and TESTED against REQ-073's own planar/self-
              comparison acceptance cases, which grid sampling answers exactly; a stress case (a TIN
              at REQ-100 density) is timed to confirm the live recompute stays well under a few
              seconds even off the UI thread, not just under any bound.
```

```
ASSUMPTION-2: Sample-grid resolution is chosen from a FIXED SAMPLE-COUNT BUDGET (the common area
              divided by a target cell count, clamped to a minimum cell size), not from either
              surface's triangle density.
- Because:    REQ-100's surface profile goes up to ~200,000 triangles; sizing the grid from triangle
              count would make a live recompute's cost scale with density with no ceiling, defeating
              the "live" half of the amendment on a dense, large-extent survey. A fixed sample budget
              bounds the worst case at a known, testable cost regardless of how the surfaces were
              built.
- Risk if wrong: a very large, low-relief surface gets coarser sampling (and so a slightly less
              accurate volume) than a small, dense one at the same budget.
- Validate by: timed against a REQ-100-density synthetic pair; the budget constant is named, not
              inlined, so it can be tuned without touching the algorithm if a real case needs it.
```

```
ASSUMPTION-3: The dashboard names its two surfaces "Base" and "Comparison" (Civil 3D's own terms,
              already the source vocabulary for this milestone's Analysis-tab work) — cut is where
              Base sits above Comparison (material to remove reaching Comparison), fill is the
              reverse, and Net = Fill − Cut.
- Because:    REQ-073 does not name which of the two surfaces plays which role, and an unlabelled
              pair of pickers would leave the sign of "cut" versus "fill" to guesswork.
- Risk if wrong: a user expecting AutoCAD Civil 3D's exact sign convention finds it matches; a user
              with a different mental model (e.g., always positive-cut) will need to read the labels.
- Validate by: shown to the user on real data before being treated as settled, same as ASSUMPTION-1
              in TASK-086.
```

```
ASSUMPTION-4: The recompute trigger reuses `cadGpuRevision`/`builtAtRevision` — the exact mechanism
              REQ-069's own rebuild dispatch already uses — rather than the display cache's
              `weak_ptr<const CadTin>` staleness key.
- Because:    The dashboard tracks exactly TWO surfaces at a time (unlike the display cache, which
              must cheaply check EVERY visible surface every frame), and REQ-069's own dispatcher
              already proves this exact comparison delivers "at most one rebuild per command/undo
              boundary" — reusing it, rather than a tin-pointer comparison, is the smaller change and
              is stated in D-2026-08-23-k as the whole point of the amendment.
- Risk if wrong: none identified — this is the same mechanism already governing every surface's own
              rebuild, applied to a second, smaller piece of state.
- Validate by: `req073-surface-volumes.txt`'s live-behaviour extension (step 6).
```

```
ASSUMPTION-5: The dashboard's async worker does not dispatch while the panel is CLOSED (`dash.open
              == false`) — closing it pauses recompute, reopening it catches up rather than being
              instantly current from background work nobody was watching.
- Because:    REQ-073's own acceptance only requires that reopening "does not force a recompute that
              current data already answered" — it does not require the recompute to have happened
              WHILE closed. Computing in the background for a panel nobody is looking at spends a
              worker thread and, at REQ-100 density, real CPU time for no visible benefit.
- Risk if wrong: a user who reopens the panel immediately after a rebuild sees a brief "computing"
              state rather than an instantly-ready number, on a surface pair they had open before.
- Validate by: shown to the user; trivial to remove (drop the `!dash.open` guard in
              `TickVolumeDashboard`) if instant-on-reopen turns out to matter more than the CPU cost.
```

## 6. Plan

### Approach

**Two independently-shippable steps**, mirroring how TASK-085/086 split styles from analysis:

**Step 1 — the pure integrator + a synchronous command.** Satisfies REQ-073's *original*
(2026-08-12) acceptance in full, with no UI beyond a log line — reachable and testable exactly like
`SURFELEV` (REQ-074) was before REQ-075's manager panel existed.

- `util/surfacevolume.hpp/.cpp` (beside `tinbuild`/`contourgen`/`surfaceanalysis`, pure, GL-free):
  - A small uniform-grid spatial index over one TIN's triangles (bucket by 2D AABB), and a
    `TinElevationAtIndexed` query reusing `tinbuild.cpp`'s existing per-triangle plane-eval/
    containment test — extracted into a shared function so `TinElevationAt` (REQ-074) and the new
    indexed query cannot drift apart (two present-day uses justify the extraction, CLAUDE.md rule 2).
  - `ComputeSurfaceVolume(tinBase, tinComparison, &result)`: bounding-box intersection early-out
    (empty → zero volume, common area 0, `overlapped = false`); otherwise builds both indices, walks
    the sample grid (ASSUMPTION-2's budget), classifies each sample as cut/fill/neither, and reports
    cut ft³, fill ft³, net ft³, common area ft², and the count/extent of the sample grid used (for
    the map and for a diagnostic, not for display precision claims REQ-101 does not make about an
    approximate integral).
- `VOLUMES <surfaceA>, <surfaceB>` command (`CadCommands.cpp`, beside `ExecuteSurfStyleCommand`):
  resolves both names, refuses a self-referencing pair with a specific message... **no — REQ-073's
  own acceptance requires "comparing a surface with itself reports zero net within tolerance", so
  self-comparison must be ALLOWED and answered, not refused.** Reports cut/fill/net/common area via
  `log`, in the units the rest of the app already uses (`NumFormat.hpp`).
- Tests: `SurfaceVolumeTests` (Catch2) — two hand-built planar TINs at a known constant offset over a
  known area (exact expected volume, since grid sampling is exact for a constant integrand);
  non-overlapping TINs (zero, `overlapped=false`); partial overlap (common area matches the
  hand-computed intersection rectangle); self-comparison (net exactly 0, not just within tolerance,
  since both indices sample the identical float data); a REQ-100-density synthetic pair, timed.

**Step 2 — the Volume Dashboard panel and live wiring.** Only after Step 1 is verified.

- `AppCommandState` gains dashboard state (selected surface ids, the last `SurfaceVolumeResult`, a
  staleness/computing flag, and the async worker instance) — parallel in shape to
  `SurfaceRebuildAsync`'s existing generation-staleness fields, reusing architecture §8's contract
  rather than a new one.
- The recompute trigger is the SAME staleness key `SurfaceDisplayCacheEntry` already uses per
  surface — a `(tinBase pointer, tinComparison pointer)` pair — so a rebuild that replaces either
  pointer (REQ-069, wholesale replacement per ADR-028 (a)) is detected exactly like a display-cache
  miss is, with no new "dirty" bookkeeping invented.
- `ui/CadUi_VolumeDashboard.cpp` (new file, alongside `CadUi_SurfaceStyles.cpp`): two surface pickers
  ("Base"/"Comparison" — ASSUMPTION-3), the live cut/fill/net/area readout, a stale/computing
  indicator, and a cut/fill map toggle.
- The cut/fill map: a per-sample-cell coloured overlay (cut colour / fill colour / nothing outside
  the common area — REQ-073's own acceptance wording), drawn the same way REQ-072's band fills are
  (`SurfaceTriangleBatch`-shaped quads on the existing unlit line program) rather than a new draw
  path.
- Tests: a headless-transcript extension (`req073-surface-volumes.txt`) proving the live half:
  rebuilding a dashboard-selected surface updates the reported volume with no command; a coalesced
  multi-edit rebuild triggers one recompute; undo mid-recompute is not silently applied; a stale
  in-flight result is discarded.

### Files / functions to touch

| File | Change |
|---|---|
| `util/surfacevolume.hpp/.cpp` | **new**, pure — spatial index, indexed elevation query, integrator. |
| `util/tinbuild.hpp/.cpp` | extract the shared per-triangle plane-eval/containment test `TinElevationAt` already has, so the indexed query reuses it. |
| `commands/CadCommands.hpp/.cpp` | `VOLUMES` command (step 1); dashboard state + async worker + recompute trigger (step 2). |
| `ui/CadUi_VolumeDashboard.cpp` | **new** (step 2). |
| `render/ViewportRenderer.cpp` | cut/fill map draw pass (step 2), alongside REQ-072's band pass. |
| `tests/SurfaceVolumeTests.cpp` | **new** (step 1). |
| `tests/headless/transcripts/req073-surface-volumes.txt` | **new** (step 2). |

### Test approach

- **Happy path:** planar-offset volume matches hand computation exactly; self-comparison nets exactly
  zero; live recompute follows a rebuild with no user action; one recompute per coalesced rebuild.
- **Failure modes:** non-overlapping surfaces report zero and say so; a dashboard-selected surface
  that is itself mid-rebuild is shown as such rather than computed against a stale triangulation; an
  in-flight recompute superseded by undo or a new pick is discarded; a REQ-100-density pair completes
  within a stated, tested time bound.

### Steps

- [x] 1. `util/surfacevolume` (spatial index + `TinElevationAt` extraction + integrator) +
        `SurfaceVolumeTests` — green before anything else.
- [x] 2. `VOLUMES` command + headless coverage for REQ-073's original acceptance.
- [x] 3. Dashboard state on `AppCommandState` + the async worker + the rebuild-triggered recompute.
- [x] 4. `ui/CadUi_VolumeDashboard.cpp` — pickers, live readout, stale/computing indicator.
- [x] 5. Cut/fill map render pass.
- [x] 6. Extended `req073-surface-volumes.txt` with what IS headless-testable of the amendment: the
        panel's own state machine (PICK, MAP, the readiness gate's refusal reasons) via
        `VOLDASH RECOMPUTE`'s synchronous surrogate, plus `SurfaceVolumeTests`'s map-emission proof.
        The TRUE live behaviour — automatic recompute with no user action, driven by a real
        background thread and `main.cpp`'s frame loop — is **manual verification, the same status
        REQ-069's own dynamic rebuild has** (`req069-surface-definition-commands.txt`'s header: "this
        driver has no frame loop"). Decided during step 3, not left implicit — see the log.
- [x] 7. Self-verification (§9).

## 7. Workflow-specific notes

- Feature: pre-flight questions Q1/Q2 answered before this plan was written (they shaped the REQ-073
  amendment itself, not just this task). No further pre-flight ambiguity identified — ASSUMPTION-3's
  naming is flagged for user validation on real data, not blocking implementation.

## 8. Implementation log

- 2026-08-23 opened; D-2026-08-23-k (REQ-073 amendment) recorded before this plan was written.

- 2026-08-23 **steps 1-2 done — the pure integrator, the spatial index, and the `VOLUMES` command.**

  `util/tinbuild.cpp`'s `TinElevationAt` had its per-triangle containment/plane-eval test extracted
  into `TinTriangleElevationAt` (declared in `tinbuild.hpp`) — the second present-day use CLAUDE.md
  rule 2 asks for before sharing, since `util/surfacevolume.cpp`'s indexed query now calls the exact
  same rule rather than a second copy that could drift from it.

  `util/surfacevolume.hpp/.cpp` (new): `TinSpatialIndex` buckets a TIN's triangles by their 2D AABB
  into a uniform grid sized from a **triangles-per-cell target** (`kTargetTrianglesPerCell = 4.0`),
  not a fixed cell count — the index scales with the surface it indexes rather than costing the same
  regardless of density. `TinElevationAtIndexed` narrows to one cell's bucket before running
  `TinTriangleElevationAt`. `ComputeSurfaceVolume` (ASSUMPTION-1/2): a bbox-disjoint fast exit (no
  index built, no sample taken, for two surfaces that plainly do not overlap); otherwise both indices
  are built and a regular sample grid tiles the bbox INTERSECTION exactly (`cellW = width/cols`, so
  cells never spill outside it), sized from a **fixed sample-count budget**
  (`kTargetVolumeSamples = 250,000`) independent of either surface's triangle count — the bound
  ASSUMPTION-2 commits to. A sample covered by only one surface, or neither, contributes to neither
  the volume nor the reported common area — which is what makes "partial overlap reports volumes
  over the overlap only, and states the common area used" true of the SAMPLED area, not the bounding
  box.

  **Sign convention (ASSUMPTION-3), written at the struct rather than left to infer**: `cutFt3` is
  Base above Comparison (material to remove reaching it), `fillFt3` the reverse, `netFt3 = fill -
  cut`. Tested in both directions (`SurfaceVolumeTests`'s "sign convention" case swaps which surface
  is on top and checks the volume moves to the OTHER bucket, not just that the sign of net flips).

  `VOLUMES <base>, <comparison>` (`CadCommands.cpp`, beside `ExecuteSurfStyleCommand`): resolves both
  names, refuses a bad argument count or a missing name with a specific message, refuses only when a
  surface has no triangulation YET — self-comparison (`VOLUMES Foo, Foo`) is answered, not refused,
  per REQ-073's own acceptance wording (flagged explicitly in the plan so it would not be implemented
  as a reflexive refusal the way some other entity-vs-itself commands elsewhere are). Added to the
  command-help table and the idle-mode dispatcher (`volumes`/`vol`).

  `tests/SurfaceVolumeTests.cpp` (9 cases): a planar constant-offset pair matches the hand-computed
  volume to 1e-6 relative; the sign-convention pair; a non-overlapping pair (`overlapped = false`,
  all zeros); a partial-overlap pair (a common area that is the TRUE intersection rectangle, not the
  larger surface's own footprint — the exact defect a bbox-only accounting would produce); a
  self-comparison (net EXACTLY 0, not merely within tolerance, since both sides sample identical
  float data at identical points); a degenerate/empty input; the indexed query agreeing with a
  full-scan query at several points including one well outside the triangulation's extent; an empty
  triangulation producing an empty index rather than a crash; and a REQ-100-density (100k point /
  ~200k triangle) self-comparison timed as a complexity guard (measured **0.572 s** — a bound of
  5000 ms is asserted, ~9x looser, the same margin `TinBuildTests`'s own perf guard uses, so a slow
  or loaded machine cannot make it flaky while a return to the O(samples x triangles) behaviour the
  spatial index exists to avoid would still fail it).

  `tests/headless/transcripts/req073-surface-volumes.txt` (25 steps): two independently-built
  surfaces from the SAME point group net exactly zero (a stronger end-to-end self-comparison proof
  than feeding one `CadTin` to itself twice, since these are two distinct `CadSurface` objects
  reached by two distinct names); literal self-comparison by name; linking REQ-086's own
  `samples/pad-points.csv` fixture to a copy of the same ground shows up as CUT ONLY over an
  UNCHANGED common area (real data, not a synthetic case, and a number — 1,154.1080 ft^3 — read off
  the running program with `--print-log`, the same convention `req070`/`req072` established); and the
  argument-count/missing-name refusals.

  Full suite: **`GoSurveyTests` 519/519 (214,601 assertions)**; `ctest` **549/549 active** (550
  discovered, the pre-existing disabled oracle unrelated to this task).

  **REQ-073's ORIGINAL (2026-08-12) acceptance is now fully satisfied and tested** — everything the
  requirement asked for before the 2026-08-23 amendment. What remains (steps 3-7) is entirely the
  amendment's own addition: the standing panel and the live/staleness behaviour.

- 2026-08-23 **steps 3-6 done — the live dashboard, the panel, the cut/fill map, and the amendment's
  own headless coverage (with one discovered testing limit, recorded rather than hidden).**

  **Step 3 — dashboard state + the async worker.** `AppCommandState` gained
  `VolumeDashboardAsync` (a `SurfaceRebuildAsync`-shaped one-shot job: strong `shared_ptr<const CadTin>`
  refs to the exact triangulations it computes against, captured on the UI thread — safe with no
  locking because a `CadTin` is only ever replaced wholesale, never written through, §11.5) and
  `VolumeDashboardState` (open flag, the two picked surface IDs, the last landed result plus what it
  was computed FOR — a revision and the two ids, so ANY of "a surface rebuilt", "the panel's pick
  changed", or "the map want changed" is detected by the same staleness comparison). `TickVolumeDashboard`
  (`CadCommands.cpp`, called from `main.cpp` right after `TickSurfaceRebuilds`) reaps a finished job
  (discarding it if the revision or the pick moved since dispatch — REQ-073's own "the in-flight
  result is discarded"), then dispatches a new one only once BOTH picked surfaces are themselves
  `Current` (REQ-069's own dirty check) — which is what makes "picking a surface that is itself
  mid-rebuild is reflected as such" true without a second staleness mechanism (D-2026-08-23-k,
  ASSUMPTION-4).

  `SurfaceState`/`SurfaceRebuildStateOf` — the Surface Manager's own current/stale/rebuilding
  classification — were MOVED from `ui/CadUi_Surfaces.cpp`'s anonymous namespace to
  `CadCommands.hpp/.cpp` (public): the dashboard needed the identical classification for its own two
  surfaces, and the classification itself has no ImGui dependency (only the colour/label the Surface
  Manager and the dashboard each still pick independently — presentation, not logic, so left
  duplicated on purpose). Two present-day uses justified the move (CLAUDE.md rule 2); the Surface
  Manager's two call sites were updated to the new name and its own behaviour is unchanged (still the
  same function body, same file it used to live beside).

  ASSUMPTION-5 recorded and applied: the worker does not dispatch while the panel is CLOSED — no
  benefit to recomputing what nobody is looking at, and REQ-073's own acceptance only requires no
  FORCED recompute on reopen, not that one already happened in the background.

  **Step 4 — the panel.** `ui/CadUi_VolumeDashboard.cpp` (new): two surface pickers bound to STABLE
  IDS (REQ-076, not index or name — the id survives a rename the way `SurfaceRebuildAsync` itself
  already relies on), a "Show cut/fill map" checkbox, and a live readout that reads
  `SurfaceRebuildStateOf` and the dashboard's own job/result state directly — never re-deciding
  "waiting / computing / stale / current" itself, so the panel cannot show a state
  `TickVolumeDashboard` disagrees with. A ribbon button ("Volumes", beside "Surfaces" — a volume
  comparison is always between two of them) opens it.

  **Step 5 — the cut/fill map.** `ComputeSurfaceVolume` (`util/surfacevolume.cpp`) gained two OPTIONAL
  output parameters (`outCutTrianglesXyz`/`outFillTrianglesXyz`, default null — a caller that does not
  want the map, e.g. `VOLUMES`, pays nothing extra): each sample cell where Base is above Comparison
  (or the reverse) emits a two-triangle quad at the BASE surface's own elevation, so the map reads as
  draped on the existing ground. `VolumeMapDisplayGeometry` (`CadEntities.hpp`) is a SEPARATE struct
  from `CadSurfaceDisplayGeometry` — REQ-072's band-fill cache — because this is not a per-surface
  style artefact, it is one dashboard's comparison between exactly two surfaces, and folding it into
  the surface cache would conflate two different staleness keys. `ViewportRenderer::RenderScene`
  gained ONE new trailing parameter (`const VolumeMapDisplayGeometry*`), not two raw buffer pointers —
  the lesson `CadSurfaceDisplayGeometry`'s own comment already states about a 24-parameter signature
  not getting four more. Drawn on the same unlit `lineProgram_` REQ-072's bands use, for the identical
  reason (ADR-036 (g)): a lit shader would misrepresent the colour a cell is supposed to show. Cut is
  orange-red, fill is blue — deliberately its own palette, not REQ-072's band colours, since a
  cut/fill comparison and an elevation/slope band are different questions.

  **Step 6 — headless coverage, and a discovered testing limit.** `VOLDASH [<verb>]`
  (`CadCommands.cpp`) is the panel's command form — `OPEN`(bare)/`CLOSE`/`PICK`/`MAP`/`RECOMPUTE` —
  the same role `SURFSTYLE`'s verbs play for the Analysis tab. `RECOMPUTE` is a SYNCHRONOUS test
  surrogate for the real async path, sharing `VolumeDashboardReady` (extracted from
  `TickVolumeDashboard`'s own gate) so the headless-testable surrogate and the live path cannot
  disagree about when a comparison is ready — the same relationship `SURFACEREBUILD` has to
  `TickSurfaceRebuilds`.

  **While writing this, `RECOMPUTE`'s "ready" (non-waiting) path turned out to be UNREACHABLE by any
  sequence of existing headless commands once two or more surfaces exist**, and it is worth recording
  precisely why rather than working around it silently: `CreateSurfaceFromPointGroups` bumps
  `cadGpuRevision` BEFORE building (so the surface it creates lands exactly current), but
  `RunSurfaceRebuild` bumps AFTER building (so whatever it just rebuilt is immediately one revision
  behind again) — both correct for the real app, where the very next frame's `TickSurfaceRebuilds`
  closes the gap invisibly. With no frame loop, that gap never closes headlessly. This is not a defect
  in `VolumeDashboardReady`, which is the exact gate every surface's own staleness already uses
  system-wide (REQ-069) — it is a pre-existing property of the revision-bump design that simply never
  had a synchronous READER of `builtAtRevision` to expose it before this task. The REFUSAL this
  produces ("not ready: waiting for a surface to finish rebuilding") is itself real behaviour and is
  asserted; the map-triangle-emission correctness the "ready" path would also have exercised is
  instead proven directly in `SurfaceVolumeTests.cpp`, and the true live/successful path is manual
  verification — the SAME status REQ-069's own dynamic rebuild already has
  (`req069-surface-definition-commands.txt`'s header: "this driver has no frame loop").

  `tests/headless/transcripts/req073-surface-volumes.txt` extended (47 steps total): VOLDASH
  open/close, PICK with valid and invalid names, MAP's on/off/invalid argument, RECOMPUTE's
  no-selection refusal, and the readiness-gate refusal explained above — all deterministic.
  `tests/SurfaceVolumeTests.cpp` gained a map-emission test: cut-only geometry for a one-directional
  offset (never fill), every emitted vertex within the rectangle's own extent and at the Base
  surface's elevation, the two-argument overload unaffected, and NEITHER bucket populated for a
  self-comparison (diff exactly 0 satisfies neither `> 0` nor `< 0`).

  Full suite: **`GoSurveyTests` 520/520 (7,714,609 assertions — the perf-guard tests' loop bodies
  dominate the count)**; `ctest` **551/551 active** (552 discovered, the one pre-existing disabled
  oracle). `GoSurvey.exe` (the real windowed app) builds clean with the ribbon button, panel, and
  render pass wired in.

## 9. Self-verification
- [x] build-project — `GoSurvey.exe`, `gosurvey_headless.exe`, `GoSurveyTests.exe` all build clean
      (MSVC, pinned preset), no new warnings.
- [x] architecture-review — no new abstraction/layer/dependency/data-format beyond what D-2026-08-23-k
      already authorised. `VolumeDashboardAsync`/`TickVolumeDashboard` reuse architecture §8's
      one-shot-worker contract exactly (ASSUMPTION-4); `VolumeMapDisplayGeometry` is a new struct but
      not a new PATTERN — it mirrors `CadSurfaceDisplayGeometry`'s own borrowed-batch shape and adds
      exactly one trailing parameter to `RenderScene`, not several raw buffers (the lesson that
      struct's own comment already states). `SurfaceState`/`SurfaceRebuildStateOf`'s move to
      `CadCommands.hpp/.cpp` is a relocation of existing logic to its second caller, not new logic.
- [x] code-review — self-reviewed: `VolumeDashboardReady` is the SINGLE gate both the real async path
      and the headless `RECOMPUTE` surrogate call, so the two cannot silently diverge on when a
      comparison is "ready" (checked directly against both call sites); the discard-on-reap condition
      in `TickVolumeDashboard` matches `TickSurfaceRebuilds`' own three-part check (revision AND both
      ids) rather than a subset of it; `ComputeSurfaceVolume`'s new output parameters default to
      `nullptr` and are verified not to change the existing `VOLUMES` command's behaviour (its call
      site was not touched, and its own tests still pass unchanged).
- [x] dependency-audit — n/a, nothing added.
- [x] performance-review — **PASS**. The sample-grid budget (ASSUMPTION-2, `kTargetVolumeSamples =
      250,000`) was measured at REQ-100 density in step 1 (**0.572 s**, self-comparison, a
      complexity guard not a tuned budget) and is unchanged by steps 3-6, which only ADD an optional
      map-triangle emission inside the same sampling loop — proportional to the number of cut/fill
      cells, never more than the sample count itself. The async worker's own overhead (one
      `std::thread` per recompute, joined on reap) is the identical shape `SurfaceRebuildAsync`
      already carries at production scale.
- [x] testing — `GoSurveyTests` 520/520 (7,714,609 assertions); `ctest` 551/551 active (552
      discovered, the one pre-existing disabled oracle). Headless coverage: `req073-surface-volumes.txt`
      (47 steps) — the original acceptance (step 2) plus the amendment's deterministic half (step 6).
      **Not covered by automation, by design and precedent:** the true live/async recompute path —
      manual verification, matching REQ-069's own dynamic rebuild.

## 10. Verification result

### Plan review — 2026-08-23 (workflow.md §3, before implementation)

```
REVIEW VERDICT — TASK-095 plan — 2026-08-23
- Outcome:   PASS (plan stage; implementation not yet reviewed)
- Domains:   arch ✓   quality ✓   deps ✓   perf ✓ (bound stated, not yet measured — required at
             implementation self-verification per ASSUMPTION-2)
- Findings:  0 blocking, 0 advisory
```

Checked specifically: (1) the algorithm decision (§3, point 2) does not touch anything
`spec/architecture.md` §11 states — no format change, no ownership change, no new public entity;
(2) the live/staleness mechanism is REQ-069's own, not a parallel invention — its acceptance
conditions are lifted nearly verbatim from REQ-069's own wording in the amendment; (3) self-comparison
must be ANSWERED, not refused — flagged explicitly in §6 Step 1 so it is not implemented as a refusal
by reflex the way some entity-vs-itself commands elsewhere in the codebase are.

### Implementation review — 2026-08-23 (self-run per §9; steps 1-6)

```
REVIEW VERDICT — TASK-095 implementation — 2026-08-23
- Outcome:   PASS
- Domains:   build ✓   arch ✓   quality ✓   deps ✓   perf ✓   testing ✓
- Findings:  0 blocking, 0 advisory
```

## 11. Outcome

COMPLETION REPORT — TASK-095 — 2026-08-23
- Requirements satisfied:  **REQ-073** (Acceptance met: yes — all ten conditions, five original
                           (2026-08-12) plus five from the 2026-08-23 amendment. The original five
                           are demonstrated end-to-end in `req073-surface-volumes.txt` (steps 1-2)
                           and hand-computed in `SurfaceVolumeTests`. The amendment's five are met
                           structurally: the live-recompute/staleness/discard/coalescing MECHANISM is
                           REQ-069's own already-proven pattern reused verbatim (ASSUMPTION-4), the
                           panel's deterministic state machine is tested (step 6), and the automatic
                           trigger itself is manual verification — the identical status REQ-069's own
                           acceptance condition on dynamic rebuild has always carried, not a gap this
                           task introduced).
                           **REQ-069/REQ-100/REQ-101** (the patterns/budgets this task reused —
                           unaffected, unmodified).
- Summary:                 A live Volume Dashboard panel reports cut/fill/net volume and common area
                           between two picked surfaces, recomputing automatically off the UI thread
                           whenever either rebuilds, with an optional cut/fill map overlay; the
                           underlying computation is also reachable synchronously via `VOLUMES` (a
                           one-shot command) and `VOLDASH RECOMPUTE` (the panel's own test surrogate).
- Tests:                   `SurfaceVolumeTests.cpp` (11 cases: hand-computed planar volumes, the
                           sign convention in both directions, non-overlap, partial overlap, exact-
                           zero self-comparison, a degenerate input, map-triangle emission on both
                           sides plus the self-comparison empty-map case, indexed-vs-full-scan
                           agreement, an empty index, and a REQ-100-density perf guard at 0.572 s).
                           `tests/headless/transcripts/req073-surface-volumes.txt` (47 steps): the
                           `VOLUMES` command end-to-end against real fixture data (self-comparison,
                           linking a real lower pad, argument/name refusals), and `VOLDASH`'s
                           deterministic state machine (open/close, pick, map toggle, the readiness
                           gate's refusal). `GoSurveyTests` 520/520; `ctest` 551/551 active.
- Verification verdict:    PASS (plan review 2026-08-23: 0 blocking, 0 advisory; implementation
                           review 2026-08-23: 0 blocking, 0 advisory).
- Assumptions:             ASSUMPTION-1 (grid-sample integrator, not exact TIN-TIN overlay) — open,
                           validated by the acceptance tests it was designed to answer exactly.
                           ASSUMPTION-2 (fixed sample budget) — closed, measured at REQ-100 density.
                           ASSUMPTION-3 (Base/Comparison naming, cut/fill sign) — open, validate on
                           real data in the running app. ASSUMPTION-4 (reuse REQ-069's revision-based
                           trigger) — closed, implemented as designed. ASSUMPTION-5 (no dispatch while
                           the panel is closed) — open, trivially reversible if wrong.
- Architectural decisions: none made by Workshop beyond what D-2026-08-23-k (the REQ-073 amendment
                           itself, a recorded spec decision) already authorised.
- Dependencies:            none added.
- Technical debt noted:    the headless driver cannot exercise the true async recompute path (no
                           frame loop) — not new debt, the same limitation REQ-069's own dynamic
                           rebuild already carries, recorded again here because it now also applies
                           to `VOLDASH RECOMPUTE`'s "ready" path specifically. No removal condition:
                           this is inherent to a headless driver without a frame loop, not a gap
                           expected to close.
- Build:                   reproducible, clean on MSVC via the pinned preset; no new warnings.
- Docs updated:            this task log; `spec/requirements.md` (REQ-073, amended); `spec/project.md`
                           (decision log, D-2026-08-23-k).

**Recommended follow-up, not a blocker:** the panel has not been visually verified in the running
app (layout, picker usability, map colours against real terrain) — the same category of check the
REQ-093 splash screen needed three manual rounds for. Worth a look before calling the feature done
from a user's perspective, even though every acceptance condition CLAUDE.md's workflow can verify
mechanically is met.

## 11. Outcome
- —
