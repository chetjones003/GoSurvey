# TASK-1085 — Drawing Settings zone details show the category's first system (GitHub issue #1085)

- Type: bug
- Status: done
- Opened: 2026-10-24
- Authority: REQ-358 item 2 (Zone group read-only details describe the selected system); GitHub #1085

## 1. Authority and plan

- Intended behavior: when Drawing Settings opens, the description, projection and datum under the
  Zone group describe the drawing's own zone (for example `HARN/NB.NB`), not the first system in its
  category.
- Subsystem: Qt Drawing Settings dialog (`EditorWindow::ShowDrawingSettings`, `src/qt/EditorDialogs.cpp`).
- Cause: `populateSystems` stages the category's first system and refreshes the details; the block
  that then restores the drawing's zone selects the right dropdown entry before the
  `currentIndexChanged` handler is connected, so the details were never refreshed for it.
- Plan: call `updateDetails()` at the end of the restore block.
- Tests: add `drawingSettingsZoneDetailsDescribeTheDrawingsZoneOnOpen`, which opens the dialog on a
  drawing whose zone is `HARN/NB.NB` and checks the dropdown, code field and details label.
- Verification: build; the new test fails without the fix and passes with it; run the Qt editor
  suite.

## 2. Architectural-boundary check

- No architectural change; one UI refresh call in existing dialog code.

## 3. Delivered and verified

- Added the `updateDetails()` call to the zone-restore block.
- The new test reproduces the issue's exact wrong text (`NAD27 Nebraska State Planes, North Zone(2601), US Foot`)
  without the fix and passes with it.
