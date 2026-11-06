# TASK-release-ci-parity — Share release validation and surface failures

- Type:    feature
- Status:  submitted
- Opened:  2026-11-05
- Owner:   Codex

## 1. Authority
- Goal:         Build/Platform quality (no product goal applies).
- Requirements: REQ-504 (accepted by the user on 2026-11-05).
- Constraints:  REQ-202 beta-push/stable-manual triggers and package/publish behavior; REQ-437 required PR checks; preserve analysis and security jobs.
- Acceptance:
  - PRs targeting beta/master and beta/master release runs use one shared release build and full CTest validation path.
  - Existing static analysis, security, and packaging responsibilities remain in their workflows.
  - A failed beta/master release opens one labeled issue with branch, failed job/step details, and workflow/log links; reruns reuse the same issue.
  - Successful release runs create no issue.
- Owning subsystem: `.github/workflows/` and `.github/actions/` (CI orchestration).

## 2. Scope
- In scope: factor shared release build/test validation; add idempotent release failure issue reporting.
- Out of scope: changing C++ tests, suppressing/retrying test failures, changing what CI checks, changing beta-push/stable-manual triggers or package/publish behavior, or configuring personal email/Slack notifications.
- Smallest change: one composite build/test action called by both workflows and a release failure-report job keyed by workflow run ID.

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — the CI action has two present-day call sites (PR validation and release validation); no runtime architecture or dependency changes.

## 4. Questions
None. The user approved the feature request and selected labeled GitHub issues as the notification route.

## 5. Assumptions
None.

## 6. Plan
- Approach: move common hosted Windows/MSVC/Qt/Mesa setup, CMake Release configure/build, and full CTest invocation into a composite action. Match PR and release build/test settings while allowing release version metadata and PR coverage/performance evidence to remain caller-owned. Add an `always()` failure job scoped to beta/master that queries run jobs/steps and creates or updates one issue identified by a run-ID marker.
- Files/functions to touch: `.github/actions/release-validation/action.yml` (new), `.github/workflows/full-validation.yml`, `.github/workflows/release.yml`, `.github/scripts/report_release_failure.ps1` (new), `spec/requirements.md`, this task log.
- Test approach: happy path = inspect resolved workflow/action inputs and confirm the action invocation and run-id marker behavior; failure mode = inspect failed and skipped job handling and issue deduplication path. Do not run product tests; workflow-level checks may be performed only if available without triggering a workflow or publishing.
- Steps:
  - [x] Record accepted requirement and verification plan.
  - [x] Implement the shared CI action and caller updates.
  - [x] Implement idempotent release failure issue reporting.
  - [x] Review workflow diffs and run non-executing syntax/format checks if available.
  - [x] Record verification findings and outcome.

## Verification plan review
- Architectural impact: CI-only; the composite action has two present-day call sites and introduces no product/runtime dependency or ownership change.
- Risks: PR and release inputs can silently diverge unless CMake options, test shards, parallelism, Mesa runtime staging, and cleanup all live in the shared action; issue creation must work when `build` fails and `package` is skipped; reruns must resolve the existing issue by run identity.
- Required checks: static YAML parsing of both workflows and the composite action; PowerShell parser check for the issue reporter; review that failed build/package results reach the alert job and that successful runs do not; inspect the action callers for matching settings. Product tests are outside scope.
- Implementation: shared composite action called from both existing workflows; separate release-only issue reporting job and PowerShell helper; preserve existing CTest coverage/performance evidence, analysis/security jobs, and package/publish jobs.
- Verdict: **APPROVE** under REQ-504 as approved by the user.

## 7. Workflow-specific notes
- Feature: request fully specified and approved; no application tests-first work applies.

## 8. Implementation log
- 2026-11-05 — User approved the shared validation and issue alert scope. On the current beta base, confirmed the PR path also prepares the hosted Qt display and beta release version metadata is resolved by the caller; both are accounted for in the shared action.
- 2026-11-05 — Added `.github/actions/release-validation/action.yml` and called it from PR full-validation and the beta/stable release build. Both use the same CMake options, full build, 6 Qt editor shards, hosted CTest parallelism 8, Mesa runtime and hosted-display setup/cleanup, and JUnit output. Kept PR-only shard coverage/performance evidence, independent analysis/security jobs, and all beta-push/stable-manual package/publish behavior.
- 2026-11-05 — Added a beta/master release failure job and `report_release_failure.ps1`; it creates an issue labeled `bug` + `needs-triage`, mentions the repository owner, includes failed job/step and log links, and updates/reopens the issue keyed by run ID on reruns.
- 2026-11-05 — Pre-PR product documentation review: `README.md` reviewed; it documents project branch rules and local tests but not hosted release-check wiring, so no update. `PRIVACY.md` reviewed; CI validation does not change product data handling, so no update. `resources/wiki/` reviewed; it is the in-app product manual and contains no CI guidance, so no update. The public wiki URL from `CLAUDE.md` redirects to the repository; the `gosurveycad.com` page was not accessible through the available fetcher. No product documentation is affected by this CI-only change, so no external update is needed.
- 2026-11-05 — PR follow-up: corrected CTest's JUnit destination to an absolute path under `GITHUB_WORKSPACE`. Enabled PR-Agent inline review findings and automatic committable `/improve` suggestions; maintainer `/improve` comments are accepted. This exposes GitHub's apply/resolve controls without enabling AI thread resolution. PR-Agent settings follow its [GitHub Action configuration](https://docs.pr-agent.ai/installation/github/) and [suggestion configuration](https://docs.pr-agent.ai/usage-guide/configuration_options/).
- 2026-11-05 — Runner follow-up: pinned every job in `full-validation.yml` to `windows-latest`, removed the dormant self-hosted route and runner-specific initialization/cache steps, and made the shared release-validation action hosted-only. The release workflow already uses GitHub-hosted Windows and Ubuntu runners.

## 9. Self-verification
- [ ] build-project        — n/a (CI orchestration only)
- [x] architecture-review  — PASS; CI-only composite action, two concrete callers, no runtime layer/dependency change
- [x] code-review          — PASS by manual review; shared setup is single-source, report path is run-ID idempotent and branch-scoped
- [ ] dependency-audit     — n/a (no new dependencies)
- [ ] performance-review   — n/a
- [x] testing              — PASS for static workflow validation; YAML parsing and caller/action parity assertions pass. PowerShell parser passes. Product tests were not run.

## 10. Verification result
- Submitted:  2026-11-05
- Verdict:    PASS
- Findings:   none

## 11. Outcome
- Requirements satisfied: REQ-504 (shared build/test action and run-scoped failure issue alert implemented)
- Tests added:            none
- Refactors:              none
- Docs updated:           `spec/requirements.md`, this task log
- Done:                   2026-11-05
