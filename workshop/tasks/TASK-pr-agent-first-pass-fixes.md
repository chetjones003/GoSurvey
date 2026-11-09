# TASK — Cut PR-Agent review loops: review once, fix everything in one push

- Type: CI / governance
- Status: self-verify
- Opened: 2026-11-08
- Owner: Claude

## 1. Authority

- Goal: Keep GoSurvey pull request validation and review reliable (and affordable).
- Requirement: REQ-456 governance row (PR-Agent on GoSurvey-originated PRs); decision D-2026-11-08-i.
- Constraints: CON-06 (smallest correct solution).
- Acceptance: PR-Agent runs automatically once per PR (opened, reopened, ready_for_review), not on every push; the owner's `/review` and `/improve` comments still trigger it; one review pass can report more than 3 findings; agent instructions require collecting all findings and fixing them in one push.
- Owning subsystem: Build/Platform — GitHub Actions workflows; repo governance docs.

## 2. Evidence (why)

- `gh run list --workflow pr-agent.yml`: 11 runs on `feat/implement-issue-121` and `feat/view-frames-issue-999`, 10 on `feat/implement-issue-807` and `feat/paper-space-issues-batch`, 8 on two more.
- PR #1174: the persistent review was updated on 11 commits; 14 inline findings arrived 1–3 at a time, many being edge cases of the previous fix. `/improve` re-ran on most pushes and returned "No code suggestions" or repeated the review's finding (read-only guard on `VIEWFRAMEDELETE` appeared in both).
- PR-Agent default `pr_reviewer.num_max_findings = 3` (upstream `configuration.toml`) caps each pass, so the reviewer can only drip-feed.

## 3. Architectural boundary check

- [x] No application code, architecture or dependency change.

## 4. Questions

| # | Question | Asked | Answer |
|---|---|---|---|
| Q1 | When should PR Agent re-run after the first review? | 2026-11-08 | "Once, then on request" (recommended option). |

## 5. Plan / implementation log

- `.github/workflows/pr-agent.yml`: drop `synchronize` from `pull_request.types` and `pr_actions`; `auto_improve` false; `num_max_findings` 15; completeness `extra_instructions`; `temperature` 0; improve suggestions per chunk 6 (threshold stays 7).
- `CLAUDE.md` and `AGENTS.md`: pre-review self-check and fix-all protocol.
- `spec/project.md`: D-2026-11-08-i.

## 6. Verification result

- Workflow YAML parses; `git diff --check` clean. A live run is needed to confirm the settings take effect (see "How to measure" below). `temperature` is passed through to a model PR-Agent selects, and PR-Agent may omit it for models that reject it, so the cap and instructions are the load-bearing changes.
- How to measure: after the next 5 PRs, `gh run list --workflow pr-agent.yml --json headBranch` grouped by branch should show 1–3 runs per PR (down from 8–11). If first reviews miss defects that later reach `beta`, revisit.

## 7. Pre-PR product documentation review (REQ-453)

- `README.md`: Reviewed; CI/governance only, no update needed.
- `PRIVACY.md`: Reviewed; fewer PR diffs go to the OpenAI API, no change to app/site data handling; no update needed.
- `resources/wiki/`: Reviewed; no product-facing change, no update needed.
- Public manual wiki (`gosurveycad.com` / GitHub Wiki): Reviewed; CI-only change, no update needed.
