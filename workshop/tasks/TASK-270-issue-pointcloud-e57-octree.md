# TASK-270 — Point cloud entity + out-of-core octree + E57 import (increment 1)

- Type:    feature
- Status:  implement
- Opened:  2026-09-17
- Owner:   Chet Jones

## 1. Authority
- Goal:         Point-cloud epic (File Format Specs, D-2026-08-29-g)
- Requirements: REQ-171 (point cloud entity), REQ-172 (E57 import), REQ-100 profile (e) (proposed
  target, D-2026-09-17-e)
- Constraints:  ADR-042 (point cloud entity kind, no plugin API), ADR-060 (`.gscloud` cache),
  D-2026-09-17-d (E57 delivered first, ahead of PTS/PTX/LAS/LAZ)
- Acceptance:   (restated from REQ-171/172, scoped to increment 1 — see Scope below)
  - a cloud appears in the viewport, selects as one object, erase/undo restores it
  - extents include the cloud; freeze/off/non-plottable on its layer hides it
  - an unrelated edit does not deep-copy the payload (shared immutable pointer)
  - DXF/DWG export names the cloud exclusion in the log
  - a pre-existing `.gs`/drawing without a point cloud section still opens unchanged
  - a known-good E57 imports; malformed/truncated E57 refuses with a specific message,
    drawing unchanged; missing file/empty path: no crash
  - reopening reuses the `.gscloud` cache instead of re-indexing (measurably faster)
  - a stale/corrupt `.gscloud` triggers a logged rebuild, never a crash or wrong result
- Owning subsystem: Domain (`src/commands/CadEntities.hpp`, new `src/util/pointcloudoctree.*`),
  IO (`src/io/`), Renderer (`src/render/ViewportRenderer.*`)

## 2. Scope
- In scope: `CadPointCloud` entity type; in-tree octree builder/query (pure Domain code, no file
  IO, no GL); `.gscloud` cache codec (ADR-060) read/write/stamp-check; E57 reader via
  libE57Format; minimal LOD point renderer; entity lifecycle wiring (select/erase/undo/extents/
  layer visibility); DXF/DWG export exclusion log line; `.gs`/DWG-trailer additive persistence.
- Out of scope (future increments, each its own verification pass): Point Cloud ribbon UI
  (size/LOD/color-scheme controls), PTS/PTX/LAS/LAZ readers, REQ-100 profile (e) benchmark
  instrument (`BENCH POINTCLOUD`) against the full 7.8GB file, point-cloud color-scheme rendering
  beyond raw RGB passthrough.
- Smallest change: entity + octree + cache codec are dependency-free and land first; E57 reader
  is the only piece gated on vendoring libE57Format (see BLOCKER below).

## 3. Architectural boundary check
- [x] No — proceeds per the already-recorded REQ-171/172/ADR-042/ADR-060 decisions. No new
  abstraction/layer/dependency/ownership model beyond what those decisions already authorized.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| 1 | Vendor libE57Format now (build from source, needs Xerces-C) vs. defer E57 reader to a follow-up task once vendored | 2026-09-17 | pending |

## 5. Dependency vendoring (resolved)
`libE57Format` (BSL-1.0) and its dependency Xerces-C (Apache-2.0) were built from source with
MSVC/Ninja and vendored as prebuilt static libs, same pattern as LibreDWG:
- `third_party/xerces-c/` — headers + `lib/win-x64/xerces-c_3.lib`, `VENDORED.md` with rebuild
  steps. Pin: `31b4b3a06105dcd607db9fda9d1883ad7e489bfe` (v3.3.0 branch tip).
- `third_party/libe57format/` — headers + `lib/win-x64/E57Format.lib`, `VENDORED.md` with rebuild
  steps. Pin: `1a5f9a830c49c117eef2d64f9522b8ecd7d6e1a4`. Built with `E57_RELEASE_LTO=OFF` — the
  default ON bloats the static lib ~9x (`/GL` object files) for no benefit since GoSurvey's own
  link is not `/LTCG`.
- `CMakeLists.txt`: `XercesC::xerces_c` and `E57Format::E57Format` imported targets, linked
  alongside `LibreDWG::libredwg` everywhere that target already appears (`gosurvey_domain`,
  `GoSurvey`, `gosurvey_headless`, `GsJsonDwgFixture`, `GoSurveyTests`, `GoSurveySnapTests`).

## 6. Implemented (this task)
- `CadPointCloud` entity struct (`src/commands/CadEntities.hpp`) — `shared_ptr<const>` payload,
  interleaved XYZ + optional RGB/intensity, per REQ-171/ADR-042(a) precedent (mirrors `CadMesh`).
- `pointcloud::Octree` (`src/util/pointcloudoctree.{hpp,cpp}`) — pure Domain octree builder/query,
  no file IO, no GL. Points partitioned into contiguous octant buckets (no copy of point data,
  only an index permutation). 6 Catch2 tests (`tests/PointCloudOctreeTests.cpp`): empty input,
  every point owned exactly once (no loss/duplication), root bounds, degenerate single-point
  input, and sphere-query leaf selection.
- `pointcloud_e57::ReadE57File` (`src/io/CadPointCloudE57.{hpp,cpp}`) — adapts libE57Format's
  Simple API (`e57::Reader`, `Data3D`, `Data3DPointsData_t`) to GoSurvey's flat layout. Never
  throws — `E57Exception` and `std::exception` are caught at the boundary and turned into
  `ReadResult{ok=false, errorMessage}`; invalid per-point cartesian/color/intensity flags are
  respected (invalid points skipped, never stored as a silent zero). 4 Catch2 tests
  (`tests/CadPointCloudE57Tests.cpp`): a fixture E57 written with libE57Format's own Simple
  Writer round-trips point count + XYZ + RGB; a garbage-bytes file is refused with no partial
  result; a missing path and an empty path are both refused without crashing.
- Full suite green: 1185 test cases / 8,923,586 assertions, no regressions.

## 7. Entity lifecycle wiring (this session, part 2 — done)
`CadPointCloud` is now a first-class entity, wired everywhere `CadMesh` is, by grepping every
`cadMeshes`/`cadMeshAttrs` reference site and mirroring it:
- `EntityKind::PointCloud` and `SelectedEntity::Type::PointCloud` added, both **appended last**
  (architecture note on `EntityKind::Solid` — inserting anywhere but the end renumbers ids in
  every existing drawing on next load).
- `AppCommandState`, `DrawingDocument` (tab-switch copy), and `DrawingGeometrySnapshot` (undo) all
  carry `cadPointClouds`/`cadPointCloudAttrs`; undo capture/restore, tab save/load, and clear-all
  all copy/clear them alongside meshes.
- Selection/erase: erase-by-selection block mirrors the mesh block (`SelectedEntity::Type::PointCloud`
  → erase from `cadPointClouds` + `cadPointCloudAttrs`, undoable in one step per REQ-171).
- Extents/zoom-extents: both the drawing-extents walk and the outlier-trimmed zoom-extents walk
  gained a point-cloud block (over resident points only — ADR-060 notes the full cloud may not be
  loaded).
- Gizmo/transform: `PointCloud` added to the display-only group (`Mesh`/`Surface`/`PipeRun`) that
  never contributes to a gizmo's bounds — REQ-171's stated "not grip-edited" boundary.
- `AttrsForKind`, `CadEntityAttrsForSelected`, `CollectIsolatableIds`, `CadEntityPickDepthAtPick`,
  `docinvariants.cpp`'s attr-count/sweep/selection-capacity checks, and `CadUi.cpp`'s
  `SelectedEntityAttr` — all gained a `PointCloud` case so layer/colour lookup, REQ-084 isolation,
  pick depth, and document-invariant checks all see the new entity kind.
- EXPLODE's "other kinds left untouched" classifier now names `"point cloud"` rather than falling
  through (it has a `default:`-free switch, so this was a compile error, not a silent gap).
- **Incidental fix**: `docinvariants.cpp`'s selection-capacity switch was missing a `PipeRun` case
  (pre-existing — MSVC doesn't warn on this switch, only clang's `-Wswitch` does, which is why it
  survived). Found because adding `PointCloud` to the same switch made clang re-flag the file;
  fixed alongside since it's the same one-line shape as every other case.
- `.gs`/DWG-trailer persistence (`GsIo.cpp`): additive `pointClouds`/`pointCloudAttrs` JSON
  sections (ADR-020 (d) tolerant-key precedent — omitted entirely when empty, so a pre-REQ-171
  drawing round-trips byte-identically). Load validates point/colour/intensity array lengths
  agree and refuses (REQ-201) a malformed entry rather than loading it truncated/misaligned. A
  load-summary log line reports cloud/point counts, mirroring the mesh one.
- DXF/DWG export exclusion log (`LibreDwgCad.cpp`): `"CAD export — skipped N point cloud(s); no
  native point-cloud object in the R2004/R2000 DWG subset GoSurvey writes (REQ-171, ADR-042 (e))."`
  — closes REQ-171's "DXF/DWG export names the cloud exclusion in the log" acceptance condition.
- New `POINTCLOUDATTACH` command (aliases `IMPORTE57`, `E57`): reads the file via
  `pointcloud_e57::ReadE57File`, builds an octree over it to validate the pipeline end-to-end,
  commits one `CadPointCloud`, logs the import (and explicitly logs that no `.gscloud` cache is
  written yet, so reopening will re-index — a stated limitation, not a silent one). Wired to a new
  `BrowseOpenFileE57Utf8` file-picker (Windows dialog + headless queue-driven stub, following the
  existing `BrowseOpenFileGltfUtf8` pattern exactly, including the windowed/non-Windows/headless
  three-implementation split).
- Full verification: `gosurvey_domain`, `GoSurvey.exe`, `gosurvey_headless.exe`,
  `GsJsonDwgFixture.exe`, `GoSurveyTests.exe`, and `GoSurveySnapTests.exe` all build clean.
  `GoSurveyTests`: 1185 cases / 8,923,586 assertions green. `GoSurveySnapTests`: 327 cases / 3,040
  assertions green. No regressions.

## 8. Rendering (this session, part 3 — done, pending visual confirmation)
Flat (no-LOD) render path added to `ViewportRenderer`, deliberately **not** building octree-driven
LOD yet: `workshop/implementation-rules.md` §5 is explicit that unmeasured optimisation is not
added speculatively, and REQ-100 profile (e) has no measurement yet to justify one. This is the
smallest correct thing that makes an imported cloud visible.
- `RenderScene`'s `pointClouds`/`pointCloudAttrs` parameters were appended at the **very end** of
  its signature, not inserted alongside `meshes`/`meshAttrs` where they'd conceptually sit — that
  call is entirely positional at its one call site (`main.cpp`), so inserting mid-list would have
  silently reassigned every later argument to the wrong parameter. Documented inline as to why.
- **Reuses `vcLineProgram_`** (the existing vertex-colour line shader, pos+RGBA layout, REQ-341
  clip-plane support already built in) fed `GL_POINTS` instead of `GL_LINES`, rather than writing
  a new shader — same layout, so no second program to maintain.
- `PointCloudGpuEntry` mirrors `MeshGpuEntry` exactly: immutable payload (architecture §11.5),
  `weak_ptr` identity+liveness, upload once, only re-upload when the view anchor drifts (never
  because geometry changed — it can't, it's immutable).
- Colour: per-point RGB from the cloud when present; otherwise the resolved layer/entity colour is
  baked into every vertex at upload time (no second uncoloured shader variant).
- Drawn in **every** visual style (unlike meshes, which are Shaded-only) — same reasoning REQ-068
  gives for TIN surfaces: a point cloud has no faces to shade or hide, so restricting it to Shaded
  would make it invisible in the default 2D Wireframe view.
- Layer visibility (`on`/`frozen`) and REQ-084 isolation (`hiddenEntityIds`) both respected, same
  gating as the mesh block.
- Point size is a **fixed constant** (`glPointSize(2.0f)`) — the ribbon size/LOD/colour-scheme
  controls are the next increment.
- Build verified: `GoSurvey.exe`, `gosurvey_domain`, and both Catch2 suites all green (1185 + 327
  cases, no regressions). **Not yet visually confirmed** — GL rendering isn't something this
  session can screenshot-verify itself (see `project_gui_hover_not_automatable` precedent); needs
  a manual check: import the sample E57 and confirm points actually appear in the viewport.

## 9. User test result (real 7.8GB E57, part 3's flat renderer)
Confirmed working, but exactly the two problems anticipated: **<5fps** (no LOD, every resident
point submitted every frame) and an **incomplete cloud** (memory exhausted — the whole-file-into-
one-`std::vector` limitation named in part 3's log was the actual cause). User chose, when asked:
build full out-of-core streaming now (not a point-count cap stopgap), and distance/screen-size
stride-based LOD as the strategy (not frustum-culling-only).

## 10. Out-of-core `.gscloud` cache — implemented (this session, part 4)
- `src/io/CadPointCloudE57.hpp/.cpp` gained `StreamE57File` — chunked reading (default 1,000,000
  points/chunk) using **manually-sized** libE57Format buffers rather than
  `Data3DPointsData_t(header)` (which allocates for the WHOLE scan up front — the exact anti-
  pattern this exists to avoid). `ReadE57File` (whole-file) is kept for the existing small-file
  round-trip tests; production import no longer calls it.
- `src/util/pointcloudcache.{hpp,cpp}` (new) — the ADR-060 `.gscloud` codec:
  - `BuildFromE57`: one bounded-memory streaming pass writes every point as a fixed-size raw
    record straight to a temp file (folding bounds/count/hasColor/hasIntensity along the way), then
    **recursive on-disk bucket-splitting** (pure local file IO, no further E57/network access)
    subdivides that temp file into an octree — each node's point data staying in its own temp file
    until small enough to become a leaf (50,000-point threshold, matching
    `pointcloudoctree.hpp`'s default; max depth 16). Root ends up last in build order and is
    swapped into index 0 to satisfy `pointcloud::Octree`'s "nodes[0] is the root" contract.
    Finally writes the real `.gscloud`: header + node table + leaf data copied in from the temp
    files (deleted as consumed). A failure at any point removes the partial cache file and the
    whole temp directory — never a half-written cache (REQ-001).
  - `Open`: reads header + node table only (small, always safe to hold in memory) into an
    `OpenCache`.
  - `ReadLeafPoints`: seeks into the cache file and decodes one leaf's point block on demand — the
    primitive both the import preview (below) and a future renderer pager use.
  - `MatchesSource`: compares the cache's stored size/mtime stamp against the source file's
    current stat (ADR-060 (b)) — `ImportPointCloudE57` now checks this and **reuses** an existing
    matching cache instead of rebuilding.
  - **Known simplification, documented in code**: `hasColor`/`hasIntensity` are decided from the
    first non-empty chunk and held fixed for the whole file; a source with genuinely inconsistent
    per-scan schemas would get zero-filled channels on a later scan that disagrees. Rare in
    practice and out of scope for this increment to generalize.
  - 6 new Catch2 tests (`tests/PointCloudCacheTests.cpp`): round-trip count/points, forced
    multi-leaf split (200k points), stamp match/mismatch, missing-cache-file refusal, garbage-file
    refusal, and malformed-source-leaves-no-partial-cache.
- `ImportPointCloudE57` (`CadCommands.cpp`) rewritten: builds/reuses the `.gscloud` cache instead
  of reading the whole file into memory, then populates `CadPointCloud` with the octree (small)
  plus a **bounded, spatially-representative preview sample** (`BuildPreviewFromCache`, capped at
  2,000,000 points, sampled by a uniform per-leaf stride so every part of the cloud contributes
  proportionally rather than only whichever leaves were visited first).
- `CadPointCloud` (`CadEntities.hpp`) gained `cloudCachePath` and `octree` fields; `pointsXyz` is
  now documented as the bounded preview, not the whole cloud.
- `.gs` persistence (`GsIo.cpp`): the `cloudCache` path is now saved/loaded; on load, the cache is
  reopened to repopulate the octree — a missing/stale cache degrades to preview-only display with
  a logged reason, never a load failure.
- Full suite verified: `gosurvey_domain`, `GoSurvey.exe`, `gosurvey_headless.exe`,
  `GsJsonDwgFixture.exe` all build clean; `GoSurveyTests` 1191 cases / 8,953,611 assertions,
  `GoSurveySnapTests` 327 cases / 3,040 assertions, both green, no regressions.
- **This alone should fix both reported symptoms**: import can no longer exhaust memory (bounded
  streaming + bounded preview), and the renderer (still the flat part-3 draw path, unchanged this
  round) now only ever submits the ≤2,000,000-point preview per frame regardless of source file
  size, rather than however many hundreds of millions of points fit before OOM.

## 10a. User report: hung import + explicit progress-bar request
Real 7.8GB file: `point-cloud cache build: cannot read a temp leaf file .` then the app froze
("not responding"). Two distinct problems, both fixed this session (part 5):

1. **Real bug — `isLeaf()` was wrong.** Both `pointcloud::Node::isLeaf()` and
   `pointcloudcache::BuildNode::isLeaf()` tested only `children[0] < 0`. An INTERIOR node whose
   octant 0 happens to hold zero points — routine for a real scan's spatial distribution — also
   has `children[0] == -1`, so it was misclassified as an empty leaf (matching the exact symptom:
   an empty `leafFile` path, "cannot read a temp leaf file"). Every point in that node's other
   seven (non-empty) octants was silently orphaned — this bug existed in the in-memory octree too,
   just never surfaced because every prior test happened to fill octant 0. Fixed by checking ALL
   eight children are -1, in both places. Two regression tests added (`PointCloudOctreeTests.cpp`,
   `PointCloudCacheTests.cpp`) that deliberately force an empty-octant-0 split and assert no points
   are lost.
2. **The "freeze" was real slowness, not a hang.** `SplitOrKeep`'s recursive on-disk bucket split
   read and wrote ONE RECORD AT A TIME (a handful of bytes) per point per octree level — for a
   ~800M-point file across ~5 levels, that is billions of tiny OS-transition-incurring calls.
   Fixed: (a) every stream now gets a 1 MiB internal buffer via `pubsetbuf` (called before `open`),
   turning "one OS write per record" into "one per ~1 MiB"; (b) `SplitOrKeep` reads/routes points
   in large in-memory blocks instead of one at a time; (c) the root streaming pass assembles a
   point's xyz/rgb/intensity into one buffer and issues a single `write()` instead of up to three;
   (d) `ReadLeafPoints` reads a whole leaf (bounded to ~50,000 points, a few MB) in one `read()`
   instead of one per point. All 17 point-cloud tests still pass, now in ~1 second total.

User also asked, mid-report, for a centered progress bar with an ETA during import — implemented
in the same pass (§10b), since a build this slow needs both the speed fix AND visible progress.

## 10b. Background import + progress UI — implemented (this session, part 5)
- `pointcloudcache::BuildFromE57` gained optional progress/cancel parameters: two
  `std::atomic<std::int64_t>*` counters (`pointsStreamedOut` — phase 1, `pointsFinalizedOut` —
  phase 2, incremented each time `SplitOrKeep` finalizes a leaf) and a `const std::atomic<bool>*
  cancelRequested`, checked at the top of the streaming callback and at each block iteration of the
  split loop. A cancellation is treated exactly like any other failure — partial cache and temp
  files removed, `*errorMessage` set to "Import cancelled."
- `pointcloud_e57::QuickPointCountEstimate` (new) sums each Data3D scan's declared `pointCount`
  from its header — metadata only, no point data read — cheap enough to call synchronously before
  dispatch, to size the progress bar's total.
- `AppCommandState::PointCloudImportAsync` (new, `CadCommands.hpp`) — architecture §8's one-shot-
  worker contract, same shape as `SurfaceRebuildAsync`: a `std::thread` + atomics the main thread
  polls, destructor requests cancellation and joins (never `std::terminate`s on a live thread). The
  worker (`RunPointCloudImportWorker`) touches ONLY the job struct and read-only paths — never
  `AppCommandState` — building the cache, reopening it, and reading the preview sample entirely on
  the background thread.
- `StartPointCloudImportAsync` / `TickPointCloudImport` replace the old synchronous
  `ImportPointCloudE57`: the command handler starts the job and returns immediately; a new
  `TickPointCloudImport(cmd, cmdLog)` call in `main.cpp` (beside `TickSurfaceRebuilds`) reaps a
  finished job once per frame and — on the MAIN thread — does the undo snapshot, entity-id
  assignment and GPU-cache bump, exactly as the old synchronous path did. Only one import may be in
  flight at a time (a second `POINTCLOUDATTACH` while one runs is refused with a logged reason).
- `DrawPointCloudImportProgress` (new, `CadUi_Modals.cpp`) — a centered modal (same
  `SetNextWindowPos`-every-frame pattern as `DrawUpdateDialog`), showing the active phase ("Reading
  scan..." / "Building spatial index..."), a blended progress bar (40% phase 1 + 60% phase 2 — the
  split phase re-reads/re-writes the dataset across several octree levels and is typically the
  slower half), an estimated time remaining (extrapolated from elapsed time; suppressed as
  "estimating..." until the fraction is past a noise floor), and a Cancel button. Wired into
  `main.cpp` beside the existing `DrawAlignResultsWindow`/`DrawUpdateDialog` modal calls.
- Full suite verified green after this round too: `GoSurveyTests` 1193 cases / 8,954,028
  assertions, `GoSurveySnapTests` 327 cases / 3,040 assertions — all six build targets compile
  clean (`gosurvey_domain`, `GoSurvey.exe`, `gosurvey_headless.exe`, `GsJsonDwgFixture.exe`,
  `GoSurveyTests.exe`, `GoSurveySnapTests.exe`).
- **Not yet re-tested against the real 7.8GB file** — the isLeaf() fix and bulk-IO rewrite are
  unit-tested at up to 200k points (multi-leaf-split confirmed) but the actual multi-hundred-
  million-point, multi-GB case has not been re-run since these fixes; that is the next thing to
  verify.

## 10c. User report: ETA ticking per-frame, "hangs" at 100% — fixed (this session, part 6)
Two more real issues, not new bugs but gaps in part 5's progress reporting:
1. **ETA text updated every frame** (jittery, not a countdown). Fixed: `PointCloudImportAsync`
   gained `lastEtaUpdate`/`cachedEtaText` (UI-thread-only, untouched by the worker); the modal now
   recomputes the ETA string at most once a second and reuses the cached text otherwise. The
   progress bar's percentage still updates live — only the "~Xm Ys remaining" text is throttled.
2. **The "hang at 100%" was a real, entirely untracked third phase.** `BuildPreviewFromCache`
   reads the WHOLE cache back off disk (every leaf, even though only a fraction of each leaf's
   points are kept for the sample) — a genuine full pass with real cost — but had no progress
   counters at all, so the bar reported phases 1+2 as "100%" while the worker kept working through
   this third phase with zero visible feedback. Fixed: `previewLeavesRead`/`previewTotalLeaves`
   atomics added to the job struct, `BuildPreviewFromCache` now takes optional progress pointers
   and increments per leaf, and the modal blends three phases (35% stream + 45% split + 20%
   preview) with a third phase label ("Sampling preview..."). Not a deadlock — the worker was
   genuinely still running; it was invisible, which is what read as a hang.
- Full suite still green after this round: `GoSurveyTests` 1193/8,954,028, `GoSurveySnapTests`
  327/3,040. Not yet re-verified against the real 7.8GB file with THIS round of fixes.

## 11. Still not done
- REQ-100 profile (e) benchmark instrument (`BENCH POINTCLOUD`) against the real 7.8GB file.
- Ribbon UI point size/LOD/colour-scheme controls, PTS/PTX/LAS/LAZ readers — separate future
  increments per `file-format-specs.md` §6.
- `CadBlocks.cpp` / `CadCommands_Bench.cpp` deliberately left unmirrored (no current caller).

## 10d. Octree LOD paging in the renderer — implemented (this session, part 7)
Closes the one item part 4's log left open: the renderer no longer draws only the flat, bounded
preview everywhere. It now pages full-density leaves from the `.gscloud` cache near the camera
focus and draws them on top of the preview.
- `pointcloud::SelectLodLeaves` (`src/util/pointcloudoctree.{hpp,cpp}`) — new pure-Domain query:
  given a focus point + search radius, reuses `QueryLeavesNearPoint` for the candidate set, then
  orders nearest-first (bounds-clamped distance, same metric as the sphere test) and truncates to a
  caller budget. A wide-open (zoomed-out) view intersects many leaves; the budget is what keeps the
  result — and therefore GPU memory/upload cost — bounded regardless of radius. 3 new Catch2 tests
  (`tests/PointCloudOctreeTests.cpp`): nearest-first ordering, budget truncation keeps the closest
  leaf, empty selection when nothing is in range.
- `ViewportRenderer::PointCloudGpuEntry` (`ViewportRenderer.hpp`) gained `diskCache`
  (`pointcloudcache::OpenCache`, opened lazily once per cloud — `diskCacheTried` distinguishes "not
  attempted" from "open failed" so a missing/corrupt cache isn't retried every frame) and
  `leafGpu` (currently-resident LOD leaves, one small VAO/VBO each).
- Per frame, per cloud with `hasOutOfCoreCache()`: `SelectLodLeaves` runs against
  `cam.targetX/Y/Z` (the pan target — the same point the existing view-anchor logic already treats
  as "what the camera is looking at") with radius `max(halfWd, halfHd) * 1.25` (the visible half-
  extent, so the selection covers what's on screen) and a 48-leaf budget (≤ 2.4M points at the
  50,000-point leaf cap — the same order of magnitude as the existing 2M preview cap, bounded GPU
  cost). Leaves no longer wanted are evicted (GPU buffers freed); newly wanted leaves are read
  on-demand via `pointcloudcache::ReadLeafPoints` and uploaded once. Resident leaves are cheap to
  redraw every frame (just `glDrawArrays`); only the octree query and any newly-crossed leaves cost
  anything per frame.
- Leaf vertices reuse the exact vertex-build/colour-fallback logic as the preview buffer (same
  `fallbackRgba` computation, now hoisted out of the preview's `if (anchorStale)` block so both
  paths can use it) and are built relative to the same sticky `entry->anchorX/Y`, so they share the
  preview's MVP/clip-anchor uniforms already bound for that draw call — no separate transform path.
  An anchor-drift event (the same float-precision trigger that forces a preview re-upload) also
  evicts every resident leaf, since their vertex data was baked relative to the old anchor.
- Reuses `vcLineProgram_` and the same VAO layout (pos + RGBA) as every other point-cloud draw —
  no new shader.
- **Not a stride-decimation LOD** (the alternative the user was offered): instead, leaf granularity
  (a leaf is capped at 50,000 points) is itself the LOD step — near the camera, whole leaves page in
  at full density; everywhere else, the existing preview (already density-balanced across the whole
  cloud) is what's visible. This is the smaller correct change for what was asked (implementation-
  rules.md §5): it reuses `SelectLodLeaves`/`ReadLeafPoints`, which already exist, rather than adding
  a second point-decimation code path.
- Full suite green: `GoSurveyTests` 1194 cases / 8,954,039 assertions, `GoSurveySnapTests` 327
  cases / 3,040 assertions — both up from part 6 by exactly the 1 new test case / 11 new assertions
  from `SelectLodLeaves`. `gosurvey_domain`, `GoSurvey.exe`, `gosurvey_headless.exe`,
  `GsJsonDwgFixture.exe` all build clean.
- **Not yet visually confirmed** — same GL-rendering caveat as part 3 (`project_gui_hover_not_
  automatable`): needs a manual check against the real 7.8GB E57 to confirm near-camera detail
  actually appears denser when zoomed in, and that panning/zooming doesn't show popping or a stall
  from synchronous leaf reads on the render thread (leaf reads are small — ≤ 50,000 points, a few
  MB — but they are still synchronous within the frame that requests them; if that proves visible on
  the real file, moving leaf reads to a background prefetch thread would be the follow-up, not
  attempted here since it's unmeasured).

## 10e. User report: density DECREASES when zooming in — fixed (this session, part 8)
Real 7.8GB file, part 7's LOD paging: framerate fine, but "it seems like I lose detail when I zoom
in" — the opposite of the intended effect.

**Root cause**: part 7 capped the LOD selection at 48 *leaves*, not points. A leaf is physically
tiny wherever the scan is dense (that's what the 50,000-point-per-leaf split threshold does — it
keeps subdividing until each leaf is small). So 48 leaves near the camera only cover a small,
roughly fixed physical AREA regardless of zoom. At a wide/moderate zoom the flat, whole-cloud
preview (2,000,000 points spread proportionally across the entire cloud) still covered the rest of
the visible area, so the view looked reasonably dense everywhere. As the user zoomed in, the
preview's contribution collapsed toward zero — it's a fixed global sample, so a shrinking on-screen
region captures a shrinking fraction of it — and the 48-leaf LOD patch, being a roughly constant
physical area, covered a *shrinking fraction of the now-smaller viewport*, not a growing one. Net
effect: a small dense island surrounded by increasingly empty space, which reads as "less dense"
overall even though the covered area was never less than full resolution.

**Fix**: `ViewportRenderer.cpp`'s LOD block now budgets by total resident **point count**
(`kMaxLodPoints = 6,000,000`, ~168 MB of vertex data), not leaf count. `SelectLodLeaves` is still
called with a generous leaf-count ceiling (`kMaxLodLeafCandidates = 8000`) purely to bound its own
candidate-sort cost; the actual cap is a running sum of `entry->diskCache.octree.nodes[...]
.pointCount` (available without a disk read — `pointcloudcache::Open`'s node table already carries
each leaf's point count) over the nearest-first candidates, stopping once the budget is spent. This
means the covered physical area now grows as more of it fits in the raised point budget, and —
because the search radius still tracks the viewport — a shrinking viewport needs proportionally
fewer points to cover it at full resolution, so zooming in now increases effective on-screen density
as intended rather than decreasing it.
- No test changes: `SelectLodLeaves`'s own nearest-first/budget-truncation contract is unchanged and
  still covered by part 7's 3 tests; the point-budget accumulation is renderer-side GL code, outside
  what the Catch2 suites exercise (same boundary as the rest of `ViewportRenderer.cpp`).
- Full suite still green after this fix: `GoSurveyTests` 1194/8,954,039, `GoSurveySnapTests`
  327/3,040. `GoSurvey.exe` builds clean.
- **Not yet re-verified against the real 7.8GB file with this fix** — next thing to check.

## 10f. User report (with screenshots): still losing density when zooming in, AND the far side of
a structure shows in detail while the near side stays sparse — fixed (this session, part 9)
Part 8's point-budget fix wasn't the whole story. Two screenshots on the real 7.8GB file (a 3D
orbited/isometric view, not plan) showed: zooming toward a vessel cluster, the near-facing pipework
stayed sparse while the far background (visible through gaps) rendered crisply — "wherever my
cursor is and I am zooming in, the point cloud breaks down to show me what's further away from me
as opposed to closer."

**Root cause**: part 7's `SelectLodLeaves` used an isotropic **sphere** around `cam.targetX/Y/Z`.
In an orbited 3D view, `cam.targetX/Y/Z` is the orbit PIVOT (a fixed 3D point the camera rotates
about), not a point pinned to whatever surface is actually facing the viewer — the wheel-zoom
handler (`CadUi.cpp` ~line 12958) only ever moves the pivot along the camera's screen-plane
right/up axes to keep the point under the cursor fixed laterally; it never moves the pivot along
the view (depth) axis. So the pivot's DEPTH relative to the camera stays wherever it started
(typically near the cloud's rough center), while the user zooms in on a near-facing surface that
can sit at a very different depth. A sphere search radius small enough to track lateral zoom also
bounds depth by the same amount — so as the user zoomed in, the shrinking sphere lost the
near-facing surface (too far from the pivot's depth to stay inside the sphere) while still reaching
whatever happened to sit AT the pivot's own depth, which could be the far side of the structure —
exactly the reported symptom.

**Fix**: replaced the sphere query with a genuine **cylinder** query,
`pointcloud::SelectLodLeavesInCylinder` (`src/util/pointcloudoctree.{hpp,cpp}`, replacing part 7's
`SelectLodLeaves`/`LodLeaf` outright — same one caller, so no reason to keep both): bounds only the
LATERAL (perpendicular-to-view) distance from the focus by `lateralRadius` (still tied to the
viewport, still shrinks correctly with zoom), leaves DEPTH along the view axis completely
unbounded, and returns leaves ordered nearest-to-the-eye-first (`depthAlongView` ascending) instead
of nearest-to-the-pivot. The bounds-vs-cylinder prune test is conservative (a node's center-to-axis
lateral distance minus its own bounding radius), so recursion never wrongly drops a node that might
truly intersect, at the cost of occasionally admitting a few extra candidates near the boundary —
cheap, since they only cost sort time.
- `ViewportRenderer.cpp`'s LOD block now passes `cam.ForwardWorld()` as the cylinder's axis
  direction; because candidates come back nearest-to-eye-first, the existing point-budget
  accumulation (part 8) now naturally spends its 6,000,000-point budget on the near-facing surface
  before anything behind it, rather than on whatever happened to be nearest the pivot in raw 3D.
- Tests: replaced part 7's sphere test with
  `SelectLodLeavesInCylinder: depth along the view axis is unbounded, lateral is not`
  (`tests/PointCloudOctreeTests.cpp`) — three clusters: on-axis-near, on-axis-FAR (would be excluded
  by a sphere sized for the near one, must still be reached by the cylinder), and off-axis
  (must be excluded by the lateral radius despite being closer in raw 3D distance than the far
  on-axis cluster). Asserts nearest-to-eye-first ordering and that a 1-leaf budget keeps the near
  cluster, never the far one — the exact bug being fixed. (Leaf BOUNDS centers, not point centroids,
  are what `depthAlongView` is computed from — `BuildOctree`'s child bounds are exact octant splits
  of the parent box, not tight-fit boxes around the actual points — so the test asserts ordering and
  a clear separation rather than exact depth values.)
- Full suite green: `GoSurveyTests` 1194 cases / 8,954,034 assertions, `GoSurveySnapTests` 327
  cases / 3,040 assertions. `GoSurvey.exe`, `gosurvey_domain`, `gosurvey_headless.exe`,
  `GsJsonDwgFixture.exe` all build clean.
- **Not yet re-verified against the real 7.8GB file** — next thing to check. If the near-vs-far
  problem was specific to the orbited/perspective case, a plan (top-down) view should have been
  unaffected by part 7/8's sphere bug (ForwardWorld is world −Z there, and the pivot's XY IS what
  the user is looking at even though its Z is arbitrary) — worth confirming both view modes.

## 10g. User report (with screenshot): looks solid, not like points; visible "step" artifacts —
fixed (this session, part 10)
Part 9's cylinder fix put full density on the near-facing surface, but a screenshot of a zoomed-in
vessel showed it looking like a shaded solid rather than a point cloud, with visible blocky/ring
artifacts the user described as "the renderer is incrementing in steps."

**Root cause**: parts 7-9 all used a **per-leaf budget cutoff** — leaves nearest the camera were
included at FULL density (every point) until the 6,000,000-point budget ran out, and every leaf
past that point was dropped to nothing (only the sparse global preview). Two problems follow
directly from that shape:
1. At typical close-up zoom, the near-facing surface's candidate leaves fit comfortably inside the
   budget, so they render at their full native scan density — which, for a real terrestrial/
   photogrammetry scan, is dense enough to look like a shaded solid rather than discrete points.
   This is a real REQ-171 acceptance concern the user raised directly: "I still want it to look more
   like individual points."
2. The cutoff is a hard **per-leaf** on/off: leaves are cubes, so the boundary between "leaf inside
   the budget" (full density) and "leaf just past it" (empty, preview-only) is a literal cube-shaped
   cliff in 3D — exactly what reads on screen as a blocky, stepped/terraced artifact at the edge of
   the dense region.

**Fix**: replaced the per-leaf cutoff with **uniform stride decimation across every candidate
leaf**. Every leaf the cylinder query returns now contributes points at the SAME stride (skip every
Nth point), rather than some leaves at 100% and others at 0%:
- `stride = ceil(totalCandidatePoints / kTargetLodPoints)`, computed once per frame from the
  (cheap, no-disk-read) `pointCount` already in each candidate leaf's node.
- `kTargetLodPoints` dropped from part 8's `6,000,000` to a deliberately sparse `800,000` — small
  enough that even a fully-in-budget close-up view visibly decimates, so it reads as a point cloud
  rather than filling in solid. `kMaxLodLeafCandidates` dropped from `8000` to `1500` (one VAO/VBO
  per resident leaf, so this is also a draw-call bound) — plenty at 1500, since coverage no longer
  depends on how many leaves fit a point budget.
- This removes the leaf-shaped cliff entirely: every leaf in the covered lateral radius contributes
  the same fraction of its points, so density is continuous across leaf boundaries — no full-vs-
  empty edge to read as a step.
- Detail still increases with zoom (the original ask): zooming in shrinks the lateral search radius,
  which shrinks `totalCandidatePoints` for the same `kTargetLodPoints` target, which shrinks the
  stride toward 1 — genuinely denser up close, never fully solid, matching the "distance/screen-size
  stride-based LOD" strategy the user chose back when this increment started (§9 "User test result").
- `PointCloudGpuEntry` gained `lodStride` (the stride every currently-resident leaf was uploaded
  at). A stride change now invalidates every resident leaf the same way an anchor-drift event does —
  all leaves must share one stride (uploading some leaves at an old stride and others at a new one
  would just be a milder version of the same cliff artifact), so a changed stride forces a full
  leaf-set rebuild, not just newly-entering leaves.
- No test changes: the stride math and decimated-upload loop are renderer-side GL code (same
  boundary as the rest of this increment); `SelectLodLeavesInCylinder`'s own contract (which leaves,
  in what order) is unchanged by this fix and still covered by part 9's test.
- Full suite still green: `GoSurveyTests` 1194/8,954,034, `GoSurveySnapTests` 327/3,040.
  `GoSurvey.exe` builds clean.
- **Not yet visually re-confirmed** — same GL-rendering caveat as every prior rendering part; needs
  a manual check against the real 7.8GB file to confirm the sparser, decimated look reads as
  intended and that the stride/leaf-rebuild churn while panning doesn't cost noticeable framerate.

## 10h. User report (with two orbit screenshots): confusing/inconsistent detail from different
angles, app noticeably laggier — fixed (this session, part 11)
Two screenshots of the same site, orbited to different angles moments apart: from one angle
several distinct towers were legible, from another (rotated back) the structure was hard to parse
at all — plus the app was "noticeably laggier."

**Root cause**: part 10's per-frame recomputation. `SelectLodLeavesInCylinder` was re-run EVERY
frame using `cam.ForwardWorld()` — which changes a little on every single frame of a smooth orbit
drag — as the cylinder axis. Any tiny direction change reshuffles which leaves the cylinder query
returns and shifts `totalCandidatePoints`, which (part 10) recomputes the shared decimation stride
every time; a stride change forces the ENTIRE resident leaf set to be dropped and rebuilt from disk
(`entry->lodStride != stride` had zero tolerance — even a stride of 41 vs. 42 triggered a full
rebuild). The result was up to 1,500 leaf reads-and-GPU-reuploads on very nearly every rendered
frame during any orbit or pan — real disk I/O and GPU allocation churn, not just extra CPU work,
which is what read as "noticeably laggier." The inconsistent-looking detail between the two
screenshots was the same cause from the visual side: the resident leaf set (and therefore which
parts of the structure show real density vs. fall back to the sparse global preview) was being
reshuffled continuously rather than settling, so two frames a moment apart could show genuinely
different coverage.

**Fix**: `PointCloudGpuEntry` now remembers the camera state (`lodFocusX/Y/Z`, `lodDirX/Y/Z`,
`lodLateralRadius`) its CURRENT leaf selection was chosen for, and the expensive
select-and-rebuild path only runs when the camera has moved meaningfully from that snapshot:
lateral radius changed by more than ~15% (a real zoom step), the focus moved by more than 15% of
the radius (a real pan step), or the view direction rotated more than ~3° (`dirDot < 0.9986`, an
orbit step) — not on every sub-degree frame-to-frame drift. Between re-selections, the existing
resident leaves just keep drawing every frame (cheap — no I/O), and an anchor-drift event alone (no
reselection needed) now rebuilds only the resident leaves' vertex data at the SAME stride, rather
than falling through to the full reselect-and-rebuild path.
- This is the same kind of hysteresis the existing `pcAnchorDriftBudget` already uses for the
  preview buffer, applied to the LOD leaf selection for the same reason: bound how often an
  expensive rebuild happens, not eliminate it.
- No test changes: this is purely a re-run cadence change in renderer GL code; `SelectLodLeaves
  InCylinder`'s own contract is untouched and still covered by part 9's test.
- Full suite still green: `GoSurveyTests` 1194/8,954,034, `GoSurveySnapTests` 327/3,040.
  `GoSurvey.exe` builds clean.
- **Not yet re-verified against the real 7.8GB file** — next thing to check: confirm orbiting/
  panning feels smooth again and that leaf detail now settles into a consistent, stable picture
  instead of reshuffling every frame.

## 10i. User report (five screenshots): structure appears to duplicate/offset — "6 towers instead
of 3," shifting — fixed (this session, part 12)
A sequence of orbit screenshots on the real 7.8GB file showed the plant's towers appearing to
double and shift apart ("now theres 6 and they are offset... now they're offsetting again... it's
like the renderer can't decide where to render the points at").

**Root cause, a genuine bug (not tuning)**: every point-cloud vertex buffer is stored in a
view-relative float, computed against a **sticky anchor** (`entry->anchorX/Y`) for float-precision
reasons (architecture's float-drift convention) — the draw-time model matrix (`pcModel`) translates
by `entry->anchorX/Y − panX/Y`, so it only produces the correct on-screen position for vertices
that were baked relative to THAT SAME anchor. The preview buffer respects this: it bakes against
`viewAnchorX/Y` and then immediately sets `entry->anchorX/Y = viewAnchorX/Y`, so the two always
match at upload time. Part 10/11's LOD leaf vertex builders (both the main upload loop and the
anchor-stale-only leaf rebuild added in part 11) baked leaf vertices against the transient, every-
frame-moving `viewAnchorX/Y` instead of `entry->anchorX/Y` — and never updated `entry->anchorX/Y`
themselves. Since leaf reselection (part 11's hysteresis) and the preview's own anchor update now
fire on independent, uncorrelated cadences, the anchor a given leaf's vertices were baked against
could drift arbitrarily far from `entry->anchorX/Y` by the time it's drawn — rendering that leaf's
points offset from the preview (and from other leaves selected at a different moment) by exactly
that drift. That is precisely "duplicate, offset" structure: two (or more) copies of the same
geometry, each correct in isolation, drawn through a transform calibrated for a different baked
origin.
- Fixed by baking every leaf vertex (both upload sites) against `entry->anchorX/Y` — the SAME
  sticky anchor the preview buffer and the shared `pcModel` transform already use — instead of
  `viewAnchorX/Y`. This is a one-line-per-site change (the anchor argument to
  `WorldToViewRelativeFloat`), not a design change: leaves and the preview now always share one
  coordinate origin, so nothing can drift apart regardless of how often each is rebuilt.
- No test changes: this is a coordinate-frame bug in renderer GL vertex-build code, outside what
  the Catch2 suites exercise for this feature.
- Full suite still green: `GoSurveyTests` 1194/8,954,034, `GoSurveySnapTests` 327/3,040.
  `GoSurvey.exe` builds clean.
- **Not yet visually re-confirmed** — next thing to check: orbit around the real file and confirm
  the structure now reads as one consistent, non-duplicated point set from every angle.
- **Related report, same session**: user also reports FPS drops while orbiting — being investigated
  next (part 13), likely a feedback loop in part 11's movement-based hysteresis (a slower frame
  makes the same mouse speed produce a bigger per-frame camera delta, which crosses the reselect
  threshold more often, which costs more time, compounding).

## 10j. User report: FPS drops while orbiting — fixed (this session, part 13)
Confirmed suspicion from part 12's log: a feedback loop in part 11's purely movement-based
hysteresis. A fast orbit drag crosses the ~3° direction threshold on nearly every frame; each
reselect costs real time (up to 1,500 candidate leaves' worth of query + disk reads for any that
changed); a frame that takes longer makes the SAME mouse speed produce a BIGGER per-frame camera
delta (more wall-clock time elapsed between polls), which crosses the movement threshold even more
readily — each slow frame makes the next one likelier to also reselect, compounding into a stall.

**Fix**: added a wall-clock floor on top of the existing movement thresholds —
`PointCloudGpuEntry::lodLastReselectTime` (`std::chrono::steady_clock`) — so a reselect now requires
BOTH "moved enough" (part 11's existing test, unchanged) AND "at least 200ms since the last
reselect" (the first selection is exempt from the clock — it always runs). This bounds reselect
frequency to at most 5/second by wall-clock time regardless of frame rate or how fast the user
orbits, which is what breaks the feedback loop: a slow frame can no longer make the NEXT reselect
happen sooner, because the clock (not the frame) is what gates it.
- No test changes: purely a render-cadence change in GL code, outside what the Catch2 suites
  exercise for this feature.
- Full suite still green: `GoSurveyTests` 1194/8,954,034, `GoSurveySnapTests` 327/3,040.
  `GoSurvey.exe` builds clean.
- **Not yet re-verified against the real 7.8GB file** — next thing to check: confirm orbiting no
  longer drops frames, and that detail still updates promptly once a drag settles (the 200ms floor
  should be short enough to feel immediate once the camera stops moving).

## 12. Log
- 2026-09-17 (part 1): Xerces-C + libE57Format vendored and built from source. `CadPointCloud`
  entity, octree builder, and E57 reader implemented with tests.
- 2026-09-17 (part 2): full entity lifecycle wiring (select/erase/undo/extents/layer-visibility/
  persistence/DXF-DWG-exclusion-log) plus the `POINTCLOUDATTACH` command. Full suite green.
- 2026-09-17 (part 3): flat (no-LOD) viewport render path added, reusing the existing
  vertex-colour line shader.
- 2026-09-17 (user test): confirmed on the real 7.8GB E57 — renders, but <5fps and an incomplete
  cloud (OOM). User chose: build full out-of-core streaming now, distance/screen-size stride LOD
  next.
- 2026-09-17 (part 4): `.gscloud` out-of-core cache built and wired into import + persistence.
  Bounds memory and caps rendered points regardless of source size. Full suite green (1518 total
  cases). LOD paging itself (using the octree for near/far detail) still pending — next increment.
- 2026-09-17 (part 5): background import (`PointCloudImportAsync`) + centered progress-bar UI.
- 2026-09-17 (part 6): real isLeaf() octree bug fixed (empty-octant-0 misclassified as leaf),
  bulk-IO speedup, ETA-jitter and untracked-preview-phase progress-bar fixes.
- 2026-09-18 (part 7): octree LOD paging in the renderer — `pointcloud::SelectLodLeaves` +
  per-cloud resident-leaf GPU cache page in full-density `.gscloud` leaves near the camera focus,
  on top of the existing flat preview. Closes the last item TASK-270 §11 had listed short of the
  REQ-100 profile (e) benchmark and the deferred ribbon UI/other-format-reader increments.
- 2026-09-18 (part 8): user report — density visibly DECREASED when zooming in on the real 7.8GB
  file. Root cause: part 7's 48-*leaf* LOD cap covered a roughly fixed physical area regardless of
  zoom, so it covered a shrinking fraction of the viewport as the user zoomed in, while the flat
  preview's contribution collapsed toward zero over the same shrinking area. Fixed by budgeting the
  LOD selection by total *point* count (6,000,000) instead of leaf count. Full suite still green.
- 2026-09-18 (part 9): user report with screenshots — still losing density on zoom, AND the far
  side of a structure rendered crisply while the near (camera-facing) side stayed sparse. Root
  cause: the sphere query centered on the orbit pivot conflated lateral extent (should shrink with
  zoom) with depth extent (should not — the pivot's depth along the view axis has nothing to do
  with which surface faces the camera). Replaced with `SelectLodLeavesInCylinder`: lateral-only
  radius, unbounded depth, nearest-to-the-eye-first ordering. Full suite still green.
- 2026-09-18 (part 10): user report with screenshot — near-facing detail now looked like a shaded
  SOLID, not a point cloud, with blocky "stepping" artifacts. Root cause: the per-leaf budget cutoff
  rendered in-budget leaves at 100% native density and out-of-budget leaves at 0% — full density
  reads as solid up close, and the cube-shaped full/empty boundary between adjacent leaves reads as
  a stepped cliff. Replaced with uniform stride decimation shared by every candidate leaf (no more
  per-leaf on/off), target dropped to a deliberately sparse 800,000 points. Full suite still green.
- 2026-09-18 (part 11): user report with two orbit screenshots — detail looked inconsistent between
  angles moments apart, and the app was noticeably laggier. Root cause: the LOD leaf selection
  (query + stride + resident-set rebuild) was re-run every single frame, and `cam.ForwardWorld()`
  drifts a little every frame during a smooth orbit — reshuffling which leaves are resident, and
  forcing a full disk-read-and-reupload of up to 1,500 leaves, on very nearly every frame. Fixed with
  camera-movement hysteresis (same idea as the existing anchor-drift budget): re-select only once
  the focus/radius/direction have moved meaningfully, not every frame. Full suite still green.
