# TASK-1101: Fix crash-report review findings

## Authority

- REQ-425 §8: each crash report identifies the commit used for the build.
- REQ-425 §9: thrown and active exception details are included in crash reports.
- `spec/coding-standards.md` §12: tests are independent and do not use shared mutable global state.

## Root causes

1. `GOSURVEY_GIT_COMMIT` is read only during CMake configuration. A direct incremental `cmake --build` after the current branch ref changes can rebuild sources while retaining the old generated `Version.hpp` value.
2. `CrashReporterTests.cpp` communicates the SEH filter result through a mutable global even though the filter can write into a caller-owned buffer.
3. `DescribeCurrentException` used bare `throw;` outside a catch handler, risking recursive termination before it could inspect the active exception. It also reported only a generic label for non-standard types, contrary to the original wording of REQ-425 §9. The user chose to require names/messages only for `std::exception` types and allow a generic message for non-standard exceptions; that decision is recorded in REQ-425.
4. The live manual crash hook was not run because it may POST a report to the production endpoint. No change should trigger that external side effect without a safe local interception method.

## Plan

- Make CMake reconfigure when the current Git commit ref changes, including loose and packed-ref worktrees.
- Pass a caller-owned buffer into the SEH filter helper and remove `g_described`.
- Rethrow the captured `std::exception_ptr` inside a local try/catch; report standard exception details and the approved generic non-standard message.
- Verify the targeted crash reporter tests and build; do not send a live production report.

## Architectural boundary

Changes remain within build metadata generation and crash reporter code/tests. The only requirement adjustment is the user-approved clarification recorded in REQ-425; no dependency or architectural boundary changes.

## Verification

- `.\build.bat`: PASS, Release build completed without the earlier invalid-character warning.
- `GoSurveyTests.exe "[crash][platform]"`: PASS, 8 cases / 16 assertions.
- `ctest --preset ninja-release`: PASS, 191/191 tests, including all six Qt GUI shards and output layout.
- Manual `--gosurvey-test-crash`: not run because the hook can POST a report to the production endpoint.
