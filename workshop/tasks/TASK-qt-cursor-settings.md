# TASK: Configurable CAD crosshair and pickbox (issue #1033)

## Requirement authority
REQ-450 (accepted), decision D-2026-10-21-b. Builds on REQ-449 (Settings window) and REQ-121 (pickbox while selecting).

## Files / subsystems
- `src/viewport/ViewportPickPolicy.hpp`: `ViewportCursorShapeFor` (normal vs selection, pure).
- `src/qt/EditorViewport.*`: hides the OS cursor once GL is ready and paints crosshair + pickbox.
- `src/qt/EditorDialogs.cpp`: Display > Cursor controls with live preview; Cancel restores. Snap aperture moved here from Drafting.
- `src/io/UserPrefs.cpp`: persists the pickbox size (colour, arm length, aperture already persisted).
- Tests: `ViewportPickPolicyTests`, `QtEditorTests`.

## Approach
Cursor controls write straight to `AppCommandState` (live), a snapshot taken at open / last Apply is restored on `rejected`. No new state fields.

## Architectural-boundary check
Qt UI plus a pure rule beside the existing selection-step predicates. No new dependencies.

## Completion report
See PR.
