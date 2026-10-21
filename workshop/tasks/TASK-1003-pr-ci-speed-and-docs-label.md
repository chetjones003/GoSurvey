# TASK-1003 — Speed up PR checks and clarify docs-only fallback

- Type:    refactor
- Status:  self-verify
- Opened:  2026-10-21
- Owner:   Codex

## 1. Authority
- Goal:         User-approved CI performance improvement; no product goal applies.
- Requirements: REQ-205 (accepted), REQ-437 (accepted).
- Constraints:  Keep all blocking checks for ordinary PRs; docs-only skip requires the explicit `docs-only` label and fully validated documentation paths; approval remains required.
- Acceptance:   REQ-205: "The build is kept within a budget so the edit → build → test loop stays usable." REQ-437: a labeled PR skips only when every changed path qualifies; absent label, paths, incomplete listings, limit, count mismatch, or classifier failure runs CI; approval remains required.
- Owning subsystem: Repository governance and GitHub Actions (`.github/workflows/`, `.github/scripts/`).

## 2. Scope
- In scope: Reduce avoidable PR-check wall time and make docs-only classifier fallback reasons visible.
- Out of scope: Remove or weaken checks, change docs-only label policy, alter branch protection, or change application behavior.
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
- CTest parallelism was rejected after inspection found tests reuse fixed shared temporary paths (for example, `gosurvey-blockimport`), making concurrent execution unsafe without broader test isolation changes.

## 6. Plan
- Approach: Keep required coverage, parallelize only independent work with bounded concurrency, avoid setup for unaffected source changes, and make fail-closed classification outcomes explain themselves.
- Files/functions to touch: `.github/workflows/pr-checks.yml`, `.github/scripts/classify_docs_only.py`, `workshop/tasks/TASK-1003-pr-ci-speed-and-docs-label.md`, `ci-cd-visualizer.html`.
- Test approach: Run the focused docs-only classifier suite and the exact CI build-target list followed by full CTest; CI timing impact remains pending an Actions run.
- Steps:
  - [x] Diagnose classifier gate and identify workflow timing candidates.
  - [x] Implement bounded clang-tidy parallelism and avoid redundant setup/generation.
  - [x] Improve classifier fallback diagnostics without changing classification policy.
  - [x] Refresh the CI visualizer and statically review the diff.

## 7. Workflow-specific notes
- Workflow: ordinary PRs continue to run every existing blocking check. Label events already trigger classification again.

## 8. Implementation log
- 2026-10-21 — Confirmed REQ-437 policy: no label returns false before the changed-files API call. The workflow already includes `labeled` and `unlabeled` pull request events, so label changes re-run classification.
- 2026-10-21 — Found duplicate Qt setup steps in build/test and MSVC analysis; unconditional Qt test MOC generation before clang-tidy; sequential per-file clang-tidy; and serial CTest invocation. sccache is configured, but prior stats showed zero requests; no speculative cache change planned.
- 2026-10-21 — Rejected CTest parallelism because test code reuses fixed temporary paths. Kept tests serial and focused speed changes on isolated clang-tidy processes and avoidable setup work.
- 2026-10-21 — Follow-up CI review compared the older `master` workflow (which did not run CTest) with this beta-based task branch (which does). Kept both test and headless targets enabled, and changed the build command to request only `GoSurveyQt`, `GoSurveyTests`, `gosurvey_headless`, and `GoSurveyQtTests`; `GsJsonDwgFixture` has no CTest consumers.
- 2026-10-21 — Local validation on MSVC 19.51 and Qt 6.8.3: configure passed; all four selected build targets built; all nine docs-only classifier tests passed; CTest passed all 2,726 tests in 551.41 seconds. CI wall-time impact is still pending an Actions run.

## 9. Self-verification
- [x] build-project — PASS (selected CI targets built locally)
- [x] architecture-review — n/a
- [x] code-review — static diff review; no coverage removed and classifier remains fail-closed
- [x] dependency-audit — n/a
- [ ] performance-review — pending next CI run
- [x] testing — classifier suite 9/9; CTest 2,726/2,726

## 10. Verification result
- Submitted: pending
- Verdict: pending
- Findings: pending

## 11. Outcome
- Requirements satisfied: REQ-205, REQ-437 (pending verification)
- Tests added: none
- Refactors: none
- Docs updated: `ci-cd-visualizer.html`
- Done: pending
