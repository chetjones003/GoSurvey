# TASK-801 — Restore the CTest gate in PR checks

- Type: CI / test
- Status: needs review
- Opened: 2026-10-20
- GitHub issue: #801

## 1. Authority

- Goal: Keep the Qt-only product's build and supported workflows verified in PR CI.
- Requirement: REQ-440 (accepted; Qt-only configure/build/test/package/CI, full Release CTest suite passes).
- Constraints: REQ-200 (reproducible builds), REQ-201 (no silent failures), CON-06 (smallest correct solution).
- Acceptance: Every PR job that configures the Qt-only project installs the pinned Qt 6.8.3 MSVC kit. The Windows PR `build / test` job builds the Qt application and helpers, then runs the complete Release CTest suite. No stale exclusion list hides failures. All 2,665 tests pass locally against current `beta`.
- Owning subsystem: `.github/workflows/pr-checks.yml` and Workshop CI tracking.

## 2. Scope

- In scope: Install Qt in the PR build and `/analyze` jobs; run `ctest --preset ninja-release` after the existing full Release build; correct #801's obsolete test-failure inventory using the current-beta baseline; record the remaining `/analyze` and SignPath work as separate follow-ups.
- Out of scope: Changing application code, test expectations, `/analyze` policy, SignPath configuration, branch protection, or relaxing test coverage.

## 3. Architectural boundary check

- [x] Repository workflow only; no application architecture, dependency, or ownership changes.

## 4. Plan

- Confirmed `pr-checks.yml` configures and builds but does not run CTest; no exclusion list remains.
- Confirmed REQ-440 is the applicable accepted authority. #801's REQ-402 reference is stale and now points to an unrelated Cloudflare dashboard requirement.
- Add the complete CTest command after Build. Preserve the existing docs-only classification and separate lint/security jobs.
- Validate workflow syntax and the exact CTest command locally; review the final diff and ensure the job runs tests rather than only compiling test executables.

## 5. Implementation log

- 2026-10-20 — On current `beta` (`f9bbb992`), `.\build.bat` passed and `ctest --preset ninja-release` passed 2,665/2,665 tests in 583.80 seconds, including `qt.editor`, output layout, deployment, and parity-ledger checks. This supersedes the historical list of 18 failing tests.
- 2026-10-20 — Added `ctest --preset ninja-release` after the full build in the PR `build / test` job. The workflow YAML parses and `git diff --check` passes. No exclusion list remains in the workflow.
- 2026-10-20 — First hosted run confirmed the Qt-only PR build and `/analyze` jobs lacked Qt setup; both failed CMake configure because Qt6 6.8 was unavailable. Added the same pinned Qt 6.8.3 MSVC setup used by the clang-tidy job to both configure jobs. The failed runs also exposed that the missing Qt setup predates the CTest-step change.
- 2026-10-20 — The old #801 failures are resolved on current `beta`. Its `/analyze` and SignPath topics were split into #977 and #978. The issue's REQ-402 citation was corrected because REQ-402 now governs the Cloudflare admin dashboard.
- 2026-10-20 — PR #976 opened; hosted verification pending after Qt setup correction.

## 6. Verification result

- Verdict: PASS locally; first hosted configure exposed missing Qt setup; corrected and awaiting CI rerun.
- Evidence: full Release build passed; full CTest passed 2,665/2,665 on current `beta`; workflow YAML parsed; `git diff --check` passed.
- Remaining: confirm the new PR CI step passes after review.
