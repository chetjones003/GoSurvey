# TASK-918 — Add docs-only PR check handling

- Type:    docs
- Status:  implement
- Opened:  2026-10-19
- Owner:   Codex

## 1. Authority
- Goal:         User-approved repository process change (2026-10-19); no existing product goal applies.
- Requirements: REQ-437 (accepted).
- Constraints:  Preserve beta's required approval; preserve all existing CI checks for ordinary PRs.
- Acceptance:   A PR with the `docs-only` label and documentation-only changed files skips build, lint, and security CI jobs, including CodeQL. A `docs-only` label on a PR containing any non-documentation file does not skip the required CI checks. At least one approving review remains required before merge.
- Owning subsystem: Repository governance and GitHub Actions (`spec/`, `.github/workflows/`).

## 2. Scope
- In scope: Define docs-only classification and acceptance in REQ-437; record the user's decision; adapt PR checks and CodeQL so validated docs-only PRs do not run CI while review approval remains required.
- Out of scope: Change application behavior, alter branch protection, or weaken approval requirements.
- Smallest change: Use a `docs-only` PR label, validate changed paths once in a shared script, and conditionally skip required CI jobs only for a validated docs-only PR.

## 3. Architectural boundary check
- [x] No — repository process and workflow configuration only; no application architecture changes.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Should docs-only PRs skip CI while retaining approval? | 2026-10-19 | Add an accepted spec requirement; docs-only PRs skip CI, but still need an approval before merge. |

## 5. Assumptions
- `docs-only` is the explicit GitHub label that marks a PR for classification.
- Documentation-only files use Markdown, MDX, reStructuredText, AsciiDoc, or `.text`; `.txt` qualifies only inside `docs/`, `spec/`, `workshop/`, or `verification/`. Both old and new names of a rename must qualify, and the API listing count must match the PR's changed-file count (with more than 3,000 changes falling back to CI).
- Existing beta branch protection's one approving review remains the approval gate; this task does not change repository settings.

## 6. Plan
- Approach: Add REQ-437 and a dated project decision, then use one shared classifier in PR checks and CodeQL to validate the label and changed paths before skipping CI.
- Files/functions to touch: `AGENTS.md`, `CLAUDE.md`, `spec/requirements.md`, `spec/project.md`, `.github/scripts/classify_docs_only.py`, `.github/workflows/pr-checks.yml`, `.github/workflows/codeql.yml`, `workshop/tasks/TASK-918-docs-only-pr-checks.md`.
- Test approach: Run focused local unit tests for qualifying docs, code files and renames, incomplete API listings, the API file cap, and API failures; no CI or application tests apply.
- Steps:
  - [x] Record this plan before implementation.
  - [x] Add and accept REQ-437 and record the user decision.
  - [x] Update PR and CodeQL workflow classification and conditional CI jobs.
  - [x] Inspect the final diff and verify the review approval rule is preserved.
  - [x] Add and run focused local classifier regression tests.

## 7. Workflow-specific notes
- Docs: Every policy statement will be cross-checked against the workflow and beta branch protection configuration.

## 8. Implementation log
- 2026-10-19 — User authorized a docs-only spec requirement, confirmed that approval remains required, and asked that the PR workflow implement the policy.
- 2026-10-19 — Added REQ-437 and D-2026-10-19-i; added docs-only label classification and CI job conditions; created the repository `docs-only` label. Beta currently requires one approving review and dismisses stale approvals.
- 2026-10-19 — Review found that `.txt` could include `CMakeLists.txt`, renames checked only the new path, and GitHub's files API caps listings at 3,000. Tightened `.txt` locations, validate both rename paths, and compare enumerated files with the PR's changed-file count; PRs over the cap run CI.
- 2026-10-19 — Follow-up review found the prior Qt screenshot instructions in `CLAUDE.md` still required inline images. Aligned them with the T3 visualizer rule and consolidated repeated API request headers.
- 2026-10-19 — Final spec review found that CodeQL still ran on docs-only PRs to `master`. Moved classification to a shared script and added the same fail-closed skip behavior to CodeQL; manual dispatch remains unaffected.
- 2026-10-19 — Added nine offline regression tests covering qualifying documentation, missing label, code files, both rename directions, `.txt` scope, incomplete listings, the API listing limit, and API failure propagation. `python .github/scripts/test_classify_docs_only.py -v` passed all nine tests. CI remains skipped as requested.

## 9. Self-verification
- [ ] build-project — n/a (no application code)
- [ ] architecture-review — n/a (no application architecture)
- [ ] code-review — pending
- [ ] dependency-audit — n/a
- [ ] performance-review — n/a
- [x] testing — workflow YAML parsed successfully; all nine focused local classifier tests passed; CI remains skipped per user direction

## 10. Verification result
- Submitted: pending
- Verdict: pending
- Findings: pending

## 11. Outcome
- Requirements satisfied: pending
- Tests added: 9 focused classifier regression tests
- Docs updated: pending
- Done: pending
