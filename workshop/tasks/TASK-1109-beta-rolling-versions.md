# TASK-1109 — Publish a rolling beta version for each changed beta head

- Type: feature / release workflow
- Status: submitted — PR #1151
- Opened: 2026-11-02
- Owner: Workshop

## 1. Authority
- Goal: M-Distribution step 2 (roadmap)
- Requirements: REQ-202 (accepted)
- Constraints: ADR-029 amended by ADR-074; REQ-200 reproducible published artifact; REQ-300 no new
  project dependency
- Acceptance:
  - A push to `beta` runs build + test and updates the rolling beta release; a push to any other
    branch runs no release job.
  - A beta build for a new source commit receives the next patch version after the higher of the
    latest stable version and previous beta version; rerunning the same commit retains its version.
  - Feature-branch dispatch, stable version gate, test publication gate, artifact parity and hash
    requirements in REQ-202 continue to hold.
- Owning subsystem: Build/Platform

## 2. Scope
- In scope: beta trigger and version resolution in `.github/workflows/release.yml`; optional CI
  version override in `CMakeLists.txt`; task/verification/spec records.
- Out of scope: changing the user's stable version, changing updater comparison behavior, publishing
  a release, or changing the stable manual release flow.
- Smallest change: one workflow, CMake's version input, and the beta manifest's source-commit field.

## 3. Architectural boundary check
- New abstraction / dependency / runtime ownership / public API / persisted application format?
  - [x] No. Workflow-derived beta version and additive release manifest provenance are specified
        by ADR-074; CMake defaults to the existing checked-in stable version.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Should a scheduled beta build advance even when beta is unchanged? | 2026-11-02 | Only when beta changes; resolved as a beta push/new source commit. |

## 5. Assumptions
None. The idempotency rule and stable baseline are recorded in D-2026-11-02-a / ADR-074.

## 6. Plan
- Approach: retain manual dispatch for master and other branches; add automatic beta pushes. Resolve
  the stable version from `master`, read the rolling beta manifest, reuse a version for the same
  source SHA, otherwise increment the patch of the higher version. Pass the computed beta version to
  CMake and the installer and record source provenance in the beta manifest.
- Files/functions: `.github/workflows/release.yml`; `CMakeLists.txt`; `spec/requirements.md`,
  `spec/architecture.md`, `spec/project.md`; verification review and this task log.
- Test approach: no test files or test commands for this workflow change. Review version scenarios
  and workflow guards; CI on the PR provides the build/test gate.
- Steps:
  - [x] Specification accepted and plan reviewed (Verification APPROVE).
  - [x] Add beta push trigger and idempotent version calculation/provenance.
  - [x] Pass dynamic beta version through CMake and package metadata.
  - [x] Review diff and record outcomes.

## 7. Workflow-specific notes
- Feature request fields were prefilled from the user's request and confirmed on 2026-11-02.
- Verification plan: `verification/reviews/TASK-beta-rolling-version-plan-review.md` (APPROVE).

## 8. Implementation log
- 2026-11-02 — Recorded REQ-202 amendment and ADR-074 after user confirmation; Verification plan
  approved before implementation.
- 2026-11-02 — Added beta push triggering, stable/rolling-manifest version resolution, same-commit
  idempotency, beta source SHA provenance, and the CMake version override. Stable releases still
  use `GOSURVEY_STABLE_VERSION` and the existing manual tag gate.
- 2026-11-02 — Reviewed README, PRIVACY.md, `resources/wiki/`, and the public Getting Started wiki.
  No product-doc changes needed: these sources already describe stable/beta channels and user
  initiated beta updates without promising a version format or release cadence.
- 2026-11-02 — `git diff --check` passed. Build and tests were not run; no CI workflow execution
  occurred in this task.
- 2026-11-02 — Opened [PR #1151](https://github.com/chetjones003/GoSurvey/pull/1151) against
  `beta`, labeled `feature`, `github_actions`, and `needs review`.

## 9. Self-verification
- [ ] build-project — not run.
- [x] architecture-review — PASS; version handling stays in Build/Platform; no runtime abstraction,
  dependency or application-data change.
- [x] code-review — PASS on static inspection; version values flow to CMake, installer and manifest;
  malformed prior version/provenance stops publication; stable gate is unchanged.
- [x] dependency-audit — n/a; no dependency changes.
- [x] performance-review — n/a; CI workflow only.
- [ ] testing — not run; live workflow scenarios remain unobserved here.

## 10. Verification result
- Submitted: 2026-11-02
- Verdict: PASS (static review; live workflow behavior not observed)
- Findings: none open. Version scenarios and GitHub Actions trigger behavior still require
  observation on workflow execution.

## 11. Outcome
- Requirements satisfied: REQ-202 implementation updated; live CI acceptance remains pending
  observation.
- Tests added: none
- Refactors: none
- Docs updated: SPEC and release task records; product-doc review found no user-manual changes needed
- Done: 2026-11-02
