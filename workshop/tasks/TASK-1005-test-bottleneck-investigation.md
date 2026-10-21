# TASK-1005: Investigate test bottlenecks

## Scope, authority and verified plan

User request: investigate the test bottlenecks after the laptop benchmark passed.
REQ-205 covers iteration performance; REQ-437 retains full blocking build/CTest
validation. ADR-002/031 keep domain/headless testing separate from UI/GL code.
Read-only investigation: extract existing timings, trace expensive paths, model
scheduling, and identify safe follow-up experiments. No production/test/workflow
implementation, coverage removal or new test run is required for this report.
Plan verification: APPROVE for evidence analysis within these boundaries.

## Evidence and reproducibility

- Source revision: 5eb7fcf34998ffb91f26d9e97c2680b3575c632a.
- Laptop run 39679252637, attempt 2, job 119507211035: full CTest 148.649s,
  188/188 registrations passed, no failures or skips; six CTest workers.
- Raw evidence: `build/artifacts/laptop-39679252637-attempt-2/`. Full Catch2 output
  exists in `.cmake/release/Testing/Temporary/LastTest.log`; JUnit's stdout is
  truncated to 1024 bytes. The raw log is needed for reliable hotspot analysis.
- Analysis command: `python build/artifacts/test-bottleneck-investigation/analyze.py`.
  It validates all 211 unique Qt functions against source, reads 3527 Catch2
  timing rows and confirms 2542 cases / 9,213,033 assertions. Nested Catch2 section
  durations overlap; summing every timing row would overcount.
- The developer workstation's existing Qt binary lists only 209 functions, so it
  was not used for timing this revision. No tests were executed during analysis.
- Comparison baseline: hosted e939f75 attempt 2, run 39651530957, full CTest
  216.89s, largest Qt shard 216.84s, Catch2 213.51s. Hardware, software renderer,
  revision, and worker count differ, so this is not a controlled GPU comparison.

## Findings

### 1. Current critical path is Qt, and four-way balancing has little headroom

Qt registrations take 135.049, 146.123, 147.215 and 142.693 seconds concurrently.
The Qt-reported suite times are 133.029, 144.134, 145.251 and 140.736s;
launch/report overhead is about two seconds per shard. All 211 function durations
sum to 563.130s. Ideal four-way balancing has a 140.782s lower bound, only 4.469s
below the slowest current Qt suite. Balancing alone cannot meet the overall gate.

The 183 other registrations sum to 111.080s. With four Qt processes and one Catch2
process, the six-worker laptop has one slot left for these smaller tests.

### 2. An offline-map test waits for an unsuitable completion condition

`tests/OnlineMapTests.cpp:430`: 13.202s. The test configures immediate retry,
checks 200 frames with a 1ms sleep per iteration, restores the network, then
fails it again and calls `Settle`.

`Settle` at line 163 waits for service `Idle()` or its ten-second deadline.
`src/platform/MapTileService.cpp:187` defines idle as no waiting requests,
in-flight requests, or results. In the failed state, controller Update drains
failed results and immediately requests missing tiles again (CadOnlineMap.cpp
lines 305–326 and 400 onward). With continuous retry, idle is the wrong success
predicate for observing the new failure notification. The helper can spend its
full deadline despite the notification already having arrived.

Safe follow-up: pump until the expected second failure message and retry evidence
arrive, retaining nonblocking-frame, retry, latch, recovery and renewed-failure
assertions. Do not reduce the timeout globally or alter the production retry
policy. Avoidable time is up to ~10s; exact post-fix timing needs measurement.
Catch2 currently finishes ~28s before Qt, so this fix alone will not shorten the
current full CTest wall time. It matters once Qt becomes faster.

### 3. Expensive editor setup is repeated throughout Qt tests

`src/qt/EditorWindow.cpp:301` constructs the complete editor, loads the default
template and bundled block library, then builds ribbon/docks/start page.
`CreateDrawingFromTemplate` at line 1604 loads the library again.
`src/commands/CadBlocks.cpp:478` enumerates and imports library DWG/DXF/SAT files;
the loader has no cache at this layer and imports into scratch before merging
definitions. Tests contain 164 direct EditorWindow declaration sites and 102
CreateDrawingFromTemplate call sites. These are lexical counts, not runtime counts.

All four first tests in their shards are among the five slowest Qt functions:
spline screenshots 12.609s, block picker 11.351s, all-format import 12.928s, import
failure/cancel 15.003s. This is consistent with cold process/window setup, but
neither library import nor ribbon construction has an isolated measured share.

Next diagnostic: time constructor stages, block loading and drawing creation in
representative existing cases, separately from test assertions and teardown.
If library decoding dominates, consider reusing parsed immutable data while
copying independent document state and handling file-change invalidation. A
product-runtime cache needs ownership/invalidation design review. Sharing one
mutable EditorWindow across unrelated tests would risk test order/state leakage.

### 4. Large cancellation fixtures and screenshot work are visible costs

- `issue916ImportFailureCancelAndReadOnlyLeaveTheDrawingUnchanged`: 15.003s;
  writes a three-million-point PTS file before testing cancellation (line 727).
- `issue916CaptureScreenshotOptions`: 9.852s; another three-million-point file
  and an unconditional 3000ms wait (lines 922–926), plus screenshot output.
- `revisionCloudScreenshotsForReview`: 12.476s; two complete editor instances,
  multiple command sequences, waits and PNG capture (line 5925).
- `issue803SplineRibbonPreviewAndSelectionScreenshots`: 12.609s; renderer
  readiness, full-window PNG captures and two 150ms waits (line 600).

There are 78 literal qWait sites and 195 grab/grabFramebuffer call sites in the
file. Lexical counts do not establish actual aggregate delay: loops multiply
calls, and work also happens while processing events. QTRY timeout limits are
not unconditional waits; they return when the condition is satisfied.

Safe follow-up: measure fixture generation, asynchronous import, frame readiness,
PNG writing and teardown separately. Use explicit readiness predicates instead
of fixed sleeps where valid. A smaller cancellation fixture needs deterministic
worker coordination so the operation cannot finish before the cancel action.
Keep cancellation, error, read-only and visual-state assertions. Screenshot
tests that check only successful PNG writing are candidates for stronger image
or state assertions, not automatically redundant coverage.

### 5. Four domain tests account for 60.5% of Catch2 time

| Test | Laptop seconds | Work covered |
|---|---:|---|
| Every ACIS Plant body matches AutoCAD | 26.943 | Decode 89 bodies; import, tessellate, compare volume/extents |
| Repeated save of two TIN surfaces | 20.941 | Import sample, build two surfaces, save 100 times |
| Offline map notification/recovery | 13.202 | Retry/nonblocking/latch/recovery checks; helper wait above |
| Placed Plant parts match AutoCAD | 11.018 | Import full drawing and compare placed solids |

Together: 72.104s of 119.162s. The two Plant tests check different contracts
(definition geometry versus placement/transforms); dropping one is not safe
deduplication. Reducing the 100-save loop weakens its stress scope unless a
smaller fixture reproduces the original bug and preserves the intended contract.

Catch2 already supports `--shard-count` and `--shard-index` in the vendored
implementation. Process sharding could preserve all cases and distribute these
heavy tests. It requires identical discovery/order/seed, separate TEMP/APPDATA,
artifact/output paths and an audit of fixed ScratchDir names. Qt sharding does
not parallelize this existing one-process Catch2 registration.

## Scheduling experiments to run next

Replay model: preserve registration order, fixed observed case costs, independent
tests and ~2s Qt launcher cost; greedy duration balancing for proposed Qt shards.
The baseline replay predicts 147.215s versus measured CTest 148.649s (1.434s
CTest overhead/start offset). Values below are planning estimates, not benchmarks.

| Qt shards | CTest slots | Modeled elapsed seconds |
|---:|---:|---:|
| 4 current | 6 current | 147.215 |
| 5 | 6 | 144.619 |
| 5 | 8 | 119.162 |
| 6 | 8 | 119.162 |
| 8 | 8 | 144.776 |

Simply adding shards can queue other long work and lose its benefit. Raising
CTest concurrency is a separate setting from Ninja/build concurrency. Extra
processes also change CPU/GPU/memory pressure and cold startup costs, which the
model cannot predict. Five and six shards have the same modeled wall time here
because unsharded Catch2 becomes the floor; they need hardware measurement.

Recommended order:

1. Replace the offline test's idle predicate with the asserted event, preserving
   its coverage. Validate the test and full suite under current concurrency.
2. Compare 4/8, 5/8 and 6/8 Qt-shard/CTest-slot configurations on the laptop,
   holding revision/compiler/fixtures constant and recording peak RAM/CPU. Keep
   Ninja at six workers for this experiment. Retain all tests as blocking.
3. Profile repeated editor/library setup and point-cloud fixture creation; fix
   only the stages shown to dominate. Retain real full-editor integration cases.
4. If Catch2 remains limiting, trial two isolated Catch2 processes and verify
   exact case coverage once, no duplicates or omissions and no temp-file races.

## Verdict and limits

Investigation complete. Confirmed a scheduling constraint and unsuitable test
wait condition; identified repeated setup/fixture work that needs phase timing.
No test was removed, skipped, modified or rerun; no CI configuration was changed.
No speedup is claimed from source inspection or the scheduling model. The overall
three-minute PR target remains unmet and needs end-to-end measurement including
build/setup and blocking analysis jobs. Local analysis evidence and JSON are under
`build/artifacts/test-bottleneck-investigation/`. No changes were pushed.

## Implementation request and verified plan (2026-10-21)

The user now authorizes fixing tests, rebenchmarking on GitHub and the laptop,
including clang-tidy/MSVC, and delivering old/new measurements in a visualizer.
BUG REPORT: full tests take 148.649s on the laptop; the offline test waits on an
unsuitable idle condition and test process scheduling limits parallelism.
Expected: preserve every assertion/case and all blocking checks while reducing
wall time. Reproduction/evidence and relevant source paths are recorded above.
The user's implementation request confirms this previously supplied report.

Plan verification APPROVE under REQ-205/437 and existing test-layer boundaries:

1. Retain the offline test's 200 frame/nonblocking checks, but wait for its new
   failure notification rather than global idle on the second outage. Replace
   the screenshot's fixed 3s delay with explicit progress-dialog visibility.
   Keep the full large cancellation fixtures and all domain regression loops.
2. Configure six Qt shards and two Catch2 processes, eight CTest workers on the
   measured runners. Isolate per-process temp/settings/report paths. Pin Catch
   discovery/order/seed, retain XML and compare executed names with discovery;
   require every Catch/Qt case exactly once before claiming a passing benchmark.
3. Share the existing clang-tidy diff runner between hosted CI and the benchmark.
   Use separate MSVC analysis build trees, the existing fail-closed selector and
   blocking /WX policy, and record source scope/base revision as well as time.
4. Preserve the old hosted/laptop evidence at 5eb7fcf. Run the same improved
   revision and analyzer base on each runner; expose cache warmth/tool versions
   and real versus estimated timings. Benchmark cleanup/coverage failures fail.
5. Validate helper success/failure paths and local focused/full tests, push the
   existing PR, monitor both runner types, and publish a clickable comparison.

Architecture review: changes stay in tests/build tooling; no new product cache,
shared mutable editor fixture or runtime ownership change. Repeated editor/block
setup remains a profiling lead, not a proven basis for changing product behavior.
No visual product surface changes. Risks: file collisions, report omissions,
test-order dependencies, runner memory contention and analyzer version drift.
Validate those explicitly; do not hide failures or remove coverage to meet time.

## Implementation and local verification (2026-10-21)

Implemented the verified plan above. Catch2 now uses two isolated processes, Qt
uses six, and long suites have explicit CTest scheduling priority. Discovery and
execution XML are compared so every case must run exactly once without skips.
Temporary files and reports are isolated by run and shard. No cases were removed.

The first local parallel run exposed an existing surface-manager test race: its
600ms delay did not guarantee background rebuild completion, allowing the next
service tick to prune the stand-in boundary IDs before the move-button assertion.
The test now waits for both surfaces to reach Current with no jobs in flight,
then waits for both manager rows to appear. All move/undo assertions remain.
The point-cloud screenshot waits for visible progress instead of sleeping 3s;
the full 3-million-point cancellation fixture is retained.

Local evidence under `build/artifacts/test-speed-validation/`:

- Native full Release build: PASS (both frontends/test/helper targets).
- Focused offline regression: 13.284s before, 3.212s after; 209 assertions pass
  on each run. The retry/latch/recovery assertions and 200-frame check remain.
- Full CTest after fixing the exposed race: PASS, 191/191 registrations, 131.05s.
- Coverage audit: PASS, 2,542 Catch2 cases over two shards and 211 Qt functions
  over six shards, each exactly once. Registration count increased from 188 to
  191 because one Catch registration became two and four Qt shards became six.
- CI script tests: 26 PASS, including MSVC selector/execution and performance
  report regression checks. Coverage-verifier tests: eight PASS, including
  missing/duplicate cases, missing shards, failed cases and skipped cases.
- Shared clang-tidy runner: real safe-code fixture passes; a changed-line
  division-by-zero diagnostic fails. Validation also caught and fixed diagnostic
  path spelling differences: line filters now accept both slash conventions.
- PowerShell syntax, actionlint on both changed workflows, and diff whitespace:
  PASS. No new dependency or product runtime change.

Review verdicts: local build/testing, code quality, architecture and dependency
checks PASS. Performance comparison remains pending actual GitHub and laptop
runs; local time is not a replacement for either requested runner measurement.
The benchmark now records base/head, clang-tidy scope/tool/version/results, and
MSVC selection in a separate analysis tree using the existing fail-closed policy.
Hosted checks remain blocking and REQ-437 keeps the explicit docs-only label gate.

Comparison baselines preserved at head 5eb7fcf:

- Hosted full-validation run 39679261357: CTest about 229s (JUnit total rounded;
  slowest Qt registration 229.30s), Catch 209.034s; build/test job 386s.
- Laptop run 39679252637 attempt 2: CTest 148.649s, Catch 119.162s;
  initial build 532.548s, cached clean rebuild 34.839s (428/428 cache hits).
- Old-head clang-tidy had no C++ diff and skipped; this is not an analyzer speed
  baseline. New nonempty C++ analysis scope must be shown explicitly, and the
  laptop's sequential benchmark stages must not be presented as the hosted
  parallel PR gate duration.

Remote verification and final visual comparison: pending after this push.

MSVC selector validation on the committed implementation: two changed test
sources selected two affected source translation units plus Qt's generated MOC
unit (three total out of 380 candidates). No full-project fallback was needed.
