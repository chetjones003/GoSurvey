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
- Files/functions to touch: `.github/workflows/pr-checks.yml`, `.github/scripts/classify_docs_only.py`, `CMakeLists.txt`, `cmake/GoSurveyQtTests.cmake`, `tools/run-qt-test-shard.ps1`, `tests/QtEditorTests.cpp`, `workshop/tasks/TASK-1003-pr-ci-speed-and-docs-label.md`, `ci-cd-visualizer.html`.
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
