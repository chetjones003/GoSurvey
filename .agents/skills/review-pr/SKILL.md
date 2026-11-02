---
name: review-pr
description: Review a GitHub pull request for concrete correctness, regression, security, and requirement gaps, then publish actionable inline findings on the PR. Use when asked to review a PR or pull request and comment/post findings.
---

# Review a pull request

Review the proposed changes as a skeptical maintainer. Ground every finding in the diff, surrounding code, and repository requirements. Publish only verified, actionable defects as inline PR review comments.

For an issue-driven review that needs a full acceptance-criteria audit, also follow [review-issue](../review-issue/SKILL.md). Keep that skill's issue checklist and verdict separate from this skill's comment-posting steps.

## 1. Identify the PR and repository

Resolve the PR from the user's number, URL, or the current branch's associated PR. Use `gh` and repository metadata; do not infer the repository from a title alone.

```powershell
gh pr view <number> --json number,title,url,state,baseRefName,headRefName,headRefOid,author,body
gh pr diff <number>
```

Confirm the PR is open and record its head commit. Read the repository's `AGENTS.md`, `CLAUDE.md`, review guidance, coding standards, and relevant specification. Read linked issues and their discussion when they define expected behavior. If the PR or its diff cannot be identified, report what is missing and stop before posting.

## 2. Build evidence before judging

Inspect the complete diff, then read changed files and surrounding code. Trace affected callers, data flow, state transitions, persistence, error paths, and tests far enough to establish actual behavior. Check the PR's requirements against the implementation and look for regressions in existing behavior.

Run the repository's documented, relevant verification when practical. Do not invent commands. Record what ran and distinguish PR failures from existing or environment failures. Treat tests as evidence, not proof.

Raise a finding only when all of these are true:

- The changed code causes a concrete defect or fails a stated requirement.
- The consequence is meaningful to users, data, security, compatibility, or maintainers.
- The defect is not already prevented by validation or another code path.
- The finding can be tied to a changed line and explained with evidence.

Skip preferences, speculative risks, generic refactoring advice, and issues outside the PR's changes. Do not report an issue solely because a test is absent; report it when the missing coverage leaves a concrete requirement or defect unverified and the gap itself warrants action.

## 3. Write actionable findings

Use one comment per distinct defect. Anchor each comment to the smallest changed line range that shows the cause. Prefer a single line; use a short range only when the problem spans multiple changed lines. The line must exist in the PR diff and use the correct diff side (`RIGHT` for additions/changed head lines, `LEFT` only for removed base lines).

Each comment should state:

1. **Problem:** the specific faulty behavior.
2. **Impact:** when it occurs and what it breaks.
3. **Fix direction:** the smallest reasonable correction, when clear.

Keep comments concise and respectful. Phrase uncertain claims as questions only when evidence is genuinely incomplete; investigate further first where possible. Include a severity marker only if repository review convention requires one. Do not inflate severity.

Before posting, check whether the same defect is already covered by an open review thread or a current, resolved thread. Avoid duplicating an existing finding unless the code has changed and the defect remains.

## 4. Publish as one PR review

Post all inline findings together as a single GitHub pull-request review. Use the GitHub CLI or an available GitHub integration. For `gh api`, create a review with `POST /repos/{owner}/{repo}/pulls/{pull_number}/reviews` and a `comments` array containing each comment's `path`, `line` (or `start_line` and `line` for a range), `side`, and `body`.

Immediately before posting, re-read the PR's `headRefOid`. If the head changed since inspection, refresh the diff and re-check every finding and line location. If GitHub rejects a line because it is not part of the diff, relocate it to a valid changed line only when that remains a precise anchor; otherwise omit that comment and report why.

Use the `COMMENT` event for findings. Do not approve, request changes, merge, label, or otherwise modify the PR as part of this skill. When there are no qualifying findings, do not post an empty review; report that no actionable findings were found.

## 5. Report completion

After the GitHub response confirms publication, report:

- The PR number and link.
- The number of inline findings posted.
- A short list of finding locations and impacts, or that no actionable findings were found.
- Verification commands run and any relevant failures or limitations.

If publication fails, do not claim the findings were posted. Return the prepared findings and the posting error so they can be applied later.
