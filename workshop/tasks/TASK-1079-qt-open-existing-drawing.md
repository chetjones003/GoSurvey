# TASK-1079 — Open existing drawing without template chooser (GitHub issue #1079)

- Type: bug
- Status: done
- Opened: 2026-10-24
- Authority: accepted REQ-055 (File > Open creates and focuses a drawing tab), REQ-308 (Start is a non-document tab), REQ-424 (template chooser cancellation leaves active state unchanged); GitHub #1079

## 1. Authority and plan

- Intended behavior: opening an existing drawing creates a drawing tab directly and focuses it, without invoking the New Drawing template chooser. A failed open must not rename, replace, or remove the active tab/document.
- Subsystem: Qt editor open workflow (`src/qt/EditorWindow.cpp`) and Qt editor workflow tests (`tests/QtEditorTests.cpp`).
- Plan: change `EditorWindow::OpenDrawing` to create an empty drawing tab directly when no project is active, then load the selected drawing into it. Keep `NewDrawing` and its standalone template chooser behavior unchanged. Preserve existing project defaults when a project is active.
- Test seam: user-approved public `EditorWindow::OpenDrawing(path)`; assert a successful open adds/focuses a drawing while preserving Start, and a failed open preserves tab count and active state.
- Verification: focused Qt workflow coverage, full build, Qt editor tests, full CTest suite, output-layout check, and standards/spec code review.

## 2. Plan verification

- REQ-055 explicitly requires File > Open to show the opened drawing immediately with its tab selected when multiple tabs exist.
- REQ-308 defines Start as a non-document sentinel and requires File > Open to append drawings after it.
- REQ-424 says cancelling the chooser creates no tab and leaves the active drawing unchanged. The chooser remains the path for creating a new standalone drawing; opening an existing file is a distinct operation.
- The issue identifies a failure path where assuming `NewDrawing` created a tab can overwrite Start and later pop an unrelated tab. Direct tab creation in the existing open workflow avoids that assumption without adding an abstraction or changing document ownership.
- User approved `EditorWindow::OpenDrawing(path)` as the focused behavior-level test seam on 2026-10-24. The test observes tab count, selected index, drawing name/content, Start tab identity, and failure preservation.

## 3. Architectural-boundary check

- No architectural change proposed. The fix remains in the existing Qt editor open workflow and uses its existing tab-creation path.

## 4. Delivered and verified

- `EditorWindow::OpenDrawing` now creates a drawing tab directly, preserving the standalone New Drawing template chooser for its intended workflow.
- Added `qtOpenExistingDrawingBypassesTemplateChooserAndPreservesTabsOnFailure`, covering successful open, active-tab focus, Start-tab identity, loaded geometry, and missing-file rollback. The test failed before the fix because the chooser appeared, then passed after the fix.
- `build.bat`: passed (Release).
- Focused hidden-desktop Qt test: passed (3 assertions, 0 failures).
- Full `ctest --preset ninja-release`: passed, 191/191 tests.
- Output layout (`build.output-layout`): passed.
- Pre-PR product documentation review is still required before any pull request is opened; this implementation task does not open a PR.
