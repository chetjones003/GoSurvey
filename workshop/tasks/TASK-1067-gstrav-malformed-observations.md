# TASK-1067 — Reject malformed `.gstrav` observation entries

## Requirement authority

Accepted REQ-403: a malformed or unreadable `.gstrav` file fails Load with a reported reason and leaves the current traverse unchanged. REQ-201 also requires visible failures.

## Scope and approach

- `TraverseIo::Load` and focused tests in `tests/TraverseTests.cpp`.
- Ensure type errors in JSON decoding, including malformed entries in the recently added `sideShots` and `networkDirections` arrays, return `false` with a reason.
- Preserve transactional behavior: decode into local values and only assign to caller outputs after complete success.
- No UI or format changes.

## Verification plan

- Add malformed-entry regression cases for both new arrays; verify error text and unchanged caller state.
- Run the focused traverse IO tests and the project build/test workflow as applicable.

## Architectural boundary

This stays within the existing pure serialization API and accepted failure behavior; no new abstraction or format rule is introduced.

## Implementation and verification

- `TraverseIo::Load` catches `nlohmann::json` decoding exceptions and returns `false` with the path and JSON error. Caller data is still assigned only after complete decoding succeeds.
- Regression coverage feeds a `null` item through each of `sideShots` and `networkDirections`, checks a non-empty error, and checks that the traverse and weights remain unchanged.
- Reproduction: a valid v1 JSON object with either collection containing `null` previously threw during `.value()` decoding, escaping `Load` and the editor's load-failure path. Result: both cases now return a reported load error.
- `build.bat` (Release): PASS, exit 0.
- `build/.cmake/release/tools/GoSurveyTests/GoSurveyTests.exe "[io][REQ-403]"`: PASS, 104 assertions in 9 cases.
- `build/.cmake/release/tools/GoSurveyTests/GoSurveyTests.exe`: PASS, 9,213,218 assertions in 2,562 cases.
- `git diff --check`: PASS.

## Product documentation review

- `README.md`: reviewed; no `.gstrav` loading behavior documented, no update needed.
- `PRIVACY.md`: reviewed; no file-loading behavior documented, no update needed.
- `resources/wiki/`: reviewed the traverse and import/export pages; they document user-visible Save/Load, while this fix only makes the existing failure path reliable, so no update needed.
- Public GoSurvey manual wiki: reviewed the Traverse Editor page; it does not document `.gstrav` Save/Load, so no update needed.

## Remaining risk

JSON decoding failures are reported uniformly from the existing loader. This does not add field-by-field semantic validation beyond what decoding already enforces.
