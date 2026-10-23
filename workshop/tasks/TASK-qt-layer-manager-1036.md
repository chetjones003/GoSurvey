# TASK: Redesign the Qt Layer Properties Manager (issue #1036)

## Requirement authority
REQ-451 (accepted), decision D-2026-10-22-a. Builds on REQ-082 (data grid), REQ-046 (VP columns), REQ-436 (accent).

## Files / subsystems
- `src/qt/EditorLayerManager.{hpp,cpp}` (new): `ShowLayerManager`, moved out of `EditorWindow::ShowLayers`.
- `src/qt/EditorDialogs.cpp`: `ShowLayers` becomes a thin wrapper.
- `cmake/GoSurveyQt.cmake`: new source.
- `tests/QtEditorTests.cpp`: `issue1036LayerManager*`.
- `spec/requirements.md`, `spec/project.md`: REQ-451, D-2026-10-22-a.

## Approach
Same QDialog + QTableWidget. Sorting is a display permutation of `drawingLayerTable` computed in `populate()`
(no QTableWidget sorting), and every edit resolves its record from the row's stored layer name, so the view
order cannot rewire an edit. Name column is `Stretch`; the rest are interactive. Swatches and linetype samples are
`DecorationRole` pixmaps (linetype sample uses `CadTessellateLinetypeSegmentVc`). Check boxes use one small
delegate. VP Freeze / VP Color reuse `FreezeLayerInViewport` and `SetViewportLayerColor` on `CurrentViewport`.

## Tests
`issue1036LayerManagerSortingActsOnTheShownRecord`, `issue1036LayerManagerViewportColumnsEditTheCurrentViewport`,
existing `nativeLayerManagerEditsAreUndoable`; screenshots under `build/artifacts/`.

## Architectural-boundary check
Qt UI only; no new dependencies, no domain changes.

## Completion report
See the PR.
- Technical debt: the window is still modal (`exec`), as before; "floating" means a resizable top-level dialog.
  Column order/visibility is not persisted between openings.
