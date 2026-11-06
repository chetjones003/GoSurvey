# TASK-1142 — Generate beta release notes and What's New content

- Type:    feature
- Status:  implement
- Opened:  2026-11-05
- Owner:   Codex

## 1. Authority
- Goal:         N/A — no numbered project goal covers release automation.
- Requirements: REQ-202, REQ-336, REQ-505 (accepted)
- Constraints:  none specific; respect project CI permissions and REQ-300 dependency policy.
- Acceptance:   REQ-505 Acceptance conditions (spec/requirements.md); preserve REQ-202 stable behavior and REQ-336 committed-content review lock.
- Owning subsystem: Build/Platform (release workflow, PR metadata, generation, manifest, packaging).

## 2. Scope
- In scope: beta-only PR metadata collection, AI-generated surveyor-facing notes, generated packaged What's New section, manifest persistence and retry reuse, workflow tests.
- Out of scope: stable/master release content or publishing behavior, application runtime AI/API use, committing generated files, per-release review, source code or diff analysis.
- Smallest change: add a fail-closed beta workflow generation step and deterministic workflow helper tests, using existing OpenAI service credentials and additive beta manifest fields.

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — workflow-local processing and additive beta-only manifest fields are specified by REQ-505 and ADR-079; no app API/dependency changes.
    - [ ] Yes → STOP.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | How should beta notes be authored? | 2026-11-05 | AI rewrites merged PR titles/descriptions for surveyors. |
| Q2 | Should generated notes be reviewed before publishing? | 2026-11-05 | Publish automatically; applies only to generated release-run content. |
| Q3 | Where should generated content live? | 2026-11-05 | Generate in the release run; do not commit to beta. |
| Q4 | Which AI model? | 2026-11-05 | Use the lower-cost `gpt-6-luna` model through OpenAI Responses API; strict JSON Schema, low reasoning effort. |

## 5. Assumptions
None. User choices and existing release behavior are recorded in D-2026-11-05-p, REQ-505, and ADR-079.

## 6. Plan
- Approach: derive the beta PR set from the source commit and captured master baseline; use only PR title/body with the existing AI service; validate output; stage a modified temporary What's New file; add exact output to beta manifest and release notes; reuse manifest output for same-source reruns.
- Files/functions to touch: `.github/workflows/release.yml`, release helper scripts/tests as needed, `src/update/UpdateCheck.cpp` only if manifest parsing requires explicit support, workflow docs if affected.
- Test approach: workflow/helper happy path covers beta-range selection, metadata-only prompt, output parity, authored-content/image preservation, same-commit byte-identical reuse, and stable-path isolation. Failure cases cover unavailable metadata/API, malformed/injected Markdown, and manifest write failure blocking packaging/publication. Use no application C++ tests unless client parsing must change.
- Steps:
  - [x] Add a deterministic PR metadata and source-range helper.
  - [x] Add AI generation with strict structured output and Markdown validation.
  - [x] Apply generated section to a runner-only resource copy and persist outputs to beta manifest.
  - [x] Reuse manifest outputs on reruns and leave stable publishing unchanged.
  - [x] Add workflow/helper tests for all REQ-505 acceptance cases.
  - [x] Complete release/build/review/testing verification and product documentation review before PR.

## 7. Workflow-specific notes
- Feature: user decisions are recorded; test-first for deterministic helper/validation logic.

## 8. Implementation log
- 2026-11-05: User selected `gpt-6-luna`. Official OpenAI model docs confirm Responses API and strict structured-output support; current official price listing is substantially lower than Sol. Repository secret listing confirms `OPENAI_KEY` exists; secret value was not read. Implementing direct REST call and non-publishing API smoke/full workflow validation inputs.
- 2026-11-06: Live non-publishing smoke workflow succeeded against OpenAI Responses API with `gpt-6-luna`, low reasoning effort, strict structured output, and synthetic examples. Full workflow-dispatch validation on the feature branch passed release version resolution, generated content from 36 merged beta PRs, the hosted Windows release build and all 194 CTest cases, and the package job. The run uploaded installer and beta-content artifacts only; it did not publish a release. Run: https://github.com/chetjones003/GoSurvey/actions/runs/42151754796.
- 2026-11-06: Pre-PR product documentation review: `README.md` reviewed; no change needed. `PRIVACY.md` reviewed; no change needed because CI sends only already-public merged PR titles/descriptions and no app/runtime user data. `resources/wiki/` reviewed; no release-note procedure is affected. The public wiki linked from `gosurveycad.com` was checked; its GitHub Wiki URL redirects to the repository and no accessible manual page was found. This automation does not change product instructions, so no external documentation update is needed.

## 9. Self-verification
- [x] build-project        — PASS (hosted Windows release workflow)
- [x] architecture-review  — PASS (no Workshop architectural decision)
- [x] code-review          — PASS (correctness, simplicity, ownership, readability)
- [x] dependency-audit     — PASS / n-a (no new dependency)
- [x] performance-review   — PASS / n-a (release-time only)
- [x] testing              — PASS

## 10. Verification result
- Submitted: 2026-11-05 (Specification/Verification gate)
- Verdict:    PASS — specification and proposed plan approved for Workshop; implementation not reviewed.
- Findings:   0 blocking, 0 advisory.
- Architecture: ✓ — release workflow owns this; no app runtime service or dependency is added.
- Correctness: ✓ — range, outputs, reruns, preservation, and fail-closed behavior are acceptance-tested.
- Dependencies: n-a — reuses the existing OpenAI service; no dependency manifest change.
- Performance: n-a — release-time only; no product hot path.
- Stable boundary: ✓ — stable/master flow explicitly unchanged.
- Runner boundary: ✓ — hosted-runner assignments preserved; self-hosted runners prohibited.

## 11. Outcome
- Requirements satisfied: yes; beta-only generated notes are kept out of the source tree, persisted in the rolling manifest, and reused for same-source reruns; stable publication is unchanged.
- Tests added:            12 deterministic Python helper tests; all passed. Live OpenAI smoke test and full non-publishing release build/test/package workflow passed.
- Docs updated:           spec requirements, project decision, architecture decision, and task record. Product documentation review recorded above; no product docs required changes.
- Done:                   implementation and verification complete; PR submitted separately.
