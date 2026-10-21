# TASK-803c — SPLINE drawing command in Qt (GitHub #803, REQ-443)

- Type: feature
- Status: ready for review
- Opened: 2026-10-20
- Owner: Codex

## 1. Authority
- Requirement: REQ-443 (accepted 2026-10-20, D-2026-10-20-a; renumbered after beta assigned REQ-440 to ImGui removal).
- Frontend: Qt only. ImGui has been removed from beta.

## 2. Scope
- Fit-point and control-vertex SPLINE creation, open/closed curves, edit grips, selection and transforms,
  including PDF plotting of the curve through model-space viewports.
- Save/load as native DXF and DWG SPLINE entities, render, snap, and preserve undo/redo.
- Enable the Qt ribbon button and command bar workflow.
- Wipeout (REQ-442) remains out of scope.

## 3. Files / subsystems
- `src/util/spline.*`, command/document entity state, Qt viewport/ribbon, renderer, selection/snapping,
  DXF and LibreDWG I/O, serialization and clipboard paths, invariants, CMake and tests/transcripts.

## 4. Implementation approach
- Reuse the existing rational B-spline curve representation and keep geometry math in `util/`.
- Carry spline data and attributes through every drawing lifecycle path; use adaptive tessellation for
  render, hit testing, extents and snapping.
- Preserve valid native SPLINE data on DXF/DWG import/export; reject malformed curves with clear
  diagnostics rather than silently dropping them.
- Verify each acceptance criterion and audit all entity-store copy/clear/selection/transform paths.

## 5. Verification
- Utility geometry tests, command transcript, DXF and DWG round trips, Qt ribbon/command workflow,
  renderer and selection coverage, and Qt-only build/test checks per REQ-443.
- Capture and present affected Qt screenshots for approval before commit/PR.

## 6. Architectural boundary check
- New entity type is required by the accepted spec. Geometry remains in `util/`; UI remains Qt-only;
  formats use existing DXF and LibreDWG adapters. No new dependency.

## 7. Progress
- [x] Spline mathematics, store, undo snapshots, invariant, selection/render/transform previews.
- [x] Command interaction, Qt ribbon, grips, properties, and model-space clipboard copy/paste.
- [x] Native DXF/DWG and `.gs` persistence paths; endpoint, nearest and perpendicular snap paths.
- [x] Audit document reset/import fallback, DXF/DWG empty-drawing detection, clipboard copy/paste,
      select-all/quick-select, and refusals for the out-of-scope curve commands.
- [x] Include model-space splines in PDF plotting and report malformed DXF SPLINE records in import diagnostics.
- [x] Acceptance verification and screenshots.

## 8. Completion report
- **Result:** PASS for the implemented REQ-443 scope; Wipeout remains separate under REQ-442.
- **Build:** `.\build.bat` Release passed.
- **Verification:** `ctest --preset ninja-release --output-on-failure` passed all 2,690 tests;
  `GoSurveyTests.exe "[req443]"` passed 164 assertions in 18 test cases; output-layout check passed.
- **Visual review:** live preview and selected-spline/property captures were presented in the T3
  visualizer and approved by the user on 2026-10-20. Captures are under `build/artifacts/Release/`
  and are not committed.
- **Reviews:** standards review found no remaining blocking issues. The spec review found no clear
  implementation mismatch, and called out areas without focused spline-specific tests: interactive
  Qt point picking, Escape cancellation, each transform/snap/refusal path, and importing a
  third-party DXF spline. The full suite passed; those paths should receive additional focused
  coverage in review or follow-up work. PR CI initially failed before compilation because the
  build and MSVC analysis jobs did not install Qt after beta became Qt-only; both jobs now install
  the same pinned Qt 6.8.3 MSVC kit as the existing clang-tidy job.
- **Assumptions / technical debt:** UI remains Qt-only. The focused native round trips cover an open
  Fit spline in DXF and a closed CV spline in DWG; they do not cover every combination or a
  third-party fixture. Existing repository warnings from MSVC/LibreDWG headers and Qt deprecations
  are unrelated to this task.
