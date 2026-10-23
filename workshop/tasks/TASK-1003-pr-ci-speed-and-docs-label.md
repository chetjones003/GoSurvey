# TASK-1003 — Speed up PR checks and clarify docs-only fallback

- Type:    refactor
- Status:  self-verify
- Opened:  2026-10-21
- Owner:   Codex

## 1. Authority
- Goal:         User-approved CI performance improvement; no product goal applies.
- Requirements: REQ-205 (accepted), REQ-437 (accepted).
- Constraints:  Keep all validation coverage on ordinary PRs; keep every full C++ check required and blocking while reducing runtime; remove no test unless its coverage is demonstrably redundant; docs-only skip requires the explicit `docs-only` label and fully validated documentation paths; approval remains required.
- Acceptance:   REQ-205: "The build is kept within a budget so the edit → build → test loop stays usable." REQ-437: a labeled PR skips only when every changed path qualifies; absent label, paths, incomplete listings, limit, count mismatch, or classifier failure runs CI; approval remains required.
- Owning subsystem: Repository governance and GitHub Actions (`.github/workflows/`, `.github/scripts/`).

## 2. Scope
- In scope: Reduce avoidable PR-check wall time and make docs-only classifier fallback reasons visible.
- Out of scope: Remove test coverage, change docs-only label policy, or change application behavior. User-approved scope includes separating full-validation into its own workflow while keeping every job required in branch protection.
- Smallest change: Parallelize independent clang-tidy work conservatively, skip unnecessary Qt MOC generation and the unused CTest-unreferenced fixture executable, remove redundant setup, and report classifier fallback reason.

## 3. Architectural boundary check
- [x] No — workflow configuration and local diagnostics only.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Remove docs-only label requirement or keep it? | 2026-10-21 | Keep the label requirement. |

## 5. Assumptions
- Changed-file clang-tidy invocations are independent because each reads the same compile database and line filter and emits diagnostics without modifying source/build artifacts.
  - Validate by: inspect the workflow's invocation and retain a concurrency cap of two.
- The original per-case Catch2 CTest layout could not safely run concurrently because several cases reuse fixed temporary paths (for example, `gosurvey-blockimport`). Replaced the many Catch2 processes with one sequential Catch2 suite process, and isolated each Qt shard's temp/app-data/settings/artifact paths before enabling bounded CTest parallelism.

## 6. Plan
- Approach: Keep all existing coverage unless proven redundant, batch sequential Catch2 cases to avoid process startup, shard isolated Qt cases, parallelize independent CTest work with a four-worker cap, avoid setup for unaffected source changes, and make fail-closed classification outcomes explain themselves.
- Files/functions to touch: `.github/workflows/pr-checks.yml`, `.github/scripts/classify_docs_only.py`, `CMakeLists.txt`, `cmake/GoSurveyQtTests.cmake`, `tools/run-qt-test-shard.ps1`, `tests/QtEditorTests.cpp`, `.github/scripts/select_msvc_analyze_targets.py`, `workshop/tasks/TASK-1003-pr-ci-speed-and-docs-label.md`, `ci-cd-visualizer.html`.
- Test approach: Run the focused docs-only classifier suite and the exact CI build-target list followed by full CTest; CI timing impact remains pending an Actions run.
- Steps:
  - [x] Diagnose classifier gate and identify workflow timing candidates.
  - [x] Implement bounded clang-tidy parallelism and avoid redundant setup/generation.
  - [x] Improve classifier fallback diagnostics without changing classification policy.
  - [x] Refresh the CI visualizer and statically review the diff.

## 7. Workflow-specific notes
- Workflow: PRs run the gate, security scans, full build and CTest, clang-tidy, and MSVC analysis. Full validation uses a separate workflow for visibility; every status remains required and blocks merge. Docs-only label events re-run classification in both workflows.

## 8. Implementation log
- 2026-10-21 — Confirmed REQ-437 policy: no label returns false before the changed-files API call. The workflow already includes `labeled` and `unlabeled` pull request events, so label changes re-run classification.
- 2026-10-21 — Found duplicate Qt setup steps in build/test and MSVC analysis; unconditional Qt test MOC generation before clang-tidy; sequential per-file clang-tidy; and serial CTest invocation. sccache is configured, but prior stats showed zero requests; configured sccache in the MSVC analysis job and corrected CMP0141 setup before project() so MSVC /Z7 is selected when a compiler launcher is enabled; hosted cache hit rate remains to be measured.
- 2026-10-21 — Initial CTest safety review found shared fixed temp paths, so naive parallel execution was rejected. Follow-up audit measured the whole Catch2 suite in one process and found that sequential in-process execution passes all 2,542 Catch cases while avoiding per-case process startup. Headless transcript outputs are per-test and passed at `-j4`.
- 2026-10-21 — Follow-up CI review compared the older `master` workflow (which did not run CTest) with this beta-based task branch (which does). Kept both test and headless targets enabled, and changed the build command to request only `GoSurveyQt`, `GoSurveyTests`, `gosurvey_headless`, and `GoSurveyQtTests`; `GsJsonDwgFixture` has no CTest consumers.
- 2026-10-21 — Local validation on MSVC 19.51 and Qt 6.8.3: configure passed; all four selected build targets built; all nine docs-only classifier tests passed; CTest passed all 2,726 tests in 551.41 seconds. CI wall-time impact is still pending an Actions run.
- 2026-10-21 — Coverage audit found no safely removable tests: Catch2 cases carry distinct requirement/regression names, headless transcripts assert command-level behavior, and Qt tests cover UI/persistence/rendering paths. No line/branch or mutation-coverage report is configured, so overlap in feature names is not evidence for deletion. No tests were removed.
- 2026-10-21 — Replaced 2,542 discovered Catch2 CTest launches with one sequential `unit.catch` entry; the full runner passed all 2,542 cases in one process (65.97 seconds versus 105.72 seconds summed CTest history). Kept every Catch assertion.
- 2026-10-21 — Split all 208 Qt test functions into four runtime-discovered shards. Each shard gets a unique temp directory, APPDATA directory, QSettings application name, and artifact directory. All four shards passed in parallel at `-j4` in 116.30 seconds. The right-click GUI test now uses a 1,000 ms quick-click threshold to tolerate scheduler jitter; the exact timer boundary remains covered by `CadRightClickHoldIsMenu` unit tests.
- 2026-10-21 — Full local CTest passed all 188 registrations (same Catch2/headless/Qt and integration coverage) in 164.19 seconds with `ctest --preset ninja-release --parallel 4`. This is CTest time only; complete PR workflow timing still needs a hosted Actions run.

## 9. Self-verification
- [x] build-project — PASS (selected CI targets built locally)
- [x] architecture-review — n/a
- [x] code-review — static diff review; no coverage removed and classifier remains fail-closed
- [x] dependency-audit — n/a
- [ ] performance-review — CTest locally 164.19 seconds; hosted PR timing pending
- [x] testing — classifier suite 9/9; Catch2 2,542/2,542; Qt 208/208 functions; headless 180/180; full CTest 188/188

## 10. Verification result
- Submitted: pending
- Verdict: pending
- Findings: pending

## 11. Outcome
- Requirements satisfied: REQ-205, REQ-437 (hosted timing pending)
- Tests added: none; existing tests were retained
- Refactors: CTest batching and isolated Qt sharding
- Docs updated: `ci-cd-visualizer.html`
- Done: pending

- 2026-10-21 — User selected the fast-gate architecture after reviewing the latest hosted timings. Added `full-validation.yml` for the full build/test and static-analysis jobs; retained docs-only fail-closed classification and both security scans in required `pr-checks.yml`. The required gate runs patch whitespace checks and Python syntax compilation.
- 2026-10-21 — Queried live branch protection: `master` and `beta` require build/test, clang-tidy, Gitleaks, and Trivy; MSVC analysis was not in the required context list. Updated both branches to require `PR gate`, retaining both security contexts and the existing review requirement. Verified the GitHub Actions app ID is bound to all three required checks.

- 2026-10-21 � User clarified that all checks must continue to block. Reinstated required full-validation status contexts for master and beta; fast-gate architecture is superseded. Added changed-C/C++ path detection so clang-tidy skips the entire Windows setup for PRs without C/C++ changes, and enabled sccache for the MSVC analysis job.

- 2026-10-21 � Re-ran full CTest at eight workers: all 188 registrations passed in 107.94 seconds (previous local four-worker result was 164.19 seconds). Updated full-validation concurrency to eight; hosted runtime remains unmeasured.

- 2026-10-21 � User clarified that MSVC `/analyze` must remain blocking but should analyze only the PR diff. Added a selector that analyzes changed translation units plus all consumers of changed headers; if an include graph is ambiguous or analyzer-affecting CMake/source-generation inputs changed, it fails closed to the full analyzed set. Added 11 selector regression cases.


## 12. Follow-up: measured cache and duplicate-build defects (2026-10-21)
- User authorization: implement all proposed fixes and monitor PR #1043.
- Bug report: the completed aeee012e build spent 9m16s compiling, with 313/430 sccache requests bypassing caching (/Fp:311, /Yc:2); clang-tidy spent about 10m10s building app dependencies to generate test MOC, then about 4m51s analyzing. Expected: unchanged objects are cacheable and metadata preparation does not compile the app. Reproduce via full-validation on a PR changing QtEditorTests.cpp.
- Authority: accepted REQ-205, REQ-324, REQ-437; retain all tests, blocking checks, and explicit docs-only labeling.
- Plan: disable PCH only in the cached CI build; invoke configured Qt autogen directly; reuse a cached Qt kit without Python/aQt setup on a hit; record runner capacity, Ninja overlap, total cache effectiveness, per-case test times, and clang-tidy profiles. Preserve parallel limits until measured. Resolve the beta decision-log conflict without dropping either decision.
- Validation: cache denominator and Ninja multi-output/overlap regressions; fresh-tree Qt metadata generation without application objects; local PCH-off full target build and CTest; YAML/PowerShell syntax and failure-path checks. Hosted cold/warm measurements follow after push.
- Plan verification: PASS for scope/architecture/coverage. These are build-only changes with no new product dependency or API; performance acceptance remains pending measured CI results.

- Implementation: PCH OFF in the cached CI build; direct cmake_autogen for Qt test metadata; shared cached Qt setup in all three full-validation Windows jobs; runner capacity/Ninja running-count output; final cache denominator, Ninja overlap and slow-test summaries; retained Ninja/CTest/Qt/clang-tidy profiling artifacts. Test worker limits and coverage are unchanged.
- Conflict: merged current beta; retained the ribbon-caption decision and renamed this branch's superseded async-gate decision to D-2026-10-21-e to avoid the duplicate identifier.
- Regression evidence: fresh Qt metadata configure/generation succeeded with QtEditorTests.moc present and ZERO project object files. Full PCH-off MSVC build passed: 428 sccache requests, 428 cacheable, zero bypasses/errors (cold cache). Cache-report tests cover the prior false 97-percent denominator and multi-output Ninja overlap.
- Validation: 24 Python checks PASS; actionlint PASS; YAML and all 24 embedded PowerShell scripts parsed; cached Qt environment accepted a complete kit and rejected an incomplete kit. Full CTest PASS: 188/188 in 110.42s; four Qt JUnit files contain 217 case records. An initial Qt reporter argument-binding failure was corrected with an explicit JUnitLog parameter, then the COMPLETE suite was rerun successfully.
- Verification: build/code-quality/architecture/dependency/testing PASS for this change. Hosted cold/warm performance acceptance remains pending; first cache fill is expected to be slower than the steady-state repeat. No compiler replacement, test removal, or weakened required check was introduced.
- Final reporting failure-path check: incomplete XML from an aborted test is explicitly reported without discarding other timing evidence. All 25 Python checks pass. Clang-tidy profiling CLI produced a JSON profile in the local smoke check.

## 13. Hosted follow-up: rejected Qt cache path (2026-10-21)
- First complete 6b29f772 run 39645945103: all required checks passed; 188/188 CTest registrations passed. Job times: build/test 16m01s (build 11m11s, tests 198.55s), clang-tidy 7m09s (actual analysis 278.64s), MSVC analysis 3m37s. There was also about 5m43s of queue delay after PR metadata events replaced canceled runs.
- Compiler reuse was 182/428 (42.5%), 246 misses, ZERO uncacheable requests. Ninja measured six overlapping commands on four logical CPUs and 16 GiB RAM. Thus compilation is parallel; the PCH bypass defect is fixed, but cold misses remain expensive.
- Bug report: the Qt action never saves its kit. Expected: the first successful install saves a reusable cache; reproduce with full-validation after 6b29f772. Evidence: all three post-Qt steps warn: Invalid pattern 'D:\a\GoSurvey\GoSurvey/../Qt'. Relative pathing '.' and '..' is not allowed.
- Root cause: actions/cache receives an absolute path that still contains a parent traversal segment. This is a direct logged configuration defect, so speculative hypothesis ranking is unnecessary.
- Plan verification APPROVE (REQ-205/REQ-437): compute the canonical sibling Qt path before invoking the cache action, and reuse the same resolved parent for installation and validation. No scope, coverage, dependency, or blocking-status change.
- Regression: build/artifacts/ci-hosted/check_qt_cache_path.py executes the resolver and cache input from the actual composite action with a fixture path containing spaces. It failed before the fix on the parent-relative cache pattern and passed afterward. The 25 existing Python tests pass; patch whitespace passes. The upcoming hosted run must confirm cache saving, followed by a warm repeat.

## 14. Windows self-hosted setup script (user request, 2026-10-21)
- Feature request: provide a repeatable PowerShell environment installer for a self-hosted Windows build machine. User considered Contabo, then a spare laptop and confirmed Windows 11. Goal: install the existing native toolchain and explain connecting the machine to GitHub without an inbound public port.
- Authority: REQ-205 permits CI caching and build tooling; preserve native MSVC/CMake/Ninja, Qt 6.8.3, all tests and required checks under REQ-437. The user authorized the setup script; no new infrastructure purchase or live runner registration is authorized or necessary to provide it.
- Acceptance: installs missing Windows toolchain components and Qt; checks failures and reports restart requirements; offers a read-only plan; provides a normal-user interactive runner launcher and clear registration instructions; keeps caches outside checkout cleanup. Never collect a token in chat or provision a public network listener.
- Plan: tools/setup-windows-build-runner.ps1 and a short Windows self-hosted guide. Use Windows 11's WinGet and the existing Python/aQt installer. Add no application dependency. Install tools from an administrator shell, then register/run GitHub's runner under a dedicated normal user in their own profile.
- Architecture/risk review: setup tooling only, no product subsystem or API changes. The current hidden-desktop GUI launcher explicitly starts children on winsta0, so service/session-0 suitability is unproven; use an interactive logged-in runner for initial validation. Keep hosted jobs unchanged until an isolated trusted-run routing policy and a successful hardware benchmark exist.
- Verification plan APPROVE: syntax validation and the read-only plan on Windows; mock package installation, missing-tool, partial-Qt and external-command failure paths; parse generated launchers. Full installation and performance on the spare laptop remain explicitly unverified; do not install packages on this developer machine to simulate a fresh host.
- Implementation: added tools/setup-windows-build-runner.ps1 and docs/windows-self-hosted-runner.md; linked setup from README. The installer targets the user-confirmed Windows 11 laptop, uses WinGet, installs the existing toolchain/Qt dependencies, and generates per-user environment and interactive runner launchers. No live runner, service, account, public port, workflow routing, or infrastructure purchase was provisioned.
- Validation: Windows PowerShell 5.1 parsed the installer; both generated launchers parsed; the read-only -PlanOnly path executed successfully (also with -PreventSleep); Qt action YAML parsed, actionlint passed, and the canonical cache-path regression passed after failing on the original action. Existing Python suite: 25/25. Static code-quality and architecture review PASS. Fresh-machine installation, runner registration, GUI desktop/session validation, and hardware performance remain unverified. Hosted cache fill/warm repeat remain pending; the three-minute target has NOT been achieved.
