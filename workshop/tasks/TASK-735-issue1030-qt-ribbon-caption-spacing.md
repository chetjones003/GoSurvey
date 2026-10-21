# TASK-735 — Qt ribbon caption bottom spacing (GitHub issue #1030)

- Type: enhancement
- Status: done
- Opened: 2026-10-21
- Authority: REQ-302 caption-spacing acceptance (D-2026-10-21-a, user-approved); GitHub #1030

## 1. Authority and plan

- Intended behavior: visible breathing room below each Qt ribbon group caption; the ribbon may grow
  slightly while command buttons keep their current size and behavior; supported widths retain no
  horizontal or vertical ribbon scrollbars.
- Subsystem: Qt ribbon UI (`src/qt/EditorRibbon.cpp`).
- Plan: give each panel caption 4 px of bottom margin and increase the expanded panel, page, and
  ribbon heights by 4 px. Preserve command button dimensions; leave the existing overflow behavior
  intact.
- Tests: add a Qt editor test checking caption bottom margin, the height increase, unchanged button
  minimum width and fixed height, and that ribbon/page geometry stays within the window at a narrow
  supported width.
- Verification: build the Qt application and run the targeted Qt editor test; run the full suite.

## 2. Plan verification

- The acceptance wording is approved and recorded in REQ-302 and D-2026-10-21-a.
- `origin/beta` contains the Qt ribbon implementation. `RibbonPage` currently uses fixed heights of
  94 px for expanded panels, 98 px for the page, and 126 px for the ribbon. Panel captions are
  `QLabel`s in `src/qt/EditorRibbon.cpp`; the panel layout has no bottom margin. Large command
  buttons have a 58 px minimum width and a fixed 73 px height; their final width follows label/font
  metrics and must not be forced to one value.
- Narrow widths already collapse panels into native menus; the change must not alter that behavior.

## 3. Architectural-boundary check

- No architectural change is proposed; this is a spacing and containing-layout adjustment in the
  existing Qt UI.

## 4. Delivered and verified

- Added a 4 px bottom margin below Qt ribbon group captions. Expanded panels, ribbon pages, and the
  ribbon grew by 4 px each; button dimensions and the panel overflow logic are unchanged.
- Added `ribbonPanelCaptionsHaveBottomSpaceWithoutResizingButtons` in `tests/QtEditorTests.cpp` to
  verify caption space, the panel/page/ribbon heights, the large button's existing 58 px minimum
  width and 73 px height, and the 850 px layout with no visible scrollbars.
- `build.bat`: passed. Focused Qt test: passed. Full CTest: **2726/2726 passed**.
- Standards review: no findings. Spec review: PASS, no findings.
