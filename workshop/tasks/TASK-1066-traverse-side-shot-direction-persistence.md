# TASK-1066 — Persist side shots and direction observations in `.gstrav`

GitHub issue: #1066
Requirement authority: accepted REQ-403, especially its round-trip fidelity statement and input-field acceptance criterion. The issue's references to REQ-452 and TASK-1039 are mismatched and are not used as behavior authority.

## Scope and acceptance coverage

- Persist every input field in `TraverseData::sideShots` and `TraverseData::networkDirections` through the existing v1 `.gstrav` format.
- Continue loading existing v1 files that omit either optional collection.
- Keep solver and import behavior unchanged; this change is limited to `TraverseIo` serialization and its tests.
- Add public `Save`/`Load` round-trip coverage for all input members in each collection and a legacy-v1 compatibility case.

## Diagnosis

Reproduction: save a `TraverseData` containing a side shot or network direction, load it, and observe that the loaded collection is empty. `TraverseIo::Save` enumerated other observation collections but omitted these two; `Load` likewise never populated them. The data structures and solver/import paths already support both collections, so the loss occurs only at the persistence boundary.

## Implementation record

- `src/traverse/TraverseIo.cpp`: serialize and deserialize side-shot input fields and network-direction fields. Derived side-shot results remain recomputed and are not persisted.
- `tests/TraverseTests.cpp`: test every input member in both collections and confirm a v1 document without the optional fields remains loadable.
- Product documentation review for PR preparation: `README.md` and `PRIVACY.md` have no `.gstrav` content; `resources/wiki/Import-and-Export.md` and `resources/wiki/Traverse-Editor.md` describe Save/Load at the file-format level only, so no change is needed. The public GitHub manual wiki was not inspected in this step and must be checked before the PR is opened.

## Verification

- `.\build.bat` (Release, in this worktree): built, exit 0.
- `.\build\.cmake\release\tools\GoSurveyTests\GoSurveyTests.exe "TraverseIo*"`: all tests passed (90 assertions in 8 test cases).
- `.\build\.cmake\release\tools\GoSurveyTests\GoSurveyTests.exe "*Traverse*"`: all tests passed (374 assertions in 50 test cases).
- `.\build\.cmake\release\tools\GoSurveyTests\GoSurveyTests.exe`: all tests passed (9213204 assertions in 2561 test cases).
- Not run: `ctest --preset ninja-release` (headless transcripts) and the Qt editor suite; no visible Qt UI changed.
- Base SHA: `44e0ec21` (origin/beta at time of check).
