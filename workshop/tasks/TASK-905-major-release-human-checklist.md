# TASK-905 — Create the major-release human feature checklist

- Type: docs
- Status: done
- Opened: 2026-10-20
- Owner: Codex

## 1. Authority
- Goal: no numbered GOAL is defined in `spec/project.md`; the accepted purpose is stated in REQ-441.
- Requirements: REQ-441 (accepted)
- Constraints: REQ-200, REQ-201, REQ-300, CON-06
- Acceptance: REQ-441 acceptance conditions, including a per-item prerequisite, numbered steps, expected result, and result field.
- Owning subsystem: Documentation / product verification.

## 2. Scope
- In scope: a human-run, step-by-step checklist that exercises current supported GoSurvey product areas before a major release, calls out known limitations, and records run information and findings.
- Out of scope: replacing automated build/test/release gates; asserting all individual commands are tested unless listed; testing deferred or unsupported features as if shipped.
- Smallest change: one Markdown checklist under `docs/`, with source/coverage notes, detailed runnable product checks, individual tests for every command in the catalog, and a major-release gate link.

## 3. Architectural boundary check
- No new abstraction, dependency, API, ownership, global state, or data format. Documentation only.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Record the accepted requirement before authoring? | 2026-10-20 | Yes; user authorized REQ and decision entry. |
| Q2 | Include detailed per-item test steps? | 2026-10-20 | Yes; user directed each item to explain necessary test steps. |

## 5. Assumptions
- Checklist groups detailed workflows by supported product area rather than duplicating every command-reference article as a separate test case. Its source audit must make any uncovered individual commands clear.

## 6. Plan
- Approach: derive test cases from the manual navigation, workflow guides, command reference, known-limitations page, and release-critical file/data workflows; each case states setup, steps, expected result, and outcome.
- Files/functions to touch: `spec/requirements.md`, `spec/project.md`, `verification/release-checklist.md`, this task log, and new `docs/major-release-feature-checklist.md`.
- Test approach: documentation review against the source pages and REQ-441 acceptance conditions; no application tests are applicable to a Markdown deliverable.
- Verification: APPROVE — a standalone human checklist complements `verification/release-checklist.md`, stays within documentation ownership, and adds no runtime behavior. The scope limit is explicit and known limitations prevent false claims of product support.
- Steps:
  - [x] Record accepted REQ-441 and D-2026-10-20-b.
  - [x] Verify plan against the manual and release gate responsibilities.
  - [x] Author and cross-check the detailed checklist.
  - [x] Require a PASS at the major-version release gate.
  - [x] Record documentation verification and outcome.

## 7. Workflow-specific notes
- Docs: every behavior statement must be cross-checked against current documentation or supported UI/command surfaces. The command test section matches all 170 unique canonical command names in the current Command Reference category tables, with no duplicates or missing names; aliases are tested within the matching item. All linked command detail pages exist.

## 8. Implementation log
- 2026-10-20 — Accepted REQ-441 and D-2026-10-20-b after the user authorized the documentation requirement. User clarified every checklist item must contain detailed test steps.
- 2026-10-20 — Added 170 command-specific cases plus product-area and UI-only workflows. Linked the checklist as a required PASS for major-version releases.
- 2026-10-20 — Documentation source audit passed: every unique command catalog name is represented once, all command detail links resolve, `git diff --check` is clean. No application tests run; this is a Markdown-only change.

## 9. Self-verification
- [ ] build-project — n/a (documentation only)
- [x] architecture-review — PASS (no architecture change)
- [x] code-review — PASS (documentation scope and clarity; source/coverage audit recorded above)
- [ ] dependency-audit — n/a
- [ ] performance-review — n/a
- [x] testing — PASS (documentation source audit; no runtime tests applicable)

## 10. Verification result
- Submitted: 2026-10-20
- Verdict: PASS
- Findings: none. The existing Known Limitations wiki identifies itself as v0.6.0; the checklist instructs release reviewers to verify relevant entries against current requirements and the candidate, and to log stale manual entries.

## 11. Outcome
- Requirements satisfied: REQ-441 (Acceptance met: yes)
- Tests added: none (manual release checklist authored)
- Refactors: none
- Docs updated: `docs/major-release-feature-checklist.md`, `verification/release-checklist.md`, `spec/requirements.md`, `spec/project.md`
- Done: 2026-10-20
