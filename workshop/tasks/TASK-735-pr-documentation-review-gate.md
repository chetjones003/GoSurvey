# TASK-735 — Require product documentation review before pull requests (REQ-453)

- Type:    docs
- Status:  submitted (PR #1057)
- Opened:  2026-10-22
- Owner:   workshop

## 1. Authority
- Goal:         Product documentation accuracy (project goal identifiers are not defined in `spec/project.md`)
- Requirements: REQ-453 (accepted by D-2026-10-22-d)
- Constraints:  None identified
- Acceptance:   Before a pull request is created, its task log or description records inspection of all four sources and lists updates made, or states that no update was needed for each. Every source found to require an update is updated before pull request creation.
- Owning subsystem: Repository contributor workflow documentation

## 2. Scope
- In scope: Add the REQ-453 gate to `CLAUDE.md` and `AGENTS.md`.
- Out of scope: Updating product documentation unrelated to this governance change; adding automation.
- Smallest change: Add a single authoritative checklist in `CLAUDE.md` and a concise pointer/checklist in `AGENTS.md`.

## 3. Architectural boundary check
- [x] No — documentation-only workflow rule; no code, dependency, API, or architecture change.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Add and accept a SPEC requirement first, or record an exception? | 2026-10-22 | Add and accept the requirement. |
| Q2 | Is the proposed four-source review and update gate the intended scope? | 2026-10-22 | Confirmed. |

## 5. Assumptions
- The public user manual wiki linked from gosurveycad.com is the GitHub Wiki identified in `README.md`.

## 6. Plan
- Approach: Implement REQ-453 as contributor instructions in the repository governance files.
- Files/functions to touch: `spec/requirements.md`, `spec/project.md`, `CLAUDE.md`, `AGENTS.md`, this task log.
- Test approach: Inspect the changed text and diff for all four required sources, the before-PR timing, update condition, access block, and per-source record. No executable test applies.
- Steps:
  - [x] Add accepted REQ-453 and record D-2026-10-22-d.
  - [x] Verify scope, ownership, risks, and acceptance observability.
  - [x] Add the pre-PR documentation review gate to `CLAUDE.md` and `AGENTS.md`.
  - [x] Review the final diff against REQ-453.

## 7. Workflow-specific notes
- Docs review for this change:
  - `README.md` — reviewed; no product-content update needed.
  - `PRIVACY.md` — reviewed; no privacy-content update needed.
  - In-app help wiki (`resources/wiki/Home.md`, `Getting-Started.md`) — reviewed; no user-help update needed.
  - Public manual wiki (linked by `README.md` and gosurveycad.com) — reviewed its Home page; no user-help update needed for this governance change.

## 8. Implementation log
- 2026-10-22 — REQ-453 accepted and D-2026-10-22-d recorded after user selected the SPEC requirement path and confirmed scope. The original number REQ-400 was already assigned on `beta`.
- 2026-10-22 — Verification: APPROVE. The requirement is measurable from the task log or PR description; the change stays within repository documentation ownership and introduces no architectural or runtime risks.
- 2026-10-22 — Added the gate to both governance files; checked each requirement source and completion condition in the final diff.
- 2026-10-22 — Completed REQ-453's four-source review for this change; the README, privacy policy, and both user manuals need no content change for a contributor-workflow rule.
- 2026-10-22 — Opened [PR #1057](https://github.com/chetjones003/GoSurvey/pull/1057) targeting `beta`; labeled `documentation`, `docs-only`, and `needs review`.

## 9. Self-verification
- [x] build-project — n/a (documentation only)
- [x] architecture-review — PASS (no architecture changes)
- [x] code-review — PASS (final textual diff matches REQ-453)
- [x] dependency-audit — n/a
- [x] performance-review — n/a
- [x] testing — n/a (no executable behavior changed)

## 10. Verification result
- Submitted: 2026-10-22
- Verdict: PASS
- Findings: None.

## 11. Outcome
- Requirements satisfied: REQ-453 (Acceptance met: yes)
- Tests added: none (documentation-only change)
- Refactors: none
- Docs updated: `CLAUDE.md`, `AGENTS.md`, `spec/requirements.md`, `spec/project.md`
- Done: 2026-10-22
