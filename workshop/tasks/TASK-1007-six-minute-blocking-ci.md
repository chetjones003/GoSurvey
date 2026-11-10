# TASK-1007 — Keep full blocking CI under six minutes

## Request and authority

The user requires total GitHub-hosted CI below six minutes while preserving all
blocking build, test, coverage, and analysis checks. Do not delete, skip, or
make tests optional to meet the target.

Authority: accepted REQ-437 (required full Windows build and CTest, changed-line
clang-tidy, and fail-closed blocking MSVC `/analyze`). REQ-205 governs CI/build
performance tooling. All existing docs-only classification behavior remains.

## Acceptance criteria

- Full build and every currently registered CTest test continue to run for
  non-docs-only PRs.
- Each discovered Catch2 case and Qt test function passes exactly once; the
  existing verifier remains a blocking prerequisite of `full / build and test`.
- A failure in any build/test shard fails the historical required status.
- Measure the wall time from GitHub Actions and iterate until the blocking
  validation workflow finishes in under six minutes.
- Keep clang-tidy, MSVC analysis, security, and other required checks blocking.

## Plan and architecture check

Implement a matrix of isolated GitHub-hosted Windows workers. Configure 12 Qt
shards and 4 Catch2 shards, run one CTest group per worker, and run every
remaining CTest registration on a separate integration worker. Each worker
builds all targets locally, preserving a clean and reproducible build. Upload
the per-shard test evidence; the existing coverage verifier runs in the
aggregate job whose name stays `full / build and test` for branch protection.
The aggregate check must fail if any matrix worker fails or if coverage is
missing, duplicated, skipped, or incomplete.

Files: `.github/workflows/full-validation.yml`,
`.github/actions/release-validation/action.yml`, this task record.

Test approach: statically validate workflow/action syntax and artifact paths;
then use the blocking hosted workflow's full CTest coverage audit and reported
job durations as end-to-end evidence. Preserve all source cases and analyzer
checks. No application runtime or test assertion changes.

Architectural boundary: CI scheduling and action inputs only. No production
behavior, test semantics, dependency, or branch-protection requirement changes.
Primary risk is GitHub concurrency/resource contention; tune the matrix only
from hosted run evidence while retaining the full suite and aggregate gate.

## Implementation and verification

Implemented 12 Qt matrix members, 4 Catch2 members, and one integration member.
All build their ordinary Release targets on separate Windows workers and run a
single CTest filter. Added configurable CTest shard counts and include/exclude
filters to the shared composite action. The existing check context is now an
Ubuntu aggregate that first requires all Windows matrix jobs to succeed, then
downloads their evidence and runs the unchanged exact-coverage verifier.

Local checks: actionlint passed; YAML/action input structure passed; the
coverage verifier's eight regression tests passed; the complete local Release
build passed. A registration partition check generated the new 12/4 shard names
from CTest's existing registrations and confirmed all 202 registrations map to
exactly one matrix member (186 integration registrations). Hosted wall-time
and full evidence merge remain pending the first PR run.

### First hosted timing trial — 2026-11-10

Run 42829944444 passed: 3,190 Catch2 cases across 4 shards and 359 Qt functions
across 12 shards; every registration and the exact-coverage aggregate passed.
It did not meet the six-minute objective. Workflow elapsed time was 9m23s; the
slowest Windows shard took 8m53s. Representative Ninja traces were 286–319s,
with 85.8–91.0% cache hits. Individual Qt shards took 76–93s; the integration
worker's CTest registrations took 119s. The main avoidable cost is rebuilding
all 553 Ninja commands on every test worker, with parallel sccache writers
reporting 47–75 cache-write errors.

Follow-up: run one blocking full-target build in parallel with the test matrix.
Each test worker builds only the target(s) its selected registrations execute:
`GoSurveyQtTests`, `GoSurveyTests`, or `GoSurveyQtTests` plus
`gosurvey_headless`. All 202 CTest registrations remain in the matrix exactly
once; the separate full-target job is also required by the aggregate gate.
`full / build and test` waits for both that full build and every test worker,
then runs the same coverage audit. Actionlint, YAML/action input validation,
diff checks, and the 202-registration partition check pass for this second
iteration. Hosted timing and cache evidence for this split remain pending.

### Hosted run for 7a3440e — failed, fixed in follow-up

Run 42833741601 failed on `unit.catch.shard-1` (3 cases) and `unit.catch.shard-3`
(1 case). Cause: the GoSurveyTests-only build never ran `gosurvey_stage_resources`,
which only `GoSurveyQt` depended on, so the bundled fitting library was absent
beside the test executable (`fittingsSeen == 0`, pipe palette `di == -1`, LIBEXPORT
empty). The CS-MAP tests already had this dependency (`gosurvey_stage_csmap`). Fix:
`add_dependencies(GoSurveyTests gosurvey_stage_resources)` in `CMakeLists.txt`.
Each fitting case passes after a GoSurveyTests-only build. The full-target job and
the other shards passed in that run.

### Qt shard balancing — 2026-11-10

Measured Qt test time in run 42842150591: 967 s across 12 shards (59–112 s each),
because shards were assigned round-robin by function index. `tests/qt-test-weights.json`
records each function's measured seconds, and `tools/run-qt-test-shard.ps1` now assigns
functions longest-first to the lightest shard. The simulated plan gives every shard
about 80.6 s. Functions missing from the manifest use the default weight, so new tests
still run. Duplicate review: a scan of `tests/QtEditorTests.cpp` found no test without an
assertion, no screenshot file written by two tests, and no identical test bodies, so no
tests were removed. Build-once binary sharing was not implemented: the runtime tree is
about 1 GB, so uploading and downloading it would add to the critical path.

### sccache root cause — 2026-11-10

Diagnostic run 42850779996 (debug log per runner, 9 artifacts): the sccache GitHub backend
logged 902 reads and 339 writes rejected with `RateLimited (temporary)` (HTTP 429) on the
results cache service, plus 563 `AlreadyExists` writes for objects another job had just
stored. The entries existed; the 17 parallel jobs exceeded the per-object request rate, so
most cache hits were treated as misses and recompiled. The change replaces the per-object
backend with a local `SCCACHE_DIR` that is restored from one `actions/cache` archive and
saved by the full-target build only. Test workers restore the newest archive, so their
compiles use cached objects without per-object requests.

### Six-minute result — 2026-11-10

Warm-cache runs of commit `bac6e26` (run 42854945049):

| Attempt | Start → end | Duration | Notes |
| --- | --- | --- | --- |
| 1 (cold cache) | 16:28:53 → 16:59:45 | 30m52s | No archive yet; build 29m, 535 misses, 0 hits |
| 2 (warm) | 17:03:30 → 17:10:29 | 6m59s | Build 535/535 hits; `qt-05` started 2.5 min after the run (runner queue) |
| 3 (warm) | 19:00:31 → 19:06:25 | 5m54s | All jobs started within 16 s of the run; build 535/535 hits |

Attempt 3 is under six minutes. The attempt 2 gap was runner queue delay, not compile
or test time. Per-worker compile is now about 1 minute (was about 3.7), and the
`qt-05` shard runs about 4m15s end to end.
