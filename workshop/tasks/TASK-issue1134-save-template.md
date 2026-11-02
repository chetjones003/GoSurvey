# Issue #1134 — Save a drawing template

## Status

Approved for implementation under amended REQ-424 (D-2026-10-30-template-save), integrated with the configurable default-template workflow in PR #1143.

## Requirement authority

- D-2026-10-30-a superseded the New Drawing chooser with an Options-configured `.gst` default.
- REQ-424 was amended to authorize saving a template (2026-10-30, D-2026-10-30-template-save).
- The user selected a dedicated **File → Save as Template…** command, template-only content,
  and the existing per-user template directory as the dialog's initial location.

## Evidence checked

- `src/qt/EditorWindow.cpp`: New Drawing uses the configured default `.gst` (or the bundled default)
  for standalone drawings; project drawings continue to use project defaults.
- `src/io/GsIo.cpp`: `SaveGoSurveyTemplateFile` currently serializes the whole document payload;
  no Qt UI calls it. Filtering reusable template content would require explicit scope.
- At initial review, `resources/wiki/Files-and-Drawings.md` and `resources/wiki/Workflows.md`
  described saving a `.gst` for startup use, but had no save command. The in-app workflow now also
  documents selecting a saved template in Options → Files for future standalone drawings.
- Issue #1134 has no comments resolving the remaining questions.
- Autodesk's official template guidance describes templates as carrying styles, settings, units,
  layers, text styles, layouts, linetype scale, and page setups. This supports the issue's proposed
  template-only content but does not determine GoSurvey's scope.
- Current beta style managers add drawing-owned dimension, multileader, table, surface,
  survey-point, and point-label styles to the same JSON serializer used by `.gst` templates.
- Autodesk Civil 3D's [Marker tab guidance](https://help.autodesk.com/cloudhelp/2022/ENG/Civil3D-UserGuide/files/GUID-57F91BBB-CBDA-4098-9550-652AD714EFEE.htm)
  says an AutoCAD block marker is a reference to a block definition stored in the drawing. Beta
  stores that reference as `SurveyPointStyleDefinition::markerBlockName`, so omitting every block
  definition would leave a retained point style without its marker.

## Planned scope after decisions

- Add the dedicated template-save action and `.gst` save dialog; default it to
  `<UserDataDirectory>/drawing-templates/`.
- Save layers, all drawing-owned text/dimension/multileader/table/surface/survey-point/point-label
  styles, layouts/page setups, plot scale, insertion units and drawing settings. Exclude model/paper
  entities, survey points, point groups, source block INSERTs, and unrelated block definitions.
- Retain the block definitions directly or recursively referenced by a saved survey-point block
  marker style as style dependencies, so the point style remains usable when opened.
- Ensure cancellation leaves the drawing and filesystem unchanged; report save failures.
- Save the template in the personal template folder so it can be selected as the configured default
  in Options → Files; startup workspace selection remains separate.
- Preserve `.gst` rejection in File → Open.
- Update affected in-app and website documentation and record README/PRIVACY/wiki review before PR.

## Verification and architecture

- Add focused Qt workflow coverage for save, cancel, failure, template content,
  style-manager round trips (including nested block-marker dependencies), and File → Open behavior.
- Keep template saving in the existing IO/UI owners; do not add a new file format or abstraction.
- Check project-read-only behavior and ensure a template save does not mutate the active drawing's
  DWG path or saved state.

## Completion record

- **SPEC:** PASS. User choices recorded in D-2026-10-30-template-save and amended REQ-424.
- **Build:** `.\build.bat` Release succeeded.
- **Tests:** full `ctest --preset ninja-release --output-on-failure` passed, 191/191. The focused Qt run passed all six Qt shards.
- **Code/architecture review:** manual review found no additional layer, dependency, or production abstraction. A test-only full-payload helper keeps DWG JSON round-trip coverage separate from `.gst` semantics. `git diff --check` passed.
- **Qt capture:** File menu and save dialog captures are under `build/artifacts/qt-editor/manual/shard-4/artifacts/qt-1134-*.png`. The required Civil 3D comparison is **inconclusive**: Civil 3D is not installed (the existing AutoCAD 2026 process was left untouched).
- **Product documentation review:** `README.md` and `PRIVACY.md` reviewed; no changes needed. Updated the in-app manual in `resources/wiki/Files-and-Drawings.md`, `resources/wiki/User-Interface.md`, and `resources/wiki/Workflows.md`. Updated `docs/qt-074-parity-ledger.md`. Reviewed the public GitHub Wiki's Files and Drawings and Workflows pages; those external pages and the `gosurveycad.com/docs` route were not editable from this source checkout (the web reader could not access the site route). The public-manual update remains an external documentation follow-up.
- **Assumptions / technical debt:** none recorded.
- **Verdict:** PASS for local implementation and verification; the Civil 3D comparison and external public-manual updates remain pre-PR follow-ups.

## Beta-refresh continuation (2026-11-02)

- Applied the #1134 change on a fresh branch from origin/beta at 08dbae7f, incorporating recent style-manager and paper-space merges.
- D-2026-11-02-a and amended REQ-424 record the user-directed exception for point-style block-marker dependencies: required block definitions remain, while source INSERTs and unrelated definitions do not.
- Release build succeeded with build.bat.
- The focused Save as Template Qt test and existing template chooser workflow test each passed. All six qt.editor.shard-* CTest shards passed. The full 191-test suite passed on the original #1134 base before this beta refresh; the headless suite was not rerun on the refreshed branch.
- The focused round-trip coverage confirms custom text, dimension, multileader, table, surface, survey-point, and point-label styles survive save/load and New Drawing creation from the configured saved template. The point marker retains its block definition and nested dependency. Unrelated definitions, source INSERTs, model/paper entities, survey points, and point groups remain absent.
- Manual code review found no new dependency or layer. The IO save path selects the recursive closure of block definitions referenced by named point-marker styles. diff --check passed.
- Updated Files and Drawings and Workflows to describe the style families and block-marker dependencies. README and PRIVACY need no change. The public GitHub Wiki pages were reviewed but could not be edited from this checkout; gosurveycad.com/docs could not be accessed. This limitation was recorded before integrating the change into the already-open PR #1143.
- The Civil 3D comparison remains inconclusive because Civil 3D is not installed; the existing AutoCAD 2026 process was left untouched. This follow-up changed no visible UI.
- Integrated into PR #1143 on 2026-11-02. Its configurable default-template behavior remains unchanged; the saved template is opened by setting it in Options → Files. The updated focused Qt checks passed (3 tests); the Release build succeeded on the PR branch. The full headless suite was not rerun.
- Remaining limitation: a missing block definition referenced by a point style is not synthesized. Only present definitions and their nested dependencies are retained.
- **Updated verdict:** PASS for local implementation and current Qt verification; the Civil 3D comparison and external public-manual updates remain pre-PR follow-ups.
