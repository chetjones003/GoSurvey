# TASK-1012 — Preserve large paper-space DWG viewports (issue #1012)

- Type: bug fix
- Status: in progress
- Opened: 2026-10-27
- GitHub issue: #1012

## 1. Authority

- Requirement: REQ-113 (accepted), DWG import reconstructs each paper-space VIEWPORT into its matching layout's native store.
- Acceptance: a valid 36 × 48 inch paper-space viewport survives DWG save/open on the same layout; paper-space viewports wider or taller than 50 inches are not rejected by size.
- Constraints: keep the change focused on valid large viewports; do not add broader layout-chrome classification rules.

## 2. Scope and implementation plan

- Remove the import-time viewport omission that treats a viewport spanning nearly the entire sheet as layout chrome.
- Add a DWG round-trip regression using a 36 × 48 inch layout and a same-size viewport; assert the viewport remains attached to that layout with its dimensions and position preserved.
- Keep existing non-full-sheet DWG viewport persistence coverage passing.

## 3. Files / subsystems

- `src/io/LibreDwgCad.cpp` — paper-space VIEWPORT import.
- `tests/LibreDwgCadTests.cpp` — focused DWG round-trip regression.
- `workshop/tasks/TASK-1012-large-paper-viewport-dwg-import.md` — task record.

## 4. Architectural-boundary check

The importer already maps paper-space VIEWPORT entities to the owning `PaperLayout`; this change stops silently dropping a valid member of REQ-113's required entity set. It adds no new viewport type, classification policy, abstraction, dependency, or serialization format.

## 5. Verification plan

- Focused paper-space DWG import regression covering a 36 × 48 inch full-sheet viewport.
- `.\build.bat`.
- Focused Catch2 paper-space DWG import test.
- `ctest --preset ninja-release`.
- Review `README.md`, `PRIVACY.md`, `resources/wiki/`, and the public manual wiki linked from `gosurveycad.com`; record each review and update affected sources before PR creation.

## 6. Root cause and plan verification

The importer currently returns early when both viewport dimensions are at least 99% of their owning sheet dimensions. On a 36 × 48 inch sheet, a valid full-sheet viewport satisfies that condition and is discarded. REQ-113 requires paper-space VIEWPORT entities to be reconstructed into their matching layout, and the accepted issue scope expressly includes the reported 36 × 48 inch case. Removing the omission and testing exact reported geometry is the narrowest change supported by the requirement; no replacement chrome rule is introduced.

## 7. Documentation review (REQ-453)

- `README.md` — reviewed; no update needed. The fix restores documented DWG exchange behavior and does not change the user workflow.
- `PRIVACY.md` — reviewed; no update needed; no information handling changes.
- `resources/wiki/` — reviewed the in-app Paper Space and Layouts and Import and Export guidance; no update needed because its statements remain accurate.
- Public GoSurvey manual wiki — reviewed the Paper Space and Layouts and Import and Export pages linked from `gosurveycad.com`; no update needed because this restores the existing general viewport persistence behavior without introducing a new user-facing capability.

## 8. Implementation and verification results

- Acceptance satisfied: a paper-space VIEWPORT is no longer omitted solely because it covers nearly all of its owning sheet. The regression creates a 36 × 48 inch layout and viewport, strips GoSurvey's trailer so the native DWG import path is exercised, and asserts layout association, position, and dimensions.
- Original reproduction: create a 36 × 48 inch viewport on a layout, save as DWG, reopen. Before the fix, the importer returned early for a viewport matching at least 99% of both sheet dimensions, so the viewport was missing. After the fix, the focused test reopens the native DWG and finds the viewport on `Large Sheet` with its 36 × 48 inch size and (0,0) position.
- `.\build.bat` — PASS; Release build completed (`Built (release)`).
- `.\build\.cmake\release\tools\GoSurveyTests\GoSurveyTests.exe "[issue1012]"` — PASS; 13 assertions in 1 test case.
- `ctest --preset ninja-release` — PARTIAL: 189/191 tests passed. `qt.editor.shard-2` failed in `qtDrawingTemplateWorkflowDiscoversCreatesCancelsAndPreservesFailure` because `SaveDrawingDocument(...)` returned false at `tests/QtEditorTests.cpp:5864`. `qt.editor.shard-4` timed out after 300 seconds in `addDrawingGuidedWorkflowConvertsReviewsConflictsAndCopiesIntoProject`. All other targets passed, including both unit shards, remaining Qt shards, all headless tests, output layout, deployment, and parity. These failures are outside the changed DWG importer and test; no unrelated Qt behavior was modified.
- `git diff --check` — PASS.

## 9. Remaining risks

- The legacy layout-chrome viewport may now be imported as a paper-space viewport. The accepted scope requires reconstructing VIEWPORT entities and explicitly disallows inventing a wider chrome classification policy here; no replacement filter was added.
- The two unrelated Qt failures mean the full suite is not green on this run.
- AutoCAD-authored 36 × 48 inch sample input was not available; the regression uses GoSurvey's DWG writer, strips its private trailer, and validates the real LibreDWG entity import path.
