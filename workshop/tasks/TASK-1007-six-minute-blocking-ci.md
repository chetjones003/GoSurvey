# TASK-1007 — Keep full blocking CI under six minutes

## Request and authority

The user requires total GitHub-hosted CI below six minutes while preserving all
blocking build, test, coverage, and analysis checks. Do not delete, skip, or
make tests optional to meet the target.

Authority: accepted REQ-437 (required full Windows build and CTest, changed-line
clang-tidy, and fail-closed blocking MSVC `/analyze`). REQ-205 governs CI/build
performance tooling. All existing docs-only classification behavior remains.

## Acceptance criteria

- Full build and every currently registered CTest test continue to run for
  non-docs-only PRs.
- Each discovered Catch2 case and Qt test function passes exactly once; the
  existing verifier remains a blocking prerequisite of `full / build and test`.
- A failure in any build/test shard fails the historical required status.
- Measure the wall time from GitHub Actions and iterate until the blocking
  validation workflow finishes in under six minutes.
- Keep clang-tidy, MSVC analysis, security, and other required checks blocking.

## Plan and architecture check

Implement a matrix of isolated GitHub-hosted Windows workers. Configure 12 Qt
shards and 4 Catch2 shards, run one CTest group per worker, and run every
remaining CTest registration on a separate integration worker. Each worker
builds all targets locally, preserving a clean and reproducible build. Upload
the per-shard test evidence; the existing coverage verifier runs in the
aggregate job whose name stays `full / build and test` for branch protection.
The aggregate check must fail if any matrix worker fails or if coverage is
missing, duplicated, skipped, or incomplete.

Files: `.github/workflows/full-validation.yml`,
`.github/actions/release-validation/action.yml`, this task record.

Test approach: statically validate workflow/action syntax and artifact paths;
then use the blocking hosted workflow's full CTest coverage audit and reported
job durations as end-to-end evidence. Preserve all source cases and analyzer
checks. No application runtime or test assertion changes.

Architectural boundary: CI scheduling and action inputs only. No production
behavior, test semantics, dependency, or branch-protection requirement changes.
Primary risk is GitHub concurrency/resource contention; tune the matrix only
from hosted run evidence while retaining the full suite and aggregate gate.

## Implementation and verification

Implemented 12 Qt matrix members, 4 Catch2 members, and one integration member.
All build their ordinary Release targets on separate Windows workers and run a
single CTest filter. Added configurable CTest shard counts and include/exclude
filters to the shared composite action. The existing check context is now an
Ubuntu aggregate that first requires all Windows matrix jobs to succeed, then
downloads their evidence and runs the unchanged exact-coverage verifier.

Local checks: actionlint passed; YAML/action input structure passed; the
coverage verifier's eight regression tests passed; the complete local Release
build passed. A registration partition check generated the new 12/4 shard names
from CTest's existing registrations and confirmed all 202 registrations map to
exactly one matrix member (186 integration registrations). Hosted wall-time
and full evidence merge remain pending the first PR run.
