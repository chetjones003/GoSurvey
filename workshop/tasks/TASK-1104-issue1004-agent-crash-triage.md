# TASK-1104 — Hand a crash report to an AI agent (issue #1004)

- Type: feature
- Status: submitted
- Opened: 2026-10-25
- Owner: Codex

## 1. Authority
- Goal: No explicit GOAL-NN is defined in `spec/project.md`.
- Requirements: REQ-425 (accepted), amended by D-2026-10-25-d.
- Constraints: REQ-201 (network failures remain observable to the operator); keep private artifacts out of public/package paths.
- Acceptance:
  - The admin dashboard shows each crash report's ID and a **Copy agent prompt** action containing only that ID.
  - A local read-only tool accepts one numeric report ID; performs one D1 `SELECT`; fetches only the referenced dump and matching PDB, if available; verifies PDB identity; and writes files plus a manifest under `build/artifacts/crash-triage/<id>/`.
  - Invalid IDs and mismatched PDB identities are rejected. Missing dump/PDB is reported while keeping the report text available. The tool performs no remote writes and omits unrelated install metadata.
  - `PRIVACY.md` explains that a user-requested handoff sends report text and available minidump/PDB to the selected AI provider, and that local artifacts remain until deleted.
- Owning subsystem: dashboard (site admin Worker) and developer tooling/agent skill (GoSurvey repository); D1/R2 remain the source of truth.

## 2. Scope
- In scope: admin table ID and prompt-copy action; one-report local fetch script; agent skill describing invocation and safe interpretation; privacy/spec/task documentation and tests.
- Out of scope: dashboard report editing/deletion; bulk export; public download endpoints; automatic agent invocation; automatic local artifact deletion; changing server-side 30-day dump retention.
- Smallest change: use the existing report list and private artifacts, add no new service or runtime dependency, and use the authenticated Wrangler CLI for one exact-ID read.

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
  - [x] No — D-2026-10-25-d explicitly accepts a user-initiated read-only local tool and report-ID workflow.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Should T3/Codex fetch the text, minidump, and matching PDB by report ID through a read-only tool, knowing the files go to the selected AI provider? | 2026-10-25 | Yes |

## 5. Assumptions
- No additional assumptions recorded; the fetch scope, read-only behavior, local artifact retention, and AI-provider handoff are covered by the user's decision and D-2026-10-25-d.

## 6. Plan
- Approach: add an ID and browser clipboard action to the Access-protected dashboard. A PowerShell tool uses authenticated Wrangler `SELECT`/R2 GET calls for the single ID, validates the stored dump key and PDB identity, and writes a minimal local package. An agent skill describes explicit invocation and artifact interpretation.
- Files/functions to touch: `spec/project.md`, `spec/requirements.md`, `PRIVACY.md`, `tools/triage-crash-report.ps1`, `tools/crash-triage.wrangler.toml`, `tools/test-triage-crash-report.ps1`, `.agents/skills/crash-report-triage/SKILL.md`, `workshop/tasks/TASK-1104-issue1004-agent-crash-triage.md`; site PR #2: `workers/admin/src/index.js`, `workers/admin/test.mjs`.
- Test approach: happy path verifies one ID fetch and matching artifacts; failure tests cover invalid ID, absent report/artifacts, malformed object keys, and PDB mismatch; Worker test confirms the dashboard action copies an ID-only prompt.
- Steps:
  - [x] Record D-2026-10-25-d and amend accepted REQ-425.
  - [x] Complete architectural/risk/test-plan verification.
  - [x] Implement tool, dashboard action, skill, privacy wording, and tests.
  - [x] Run focused PowerShell and Worker tests; review product documentation and both existing PRs.

## 7. Workflow-specific notes
- Feature: pre-flight answered; the user explicitly approved ID-based fetching and AI-provider sharing.

## 8. Implementation log
- 2026-10-25 — Plan approved. Wrangler `d1 execute --remote --json` and `r2 object get --remote --file` support the required scoped read workflow.
- 2026-10-25 — Added crash report ID and clipboard prompt to the admin dashboard. Added one-ID PowerShell retrieval for report text and referenced private objects, local manifest, and exact PDB identity validation. Added the agent skill and user-initiated AI sharing disclosure.
- 2026-10-25 — Product documentation review: `README.md` reviewed, no change; `PRIVACY.md` updated; `resources/wiki/` crash/troubleshooting pages reviewed, no change because this is an owner/developer workflow and D-2026-10-25-b keeps diagnostic disclosures out of the in-app troubleshooting manual; public [Troubleshooting wiki](https://github.com/chetjones003/GoSurvey/wiki/Troubleshooting) reviewed, no change for the same scope reason.
- 2026-10-25 — Verification: PowerShell helper tests PASS, including mocked one-SELECT/two-keyed-GET success, report-only case, and mismatched-PDB rejection; admin Worker `npm test` PASS; actual local PDB GUID/age parser PASS; authenticated remote `SELECT 1` PASS; authenticated absent-report lookup PASS and created no artifact directory; `git diff --check` PASS for both repositories. No app build or Worker deployment run; neither repository runtime is deployed by this task.

## 9. Self-verification
- [ ] build-project — n/a (no application build changes)
- [x] architecture-review — PASS; no new runtime layer, service, or dependency; only one-ID SELECT and keyed R2 GET operations.
- [x] code-review — PASS; validates numeric ID and stored object paths, omits install ID, verifies downloaded PDB GUID/age, and removes mismatched symbols.
- [ ] dependency-audit — n/a (no dependencies)
- [ ] performance-review — n/a
- [x] testing — PASS; focused PowerShell and admin Worker tests recorded above.

## 10. Verification result
- Submitted: 2026-10-25
- Verdict: APPROVE
- Findings: none. REQ-425 and D-2026-10-25-d define one user-triggered report-ID handoff; the task stays within the existing dashboard, D1/R2 stores, and local developer tooling. The tool performs a single-record SELECT and keyed object GETs, introduces no remote write path or new dependency, and leaves public/package artifacts unchanged.

## 11. Outcome
- Requirements satisfied: REQ-425 §11; acceptance met by the dashboard prompt, scoped local fetch, PDB identity check, missing-artifact handling, and privacy disclosure.
- Tests added: `tools/test-triage-crash-report.ps1`; site Worker assertions in `workers/admin/test.mjs`.
- Docs updated: `PRIVACY.md`, agent skill catalog and skill, SPEC and decision log, this task record.
- Done: 2026-10-25; existing PRs updated, not merged or deployed.
