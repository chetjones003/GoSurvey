# TASK-271 — Extract 3D centerline from a cylindrical point-cloud cluster

- Type:    feature
- Status:  done
- Opened:  2026-09-18
- Owner:   Claude (chetjones003@gmail.com)

## 1. Authority
- Goal:         point-cloud epic (REQ-171/172 continuation)
- Requirements: REQ-347 (accepted)
- Constraints:  CON — no new dependency, no new architectural layer, local-storage coordinate
  invariant (`project_local_storage_invariant`)
- Acceptance:   see REQ-347 Acceptance list verbatim in spec/requirements.md
- Owning subsystem: Commands (interactive command), Domain (`src/util/cylinderfit.*`), UI (ribbon
  button/icon), Renderer (preview line, existing 3D rubber-preview path)

## 2. Scope
- In scope: hover-preview + click-commit EXTRACTCENTERLINE command; least-squares cylinder-axis fit
  function operating on a point buffer; ribbon button + generated icon; unit tests for the fit.
- Out of scope: multi-cylinder batch extraction, curved/tapered pipe fitting, editing/grip-editing the
  resulting line differently from any other LINE, DXF/DWG round-trip changes.
- Smallest change: one new Domain header/source for the fit math, one new command in
  `CadCommands.cpp`, one ribbon button in the existing `kRibbonTabPointCloudCtx` section, one new
  generated icon.

## 3. Architectural boundary check
- [x] No — proceed. (Reuses `pointcloudcache::{Open,ReadLeafPoints}` and
  `pointcloud::QueryLeavesNearPoint`/`SelectLodLeavesInCylinder`, already public Domain APIs; fit math
  is a new pure function, not a new abstraction; no new dependency — 3x3 Jacobi eigen-decomposition is
  hand-written in-tree.)

## 5. Assumptions
```
ASSUMPTION-1: the axis direction is well-approximated by the dominant PCA eigenvector of the local
neighborhood (largest-eigenvalue direction of the covariance matrix).
- Because: REQ-347 doesn't specify a fit algorithm, only "least squares"; for a cylindrical shell
  neighborhood elongated along its axis (the common case for a hovered pipe segment), the best-fit
  line through the points (which PCA's dominant eigenvector gives, minimizing sum of squared
  perpendicular distances) is a legitimate least-squares axis estimate.
- Risk if wrong: a neighborhood that isn't elongated along the pipe (e.g. a very short axial extent,
  wider than long) could pick the wrong dominant direction.
- Validate by: synthetic-cylinder unit test with a neighborhood radius smaller than the axial search
  span; user's manual GUI check on a real scan.

ASSUMPTION-2 (SUPERSEDED — proven wrong by the user's own GUI test, see the 2026-09-18 log entry):
increment 1 fit against the REQ-171 bounded PREVIEW sample only, not the full-density `.gscloud`
out-of-core cache.
- Because: the renderer's own out-of-core LOD path (`ViewportRenderer.cpp`) only reads disk-backed
  leaves through a throttled background prefetch thread specifically because per-frame disk reads
  measurably stalled orbiting (TASK-270 parts 11/13). A per-frame hover handler reading leaves
  directly would reproduce that exact cost with no throttling of its own.
- Risk if wrong: on a scan whose bounded preview sample is too sparse over a given pipe, the
  residual/point-count gate may reject a real cylinder that the full-density cache would have fit.
- Validated: WRONG, immediately, on the user's first real-scan test (188M points, 2M preview) —
  every hover over a visibly correct pipe found "no cylinder." The renderer's concern is about a
  LARGE camera-proximity reselect (up to 1500 leaves); a single small-radius neighborhood query is a
  different, boundable cost, not the same problem. Replaced with a real out-of-core read, hysteresis-
  throttled — see REQ-347's "Field-tested correction" revision and the task log below.
```

## 6. Plan
- Approach: PCA (3x3 covariance eigen-decomposition, Jacobi) for axis direction through the
  neighborhood centroid; project points to the plane perpendicular to the axis, least-squares circle
  radius = mean radial distance; RMS residual of radial distances vs. that mean gauges cylindricality;
  reject if residual/radius exceeds a threshold or too few points. Inlier extent = min/max of point
  projections onto the axis, within one residual-tolerance band of the mean radius.
- Files/functions to touch:
  - `src/util/cylinderfit.hpp/.cpp` (new) — `FitCylinderAxisLeastSquares`
  - `src/commands/CadCommands.hpp/.cpp` — new `EXTRACTCENTERLINE` command state + hover/click/Esc
    handling, nearest-point-under-cursor + neighborhood gather via existing octree/cache APIs
  - `src/ui/CadUi.cpp` — ribbon button in the Point Cloud contextual tab
  - `tools/gen_c3d_icons.cpp` — new icon
  - `tests/` — new test file for the fit function (synthetic cylinder, noisy cylinder, planar reject,
    too-few-points reject)
- Test approach: happy path = synthetic point-on-cylinder fixture recovers axis/center/radius within
  REQ-101 tolerance; failure mode = planar cluster and sparse cluster both reject (no result).
- Steps:
  - [x] write cylinderfit math + unit tests
  - [x] wire command (hover/preview/commit/Esc)
  - [x] wire ribbon button
  - [x] generate icon
  - [x] self-verify (build/tests)

## 9. Self-verification
- [x] build-project        — PASS (full `GoSurvey`/`GoSurveyTests`/`GoSurveySnapTests`/
      `gosurvey_headless` build clean)
- [x] architecture-review  — PASS (no new layer/dependency/abstraction; reused existing
      Commands/Domain/UI/Renderer ownership; the out-of-core-cache scoping decision is recorded as a
      stated increment, not a silent gap)
- [x] code-review          — PASS (mirrors existing precedents throughout: HATCH's own-entry-point
      click route, PickClosestCadEntity's point-cloud hover walk, WaterDrop's ribbon-button wiring)
- [x] dependency-audit     — PASS (no new dependency; hand-written 3x3 Jacobi eigensolver)
- [x] performance-review   — PASS/n-a (hover cost bounded by the existing REQ-171 preview cap,
      the same walk `PickClosestCadEntity` already performs every hover frame; no new disk I/O)
- [x] testing              — PASS (8 `[cylinderfit]` cases green; full suite run before/after with
      identical pre-existing 8 failures on `beta`, confirmed unrelated)

## 10. Verification result
- Submitted:  2026-09-18
- Verdict:    PASS
- Findings:   none blocking

## 11. Outcome
- Requirements satisfied: REQ-347 (Acceptance met: yes — fit correctness/tolerance, LINE commit with
  inlier-extent endpoints, no-preview on non-cylindrical/sparse neighborhoods, Esc cancel, ribbon
  button + icon, headless-testable fit function)
- Tests added:            tests/CylinderFitTests.cpp (8 cases)
- Refactors:              none
- Docs updated:           spec/requirements.md (REQ-347 added), this task file
- Done:                   2026-09-18

## 8. Implementation log
- 2026-09-18 — REQ-347 accepted, task opened, implementation started.
- 2026-09-18 — `src/util/cylinderfit.{hpp,cpp}`: PCA + radial-least-squares cylinder fit, hand-written
  3x3 Jacobi eigen-decomposition (no new dependency). `tests/CylinderFitTests.cpp` (8 cases: exact
  axis-aligned cylinder, tilted/off-origin cylinder, radial noise tolerance, planar-cluster rejection,
  random-scatter rejection, too-few-points rejection, empty-input rejection, tighter-gate rejection).
  All green standalone before any GUI wiring.
- 2026-09-18 — Command layer: `Kind::ExtractCenterline`, `AppCommandState` preview fields,
  `StartExtractCenterlineCommand`/`UpdateExtractCenterlineHover`/`SubmitExtractCenterlineViewportPick`
  in CadCommands.hpp/.cpp. Hover walks each visible cloud's bounded preview sample (REQ-171) to find
  the nearest point to the pick ray (mirrors `PickClosestCadEntity`'s existing point-cloud branch),
  gathers a fixed-radius neighborhood, and calls the fit. Commit pushes a `userLinesFlat` LINE, one
  `PushUndoSnapshot` step.
- 2026-09-18 — Discovered REQ-345 was already taken (`CadPipeRun`, issue #486) — renumbered this
  requirement to REQ-347 throughout (spec, task, code comments, tests) before any further work.
- 2026-09-18 — Scoping decision recorded in REQ-347 as a stated increment: the hover reads only the
  REQ-171 bounded PREVIEW sample, not the full-density `.gscloud` out-of-core cache, because the
  renderer's own LOD path only reads disk-backed leaves through a throttled background prefetch
  thread specifically to avoid per-frame disk I/O (TASK-270 parts 11/13) — a per-frame hover handler
  reading leaves directly would reproduce that exact cost. ASSUMPTION-2 records the risk.
- 2026-09-18 — `ViewportPickPolicy.hpp`: new `ExtractCenterlinePick` route (own entry point, like
  TRIM/HATCH) — the click commits the hover's fit, not a fresh coordinate/entity pick.
- 2026-09-18 — `CadRubberPreview.cpp`: preview LINE drawn straight from the hover's stored fit
  endpoints — the one branch in that function that reads no cursor-driven construction state.
- 2026-09-18 — Ribbon: `RibbonIconKind::PcExtractCenterline`, "Extract Centerline" button added to
  the existing `kRibbonTabPointCloudCtx` section (`CadUi.cpp`), gated the same way the existing
  Point Cloud Display controls are (a point cloud must be selected first).
- 2026-09-18 — `tools/gen_c3d_icons.cpp`: new `c3d_extractcenterline` design (pipe capsule outline +
  dashed centerline axis), compiled and run; only the one new PNG was written (`git status` confirmed
  no existing icon changed).
- 2026-09-18 — Full suite run twice (feature branch and `beta` baseline via `git stash`): the same 8
  tests fail identically on both — pre-existing on `beta`, unrelated to this feature (PIPERUN/OFFSET/
  surface-selection/feature-line/solid-isolines/solid-primitives; none of those files were touched).
  All `[cylinderfit]` tests and the rest of the suite pass on the feature branch.
- 2026-09-18 — **User GUI test against a real 188M-point plant scan (issue #538 screenshot):
  consistent "no cylinder found" hovering directly over a visible pipe.** Root cause was exactly
  ASSUMPTION-2's predicted risk, confirmed: `188,439,985` points capped to a `2,000,000`-point
  preview strides roughly 1-in-94 through each octree leaf, so a 2 ft real-world neighborhood in the
  preview sample was frequently near-empty even sitting on a pipe. Fixed by reading the actual
  `.gscloud` out-of-core cache (ADR-060) for the neighborhood once the coarse target is known
  (`GatherExtractCenterlineNeighborhoodFromCache`, `pointcloud::QueryLeavesNearPoint` +
  `pointcloudcache::ReadLeafPoints` against a handle kept open across hover frames in
  `st.extractCenterlineCache`), re-querying only when the hover has moved past a 0.3x-radius
  hysteresis band (reusing the last neighborhood otherwise) so this stays a small, occasional,
  bounded read — nothing like the renderer's own much larger per-frame LOD reselect. Falls back to
  the bounded preview sample when a cloud has no out-of-core cache. REQ-347 revised accordingly (the
  earlier "stated increment" text no longer describes the shipped behavior). Rebuilt, `[cylinderfit]`
  still 8/8, full suite unchanged (same pre-existing 8 failures).
- 2026-09-18 — **Second round of the same real-scan test: still "no cylinder found," hovering
  directly over the same pipe.** The first fix only moved STEP 2 (the fit neighborhood) onto the real
  cache; STEP 1 — deciding whether the cursor is even near a cloud at all, and WHERE — still walked
  only the sparse preview sample, gated by the tight pixel-derived hover tolerance
  (`CadHoverEntityPickTolWorld`). At ~1-in-94 preview stride, the nearest PREVIEW point to the ray was
  routinely several feet away even sitting squarely on a visible pipe, so step 1 returned before step
  2's cache read was ever reached — the actual root cause both times traces to the same place
  (locating a point near the cursor at all), not the neighborhood gather. Fixed by making step 1
  cache-aware too: `pointcloud::SelectLodLeavesInCylinder` (the same primitive the renderer's own
  LOD pass uses, at a much smaller `kMaxProbeLeaves = 24` cap here) finds real leaves near the ray's
  LINE, their points are read via `ReadLeafPoints`, and the true nearest point/distance is computed
  against real density; a cache-less cloud still falls back to the preview walk. The gate is now
  `max(pixel-tolerance, neighborhood radius)`, since the coarse locate no longer needs pixel
  precision — step 2's neighborhood, centred on step 1's own answer, is what determines the final
  fit. Introduced `AppCommandState::ExtractCenterlineCacheEntry` / `extractCenterlineOpenCaches` (a
  small vector, one open cache per hovered cloud, replacing the earlier single-slot design) since
  step 1 may need to probe more than one cloud's cache in a multi-cloud drawing.
  REQ-347 and ASSUMPTION-2 revised again to describe this. Rebuilt clean, `[cylinderfit]` still 8/8,
  full suite unchanged (same pre-existing 8 failures). GUI re-verification against the real scan is
  the user's next step.
- 2026-09-18 — **Third GUI round**: cylinder now found, but the diagnostic added above (surfacing
  the specific refusal reason on a miss-click) showed exactly why the *fit* itself still failed once
  hovering worked: "56238 points nearby, but they don't fit a cylinder closely enough." A 2.0 ft
  search radius on this scan's real density pulled in tens of thousands of points spanning more than
  one structure (a crossing beam, bend, or bracket near the pipe), which the residual gate correctly
  refused rather than average into a wrong axis. Fixed by shrinking the default radius to 0.6 ft
  (tight enough to usually stay on one member) and loosening `maxResidualToRadiusRatio` from 0.15 to
  0.20 (real scan noise/insulation/welds). User then asked to make both configurable rather than
  hard-coded, since the right values are scan-dependent — added `extractCenterlineSearchRadiusFt`
  and `extractCenterlineMaxResidualRatio` to `AppCommandState` (session-global, mirroring
  `pointCloudDisplay`'s existing shape) and two sliders (Search Radius 0.1–5 ft, Fit Tolerance
  1–50%) in a new "Centerline Fit" ribbon section beside the Extract Centerline button. Rebuilt
  (`GoSurveyTests`/`gosurvey_headless`/`GoSurveySnapTests`/`GoSurvey`, the last needed the app closed
  once — it was still running from the prior GUI test and briefly locked the linker), `[cylinderfit]`
  still 8/8, full suite unchanged (same pre-existing 8 failures).
- 2026-09-18 — **User confirmed working** against the real 188M-point scan. Task complete.
