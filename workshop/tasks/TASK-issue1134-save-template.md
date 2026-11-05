# Issue #1134 — Save a drawing template

## Status

Approved for implementation under amended REQ-424 (D-2026-10-30-template-save).

## Requirement authority

- Accepted REQ-424 covers discovering and choosing `.gst` files for a new drawing.
- REQ-424 was amended to authorize saving a template (2026-10-30, D-2026-10-30-template-save).
- The user selected a dedicated **File → Save as Template…** command, template-only content,
  and the existing per-user template directory as the dialog's initial location.

## Evidence checked

- `src/qt/EditorWindow.cpp`: the New Drawing chooser scans bundled templates and
  `<UserDataDirectory>/drawing-templates/` for `.gst` files.
- `src/io/GsIo.cpp`: `SaveGoSurveyTemplateFile` currently serializes the whole document payload;
  no Qt UI calls it. Filtering reusable template content would require explicit scope.
- `resources/wiki/Files-and-Drawings.md` and `resources/wiki/Workflows.md`: describe setting up
  layers, text styles, layouts, and page setups; instruct users to save a `.gst` and configure it
  as a startup template, though no UI currently performs the save.
- Issue #1134 has no comments resolving the remaining questions.
- Autodesk's official template guidance describes templates as carrying styles, settings, units,
  layers, text styles, layouts, linetype scale, and page setups. This supports the issue's proposed
  template-only content but does not determine GoSurvey's scope.

## Planned scope after decisions

- Add the dedicated template-save action and `.gst` save dialog; default it to
  `<UserDataDirectory>/drawing-templates/`.
- Save layers, styles, layouts/page setups, plot scale, insertion units and drawing settings;
  exclude all model/paper geometry, survey points and point groups.
- Ensure cancellation leaves the drawing and filesystem unchanged; report save failures.
- Make a successful personal template available in the existing New Drawing chooser and usable as
  the startup template.
- Preserve `.gst` rejection in File → Open.
- Update affected in-app and website documentation and record README/PRIVACY/wiki review before PR.

## Verification and architecture

- Add focused Qt workflow coverage for save, cancel, failure, chooser discovery, template content,
  and File → Open behavior after the requirement is accepted.
- Keep template saving in the existing IO/UI owners; do not add a new file format or abstraction.
- Check project-read-only behavior and ensure a template save does not mutate the active drawing's
  DWG path or saved state.

## Completion record

- **SPEC:** PASS. User choices recorded in D-2026-10-30-template-save and amended REQ-424.
- **Build:** `.\build.bat` Release succeeded.
- **Tests:** full `ctest --preset ninja-release --output-on-failure` passed, 191/191. The focused Qt run passed all six Qt shards.
- **Code/architecture review:** manual review found no additional layer, dependency, or production abstraction. A test-only full-payload helper keeps DWG JSON round-trip coverage separate from `.gst` semantics. `git diff --check` passed.
- **Qt capture:** File menu and save dialog captures are under `build/artifacts/qt-editor/manual/shard-4/artifacts/qt-1134-*.png`. The required Civil 3D comparison is **inconclusive**: Civil 3D is not installed (the existing AutoCAD 2026 process was left untouched).
- **Product documentation review:** `README.md` and `PRIVACY.md` reviewed; no changes needed. Updated the in-app manual in `resources/wiki/Files-and-Drawings.md`, `resources/wiki/User-Interface.md`, and `resources/wiki/Workflows.md`. Updated `docs/qt-074-parity-ledger.md`. Reviewed the public GitHub Wiki's Files and Drawings and Workflows pages; those external pages and the `gosurveycad.com/docs` route are not editable from this source checkout (the web reader could not access the site route). Per CLAUDE.md's pre-PR rule, those public pages must be updated before a PR is opened; no PR is being opened in this task.
- **Assumptions / technical debt:** none recorded.
- **Verdict:** PASS for local implementation and verification; the Civil 3D comparison and external public-manual updates remain pre-PR follow-ups.

## Scope amendment — 2026-11-02

- **Owner decision:** `.gst` also preserves supported model- and paper-space vector linework, layout border blocks and their referenced definitions, and reusable drawing styles/settings including point-group rules and survey-point marker/label styling. Survey-point instances and unrelated non-linework drawing objects remain excluded.
- **Implementation:** `SaveGoSurveyTemplateFile` now filters non-linework entity arrays while retaining vector geometry, layout setup, point-group definitions, and block-definition dependencies referenced by layout borders and marker styles. Updated the accepted REQ-424 statement and in-app Files/Workflows wiki.
- **Verification:** Release `.\build.bat` succeeded after the change. `issue1134SaveAsTemplatePreservesReusableLineworkAndSetup` passed (3 Qt test cases including init/cleanup). `git diff --check` passed. The full CTest suite was not rerun for this scope amendment; prior full-suite results above predate it.
- **External documentation:** README and PRIVACY remain unaffected. Public GitHub Wiki and `gosurveycad.com/docs` remain outside this checkout's editable sources; the existing PR's documentation follow-up remains outstanding.
- **Beta integration:** Merged `origin/beta` at `9ffa0924` to include the newer drawing-owned survey-point marker/label and table style models. The merge also advances PR #1143 to the current development base; the project decision log retains beta decisions and the amended template decision.
- **Final verification on merged base:** Full Release `.\build.bat` succeeded. `issue1134SaveAsTemplatePreservesReusableLineworkAndSetup` passed after saving the `.gst`, setting it as the configured default template, opening it with New Drawing, saving the created drawing to DWG, and reopening that DWG. Assertions cover model/layout linework, layout border blocks and definitions, custom text/dimension/multileader/table/surface/point-marker/point-label styles, and point-group rules; survey-point instances and unrelated block definitions are omitted. The full CTest suite was not rerun after integrating beta.
- **Review follow-up:** Updated the parity ledger to the current test and retained-linework scope. Template serialization now strips captured map-tile payloads while keeping other drawing settings, and matches block dependencies case-insensitively through nested definitions. Replaced the obsolete chooser-based New Drawing tests with configured-default open, DWG reopen, invalid-template recovery, and bundled-default coverage; removed the chooser screenshot test. Release build and the two focused Qt tests passed (4 Qt cases including init/cleanup). Full CTest remains delegated to PR CI.
