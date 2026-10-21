# TASK-996 — Home ribbon Create Surface wiring

- Scope: GitHub #996 only, on Qt beta commit `01faffc81ea6d95e5fe282fd13e07386f79eadcf`.
- Authority: issue #996's agreed acceptance seams; REQ-302, REQ-068, REQ-124,
  REQ-201, REQ-440; ADR-028, ADR-036, ADR-038 and ADR-044 (DWG persistence).
- Constraints: CON-06; existing Qt/shared workflow only, no new abstraction,
  domain behavior, serialization format, dependency, or ImGui code/tests.
- Root cause: `RibbonCatalog.hpp` marks Home/Create Ground Data/Surfaces as a
  placeholder with an empty command; `EditorRibbon.cpp` disables placeholders and
  rejects their dispatch. Survey already routes `@createSurface` successfully.
- Plan: point that catalog item at the existing `@createSurface` workflow and
  replace its placeholder tooltip. No dialog redesign or persistence changes.
- Verification of plan: PASS — existing Qt dispatch and drawing persistence
  boundaries suffice; no architectural decision or missing requirement.
- Agreed public seams: Home ribbon action opens Create Surface (cancel leaves
  drawing unchanged); creating through it makes a drawing-owned surface that
  survives normal Qt save/close/open of a DWG. Test these in vertical TDD slices
  using QtEditorTests and the hidden desktop runner.
- Validation: Release build, focused Qt workflows, full supported CTest suite,
  output-layout and verification checklists. Capture affected UI under
  build/artifacts for required visual review. No issue edits or PR creation.

## Results

- Change: `RibbonCatalog.hpp` Home/Create Ground Data/Surfaces now dispatches
  `@createSurface` and is enabled (no longer a placeholder).
- Tests (`tests/QtEditorTests.cpp`): `homeSurfacesOpensCreateSurfaceAndCancelLeavesDrawingUnchanged`,
  `homeSurfacesCreatedSurfaceSurvivesSaveAndReopen` (DWG save/reopen). Both PASS;
  `qt.editor` 198 passed, 0 failed.
- Assumption: wiring only, no visible-UI redesign; the button was previously disabled.
- Technical debt: none new.
