# TASK-1011: Triage test and CI build runtime

## Request and authority

User request (2026-11-09): triage all tests, including Qt, and reduce CI runtime;
the user clarified that CI build time is the primary concern.

Applicable authority: REQ-205 (keep the edit/build/test loop usable), REQ-437
(full Windows build and CTest remain blocking), and each accepted requirement's
test mapping in `spec/requirements.md`. The user authorizes coverage reduction
where it is demonstrably redundant or low value. Do not silently remove the only
verification for an accepted requirement or weaken a must acceptance criterion;
such a change needs an explicit recorded SPEC decision.

## Plan

1. Inventory all current CTest registrations and their underlying suites: Catch2
   unit/snap tests, headless transcripts/fuzz smoke, Qt functions/shards, and
   build/deployment/policy checks. Map explicit REQ test references.
2. Establish a comparable current runtime baseline from available build/test
   evidence. If no current evidence exists, run the existing full suite only
   after configuring/building this worktree; distinguish CTest wall time from
   build/setup time and retain logs under `build/artifacts/`.
3. Review high-cost and duplicate-looking cases at assertion level. Classify
   every candidate as retain, consolidate, remove, or requires SPEC decision;
   preserve unique failure modes and end-to-end coverage for each accepted REQ.
4. Implement only safe, evidence-backed reductions. Record removed/consolidated
   coverage, before/after suite counts, exact tests run, and measured runtime.
5. Self-review the diff for lost unique assertions, affected REQs, and any suite
   registration accidentally omitted. Record remaining candidates requiring a
   SPEC decision rather than making an unapproved coverage change.

## Verification and boundaries

- Verify the CTest inventory and coverage mapping before and after edits.
- Run the full build and CTest suite per repository policy after changes; record
  failures explicitly and do not claim a speedup without comparable timings.
- No production code, CI blocking checks, or test-layer boundaries are changed.
  CI build configuration may use the REQ-205 PCH optimization while retaining
  the existing full PCH-off compile guard in release validation.
- No Qt UI behavior changes are planned; no screenshot comparison is applicable.

## Plan verification

APPROVE. This plan preserves all must requirements and existing blocking checks,
while allowing removal or consolidation of genuinely overlapping checks. REQ-205
and the user's request support reducing iteration cost. A test explicitly named
as sole acceptance evidence in the SPEC cannot be removed without a recorded
SPEC decision. Runtime claims require same-worktree full-suite measurements.

## Inventory and baseline (2026-11-09)

- Worktree started clean at `1fcec885` (`master`), on branch
  `test/trim-testing-bloat`.
- The first `build.bat` configured and built Release successfully (554 Ninja
  steps). There was no pre-existing configured tree or current run log.
- Current CTest inventory: **194 registrations** — two Catch2 shards, six Qt
  shards, 183 headless transcript/fuzz checks, plus output-layout, deployment,
  and parity-ledger checks. Catch2 discovery reports 3,179 test cases; Qt
  discovery reports 360 functions. The SPEC has 121 rows with explicit test
  mappings.
- Full Release CTest passed **194/194 in 225.65 s** with eight CTest workers.
  Evidence: `build/.cmake/release/Testing/Temporary/LastTest.log`, per-test
  timing in `CTestCostData.txt`, Catch2 discovery/results under
  `build/artifacts/catch/triage-20261109/`, and Qt shard logs/JUnit under
  `build/artifacts/qt-editor/triage-20261109/`.
- The prior October benchmark is not comparable: it had 211 Qt functions and
  different runner load. Current Qt shard times are 206–226 s with eight
  processes competing; Catch shards took 63–68 s. Most headless transcripts
  finish in under a second; the longest current transcript registrations are
  around 4 s. Qt is the current critical path.
- CTest's per-registration timing totals **1,230.52 s** on the verification
  run; this is an estimate of serial work, not a separately completed serial
  benchmark. It is about 20.5 minutes of accumulated test time compared with
  3.5 minutes of observed wall time when the eight workers run concurrently.
- `ctest --preset ninja-release` initially ran serially. In that run, two Catch
  shards took 49 s and 50 s, and the first Qt shard alone took 166 s before the
  run was stopped. This demonstrates a scheduling issue, not redundant tests.
  The interrupted run collided with the next run's `manual` Qt log; those
  worktree-local children were stopped, and the complete run used a unique
  `GOSURVEY_TEST_RUN_ID` and passed.

## Triage result

No test was removed. The costly cases reviewed have distinct assertions or
accepted-requirement coverage:

- Qt: `pdfCoordinateReviewAcceptsAndDeniesThroughDrawingWorkflow` (23.8 s),
  `projectHealthPackAndTurnoverDialogsAreNativeAndDismissWithoutChanges`
  (20.7 s), `cutRefusesEveryTypeTheClipboardCannotCarry` (21.9 s),
  `issue1031AmberPrimaryAccentInBothAppearances` (15.9 s), and
  `issue916ImportFailureCancelAndReadOnlyLeaveTheDrawingUnchanged` (7.4 s)
  each exercise separate workflows/failure contracts. Screenshot-producing
  cases reviewed among the hotspots also assert UI state or rendered behavior;
  their names alone do not make them disposable.
- Catch2: `ExportDwgFile survives repeated save with two TIN surfaces`
  (21.18 s), `every ACIS body the Plant fixture yields matches AutoCAD's solid`
  (20.76 s), and `opening the Plant fixture places every part where AutoCAD
  does` (9.48 s) cover repeated-save stability, imported body geometry, and
  placement/transforms respectively. The latter two are not duplicates.
- Existing dual-layer checks generally cover different contracts: domain
  tests pin pure math/data; transcripts and Qt tests verify command/UI wiring
  that the domain target cannot exercise.

There is a low-cost candidate for a separate evidence-backed review: identify
small untagged checks whose only assertions duplicate a newer accepted-REQ
regression test. The 3,201 Catch2 cases and 360 Qt functions are too large for
filename-only pruning; any deletion needs assertion-level proof and a before /
after run showing which unique contract remains. No hotspot reviewed met that
bar. No suite reduction is justified by the reviewed candidates, so coverage is
preserved while the CI compile path is optimized; test execution work itself is
unchanged.

## Change and verification

The earlier local CTest preset edit was withdrawn after the user clarified that
the problem is hosted CI. CI already invokes CTest with eight workers, so the
local preset setting would not reduce the hosted runtime.

The full local suite still passed **194/194 in 225.65 s** with `-j 8`; the exact
coverage audit passed: **3,179 Catch2 cases over two shards and 360 Qt
functions over six shards, each exactly once, with no skips**. A second local
run after the temporary preset edit passed in 209.05 s, but it is not a CI
measurement and the preset edit is not retained.

## Hosted CI evidence (run 42741341650, commit 2235ee9)

The latest completed public `full-validation` run took **28m15s overall**; the
`full / build and test` job took **27m56s**. Its uploaded timing artifact shows:

- Ninja compilation: **1,267.9 s (21m08s)** across 552 commands, peak six
  commands in parallel.
- Cache reuse: **198/534 requests (37.1%)**; 103 misses and 233 requests were
  uncacheable because they had multiple input files.
- CTest's six Qt shard registrations: **320–350 s each**; slowest shard
  `qt.editor.shard-3` was **349.6 s**. The two Catch2 shards were 100–111 s.
- The current CI workflow already runs six Qt shards, two Catch2 shards, and
  eight CTest workers on hosted runners. It sets `QT_OPENGL=desktop` and uses
  Mesa `llvmpipe` software rendering on hosted runners.
- Slow individual Qt functions include PDF coordinate review (40.5 s), the
  health-pack/turnover dialog workflow (36.5 s), and the clipboard refusal
  matrix (34.6 s). These are per-function durations from the same hosted run.

This evidence shows two separate costs: CI's end-to-end delay is dominated by
the 21-minute compile, while Qt tests add a roughly six-minute critical path.
The local eight-worker preset change was withdrawn because it could not improve
either hosted measurement. The slow Qt cases need assertion/REQ review before
removal; their names alone are not evidence of redundancy. The compile path is
the larger lever for reducing the 28-minute job.

The build dominates this CI gate, so the shared validation action now has an
explicit `use-pch` input. Blocking PR validation enables it to shorten feedback;
the release workflow retains the default PCH-off build as the CI compile guard
required by REQ-205. This does not remove tests or weaken a blocking check.

The local PCH-enabled Ninja log recorded 327.9 seconds, compared with 1,267.9
seconds in the hosted PCH-off build. They ran on different machines with
different cache conditions; treat this as direction only. YAML parsing passed,
and the configured local PCH-enabled build reported no work to do. Use the next
hosted PR run to determine the actual wall-time improvement; no CI timing claim
is complete until that run is available.

## Pre-PR product documentation review

- `README.md`: reviewed; describes the product, contribution flow, and manual.
  This CI-only build configuration change does not affect it.
- `PRIVACY.md`: reviewed; no application data handling or website privacy
  behavior changes, so no update is needed.
- `resources/wiki/`: reviewed; this is the in-app product manual and contains
  no CI build guidance affected by this change.
- Public manual wiki: confirmed `gosurveycad.com` links to
  `https://github.com/chetjones003/GoSurvey/wiki`; cloned and searched the
  current wiki. It documents product usage, not CI/build configuration, so no
  update is needed.
