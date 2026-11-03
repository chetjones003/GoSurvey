# TASK — Add PR-Agent GitHub workflow

- Type: CI
- Status: self-verify
- Opened: 2026-11-03
- Owner: Codex

## 1. Authority

- Goal: Keep GoSurvey pull request validation and review reliable.
- Requirement: REQ-456 (accepted from the user's direction in this task).
- Constraints: CON-06 (smallest correct solution).
- Acceptance: PR-Agent reviews GoSurvey-originated PRs to `master` and `beta` when opened, reopened, marked ready, or updated. Fork-originated PRs do not run automatically. The repository owner can post `/review` on a PR to trigger a review. The workflow does not check out fork code or use `pull_request_target`.
- Owning subsystem: Build/Platform — GitHub Actions workflows.

## 2. Scope

- In scope: Add a PR-Agent workflow with explicit minimum review permissions and use the existing `OPENAI_KEY` repository secret.
- Out of scope: Change GoSurvey's existing CI gates, OpenAI billing, or branch protection.
- Smallest change: One workflow file and one accepted requirement entry.

## 3. Architectural boundary check

- [x] No application architecture or dependency changes; repository workflow only.

## 4. Questions

| # | Question | Asked (date) | Answer |
|---|---|---|---|
| Q1 | Should fork PRs be automatic or manually triggered? | 2026-11-03 | Automatic for GoSurvey PRs; manual `/review` comment for fork PRs. |

## 5. Plan

- Approach: Add automatic `pull_request` and manual `issue_comment` triggers. Gate automatic PR events on the head repository matching GoSurvey. Let PR-Agent handle recognized manual commands on pull requests.
- Files: `spec/requirements.md`, `.github/workflows/pr-agent.yml`, this task log.
- Verification: Inspect the final diff, confirm event gating and token permissions, parse the workflow YAML, and run `git diff --check`. No application tests apply.
- Steps:
  - [x] Record the accepted behavior as REQ-456.
  - [x] Add the workflow.
  - [x] Verify diff and workflow syntax.

## 6. Implementation log

- 2026-11-03 — Added workflow for automatic same-repository PR reviews and manual `/review` comments on PRs. Fork PRs are excluded from automatic runs; no fork code is checked out.
- 2026-11-03 — Restricted manual comment triggers to the repository owner and `/review` commands. `git diff --check` passes; Python YAML parser accepts the workflow (its YAML 1.1 mode treats the GitHub `on` key as a boolean, so the event keys were also visually checked). `actionlint` is not installed in this environment.

## 7. Verification result

- Verdict: PASS for local structure and diff checks. A live GitHub Actions run is still needed after the workflow is pushed to GoSurvey.

## 8. Pre-PR product documentation review (REQ-453)

- `README.md`: Reviewed; no product-facing setup or usage change, no update needed.
- `PRIVACY.md`: Reviewed; this change only sends PR diff/context to the configured OpenAI API through PR-Agent, so no change to GoSurvey app/site data handling; no update needed.
- `resources/wiki/`: Reviewed for affected product help content; no update needed.
- Public manual linked from `gosurveycad.com`: Checked the current link from README (`https://gosurveycad.com/docs/`) and the previously documented GitHub Wiki URL. The public site URL was unavailable to the web fetcher and the GitHub Wiki redirects to the repository; neither provides a relevant manual page for this CI-only change. No update needed.
