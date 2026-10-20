# GoSurvey major-release feature checklist

Use this checklist on the exact release-candidate build before a major version release. It is a human product-acceptance pass: it complements [`verification/release-checklist.md`](../verification/release-checklist.md), which covers engineering and release gates.

## Run record

| Field | Record |
|---|---|
| GoSurvey version / build | |
| Release candidate / commit | |
| Windows version | |
| Display resolution / scaling | |
| GPU / driver | |
| Tester / date | |
| Result | PASS / FAIL / BLOCKED |
| Test drawing and fixture copies | |

## How to use this checklist

- Run every applicable case in order on a disposable copy of the release-candidate installation and test data. Do not use production survey data.
- Each case has prerequisites, numbered actions, and an observable expected result. Mark **PASS**, **FAIL**, or **BLOCKED**; explain any skipped step. A blocked feature is not a pass.
- Save the drawing before destructive or format-conversion tests. Keep the original fixtures unchanged and record the output paths.
- Use the bundled sample files in `samples/` where named. For other cases, create a small scratch drawing with the described geometry.
- This is feature-area coverage. It does not claim that every individual command, option, file-format edge, or supported entity has its own independent test. Use the linked Command Reference and Known Limitations pages to reconcile the full command inventory and current exclusions before sign-off.
- If observed behavior disagrees with the expected result, record exact steps, input file, command/dialog, message, and a screenshot or log when possible. Do not silently omit it.

## A. Start, windows, and drawing lifecycle

### A1. Launch, start screen, and new drawing

**Prerequisites:** Clean launch; no other GoSurvey instance is required.

1. Launch GoSurvey from the installed release-candidate shortcut.
2. Confirm the start screen appears and its New, Open, and recent-file routes respond.
3. Create a blank drawing; then create one from a bundled drawing template if templates are offered.
4. Open a second drawing tab and switch between the tabs.
5. Close one tab and then the remaining tab, responding to any save prompt appropriately.

**Expected:** App starts without an error; the requested drawing opens in the focused tab; tab switching and close/save prompts target the correct drawing; closing drawings does not unexpectedly close the app or lose acknowledged changes.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### A2. Create, save, reopen, and recover a drawing

**Prerequisites:** Scratch drawing with at least one line, one survey point, and a changed view.

1. Save the drawing as a `.gs` file to a new path.
2. Change geometry and view, save again, close the tab, and reopen the file.
3. Confirm geometry, point data, layers, units, and saved view are present.
4. Use Save As to create a second copy; make a change in the copy and verify the original remains unchanged.
5. Confirm there is no unsaved-change loss on a normal close: make a small edit, close the tab, choose Cancel at the unsaved-changes prompt, and verify the drawing remains open with the edit intact. Do not expect autosave/recovery unless the current release documentation says it shipped.

**Expected:** Save/open/Save As operate on the intended path; reopened data and view are retained; the source copy is not overwritten by Save As; cancelling close preserves the open drawing. There is no implied recovery feature.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### A3. Project creation, drawing membership, and handoff

**Prerequisites:** Disposable project folder and two scratch drawings; do not use a production project.

1. Create a new project and set its project-wide settings.
2. Add one drawing to the project, then reopen the project and confirm the drawing is listed and opens from the project.
3. Use the project toolspace to inspect its survey database, drawing membership, and health/status information.
4. Exercise the available project lock/join behavior with a second app instance or another local test user if the feature supports it; confirm an already-locked project is reported clearly.
5. Run the available project pack/turnover workflow to a new output folder, then inspect that package and open it from the new location.
6. Cancel a project operation mid-dialog and verify the project and source drawings remain usable.

**Expected:** Project settings and membership persist; survey/project status is visible; locking prevents conflicting edits as documented; package/turnover contains the selected project data and opens successfully; cancellation does not damage the source project. Mark unavailable phases as BLOCKED and record them against the project's accepted rollout requirements.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### A4. User interface and command entry

**Prerequisites:** Open a scratch drawing.

1. Visit each ribbon tab and the File, Edit, and View menus; open and dismiss a representative dialog or panel from each available area.
2. Type a known command in the command line; confirm its prompt appears and complete it by typing input.
3. Repeat a point-pick command by selecting a point in the viewport; cancel an active command with Escape.
4. Open command history/help, use a documented keyboard shortcut, and try a misspelled command. Check whether a recent-files list is actually part of this release before expecting one.
5. Resize or reposition the command line if available, restart the app, and confirm its documented preference persists.

**Expected:** Shipped controls are enabled and routed to the expected action; typed and viewport input both reach the active command; cancellation leaves no partial geometry; unknown commands produce a clear response; help/history and documented preference behavior work. A control that is explicitly disabled or absent in current release documentation is recorded as unavailable, not passed as a working feature.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

## B. Drafting, selection, and editing

### B1. Draw geometry with typed and viewport input

**Prerequisites:** Empty scratch drawing; coordinate input and snaps enabled.

1. Draw a line using typed coordinates, then draw another by viewport picks.
2. Draw representative supported primitives: polyline, arc, circle, ellipse, rectangle, and a solid primitive such as a box or cylinder.
3. Use relative or bearing input for one point, then use an object snap for another.
4. Cancel a third drawing command before its final point.

**Expected:** Each accepted input creates the intended geometry at the intended location; snaps and coordinate formats resolve predictably; cancellation creates no incomplete object. If a listed primitive is not present in this build, record it against Known Limitations rather than silently passing it.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### B2. Selection, properties, and grips

**Prerequisites:** Drawing contains overlapping lines, a polyline, a circle, and a 3D solid.

1. Select each object by click, window, crossing window, and selection cycling where applicable.
2. Use Ctrl/Shift selection gestures documented by the UI; inspect and clear the current selection.
3. Open Properties, change a safe property (such as layer or color), and confirm the object updates.
4. Select a supported object and edit a grip; press Escape before committing another grip edit.
5. Undo a completed edit and redo it.

**Expected:** Correct objects are selected, overlap choices are accessible, properties reflect and update the selected object, grip edits commit once, cancellation restores the original, and undo/redo restore the expected states.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### B3. Modify tools and clipboard

**Prerequisites:** Several simple objects on separate layers; save a baseline copy.

1. Move, copy, rotate, and scale one selected object using typed values and viewport points.
2. Use TRIM and OFFSET on intersecting lines; use other modify tools listed as supported for this release.
3. Use ARRAY or another documented repeated-edit tool. Do not count a disabled/unimplemented command as a passing workflow.
4. Copy and paste an object, then delete a disposable object.
5. Cancel one operation at its selection/pick prompt; undo each committed operation.

**Expected:** Resulting geometry and counts match the requested operation; cancel leaves the drawing unchanged; each committed edit can be undone/redone without affecting unrelated objects. Reconcile each command against the current build because the wiki Known Limitations page is versioned and may lag the release candidate.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### B4. Snaps, drafting aids, coordinate systems, and views

**Prerequisites:** Geometry with endpoints, midpoints, centers, intersections, and a non-world-aligned edge.

1. Enable/disable representative object snaps and verify the cursor candidate on endpoint, midpoint, center, and intersection.
2. Toggle ORTHO; draw a short segment with it enabled. If polar tracking is claimed as supported in this release, test it too; otherwise record it as unavailable.
3. Set a user coordinate system from a non-world-aligned object and enter a point; return to World/Plan.
4. Pan, zoom, zoom extents, orbit the 3D model, and restore a named/saved view if available.
5. Save, close, reopen, and confirm the drawing view/coordinate behavior.

**Expected:** Snap candidates and drafting constraints match their indicators; typed points follow the active coordinate frame; navigation changes the view without altering model geometry; saved view behavior matches the manual.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

## C. Annotation, layers, and drawing setup

### C1. Layers, linetypes, and object appearance

**Prerequisites:** At least three objects.

1. Create a layer, assign a color and linetype, and move an object onto it.
2. Hide/freeze and lock the layer using the available controls; attempt to select/edit its object.
3. Restore visibility and editability; rename the layer and delete an unused layer.
4. Save and reopen the drawing.

**Expected:** Layer state controls behave as documented for this release and prevent edits when locked/frozen if those rules are supported; object appearance follows layer properties; valid changes persist; invalid deletion is refused clearly. If a control is stored but not enforced, record that as a failure only when current release requirements/manual claim it is enforced.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### C2. Text, dimensions, leaders, and hatches

**Prerequisites:** Lines and a closed boundary in a scratch drawing.

1. Add single-line text and multiline text; edit text content and formatting.
2. Add linear/aligned and angular dimensions; change dimension style or scale.
3. Add a leader and a hatch/fill to the closed boundary if those tools are supported.
4. Move or edit each annotation using its grips/properties; save and reopen.

**Expected:** Annotation is legible and placed correctly; dimension values reflect measured geometry; edits persist and remain associated as documented. Record unsupported annotation types from Known Limitations.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### C3. Units, display preferences, and settings persistence

**Prerequisites:** New scratch drawing and access to application/drawing settings.

1. Open Options/Settings and visit every populated settings page.
2. Change a reversible display preference (theme, crosshair, selection, or command-line preference) and apply it.
3. Set drawing units and angle display; check a coordinate/distance readout and create one measurement.
4. Save preferences, close and restart; reopen the drawing and inspect both app and drawing settings.
5. Restore preferred settings after the test.

**Expected:** Controls accept valid values, displayed units/angles match chosen settings, saved application preferences persist at app scope, and drawing-specific units persist with the drawing. Empty or unavailable settings pages are recorded as findings only if the current manual claims controls there.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

## D. Survey and civil workflows

### D1. Survey points and point groups

**Prerequisites:** Copies of `samples/state-plane-points.csv` and a small CSV with one malformed row.

1. Import the valid CSV through the documented point import workflow.
2. Inspect point numbers, coordinates, elevations, descriptions, and labels in the drawing/grid.
3. Create a point group with a rule matching a subset; change its display/visibility.
4. Export selected points, then re-import the export into a disposable drawing.
5. Import the malformed CSV and inspect the reported error and resulting data.

**Expected:** Valid points and attributes are preserved; group membership and display follow its rule; export/import round-trips supported fields; malformed input is reported and does not silently create guessed data or damage existing points.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### D2. Traverse import, adjustment, and commit

**Prerequisites:** Copies of `samples/traverse_sample.fbk`, `samples/closed_loop_trav.fbk`, and where applicable `.rw5`, `.jxl`, or `.dat` examples.

1. Open the traverse/adjustment workflow and import a supported field-data file.
2. Review imported observations, settings, warnings, and the unadjusted closure.
3. Run the adjustment; inspect residuals/report and export a report.
4. Accept/commit the result into a disposable drawing; verify stations/coordinates.
5. Cancel a second import or adjustment before commit and confirm no partial result was added.

**Expected:** Supported records are mapped and errors are visible; adjustment/report values are produced; commit adds the intended points once; cancellation preserves the prior drawing and session data. Record each tested input format.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### D3. Surfaces, analysis, and volumes

**Prerequisites:** Copy `samples/state-plane-points.csv` or another known XYZ point set; at least two point groups/surfaces for volume comparison.

1. Create a surface from points, rebuild it, and inspect its displayed triangulation/contours.
2. Add/remove a point or boundary/breakline source where supported; observe whether a rebuild is automatic or requires the documented Rebuild action, and check stale/rebuilding status.
3. Query an elevation and run a surface statistic/profile or catchment tool available in the build.
4. Compare two surfaces for cut/fill/net volume; inspect the report/dashboard and export the volume result if available.
5. Use a deliberately invalid or empty source and confirm the surfaced error and unchanged valid surface.

**Expected:** Surface is built from the selected sources; edits trigger the rebuild behavior documented for this release; query/analysis results are visible and internally consistent; volume output reports cut/fill/net as applicable; bad inputs fail visibly without corrupting the previous surface.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### D4. Feature lines, grading, and 3D modeling

**Prerequisites:** Surface from D3 and a short baseline polyline/feature line.

1. Create/edit a feature line or 3D polyline and verify elevations at its vertices.
2. Run a supported side-slope/daylight operation against the test surface.
3. Create a simple 3D solid, then try one supported Boolean or solid edit.
4. Orbit the view, inspect selection/properties, save, close, and reopen.
5. Attempt a documented invalid operation (for example, outside the surface boundary) and inspect its refusal.

**Expected:** Geometry appears at the entered elevations; grading stops/refuses according to documented surface limits; solid edits produce the requested result or a clear refusal; saved 3D objects remain visible and selectable after reopen.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

## E. External files, references, and output

### E1. Interchange round-trip and unsupported content reporting

**Prerequisites:** Copies of a representative DXF and DWG from `samples/`, plus the scratch drawing from earlier tests.

1. Import the DXF/DWG through the File menu and inspect geometry, layers, units, and points.
2. Export a copy to DXF and DWG using the documented options.
3. Open each export in GoSurvey; compare representative geometry and survey data to the source.
4. If a compatible external CAD viewer is available, open the exports there as a supplementary check.
5. Import a malformed/unsupported fixture and inspect warnings, exclusion messages, and drawing state.

**Expected:** Supported entities and metadata round-trip within documented limits; unsupported objects are reported rather than silently represented as valid data; a failed import leaves the active drawing unchanged. Record format/version and any excluded entity messages.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### E2. Point, model, and PDF inputs

**Prerequisites:** Samples for point/model/PDF formats available in this build; use copies.

1. Import/export supported point-file formats and compare a few records and coordinates.
2. Import a supported 3D model; inspect orientation, scale, name, color, and placement.
3. Attach a PDF underlay, place/scale it, navigate pages, and test any supported annotation or measurement workflow.
4. Detach or cancel placement; try a missing or unreadable file.

**Expected:** Each supported input is placed/read with expected units and orientation; PDF page and underlay controls work; cancellation creates no dangling reference; failures identify the file problem and preserve the drawing. Record each format tested and unavailable formats separately.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### E3. Point cloud attachment and viewport interaction

**Prerequisites:** A supported E57/LAS/LAZ/PTS/PTX fixture available locally; test with a disposable copy and a machine with enough free disk space.

1. Attach/import the point cloud using the release's documented route and accept the default placement.
2. Confirm the cloud appears in the drawing and can be framed, panned, zoomed, and orbited.
3. Change its visibility/display settings; inspect a small region at close and distant zoom levels.
4. Save and reopen the drawing; confirm the reference reloads and any missing-source warning identifies the file.
5. Detach or cancel a second cloud load and confirm the first cloud and drawing remain intact.

**Expected:** Supported point data streams/displays without hanging or corrupting the drawing; visibility and navigation respond; saved references reload or report a missing source clearly; cancelling/detaching the second reference preserves the first. Record the format and fixture; if no suitable fixture is available, mark BLOCKED and state what is needed.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### E4. Paper layouts, viewports, and plotting

**Prerequisites:** Drawing with model geometry and at least one annotation.

1. Add/rename a paper layout and set paper size and orientation.
2. Add two viewports at different scales; pan/zoom one viewport and confirm the other is unaffected.
3. Set viewport layer visibility/freeze and inspect paper-space objects.
4. Preview a plot, export a PDF, and inspect page size, orientation, scale, clipping, and annotation legibility.
5. Save, reopen, and repeat preview.

**Expected:** Layout and viewport states are independent and persist; plot preview and output match the chosen sheet and scale; no unexpected clipping or blank output occurs. Record any entity types omitted by plotting/export.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### E5. Blocks, attributes, and reusable content

**Prerequisites:** A drawing with a simple block definition or bundled block-library sample.

1. Create or import a block using a supported command/library route; insert multiple references at different positions/scales.
2. Edit an attribute value, then synchronize attributes if that workflow is available.
3. Edit/save the definition and inspect the references.
4. Export a block to a separate drawing/file and reopen it.

**Expected:** Inserted references use the right definition and attributes; definition edits and synchronization behave as documented; exported content opens without corruption. Record unsupported block features based on the current release documentation; do not rely on an older limitation statement without checking the candidate.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

## F. Release-critical behavior

### F1. Failure, cancellation, undo, and read-only safety

**Prerequisites:** A saved copy of a drawing containing geometry, points, and a surface.

1. Open a valid file read-only if the workflow supports it; try a geometry edit and a data import.
2. Start then cancel a multi-step draw, import, or modeling operation at an intermediate prompt.
3. Trigger one expected validation failure (invalid value, missing file, or impossible geometry).
4. Undo and redo a successful edit; close without saving and reopen the baseline.

**Expected:** Read-only state prevents writes; cancellation and validation failure leave no partial changes; errors are visible and understandable; undo/redo and close-without-save restore the exact expected state.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### F2. Responsiveness and stability in ordinary use

**Prerequisites:** Use a representative large drawing or a generated benchmark scene only if one is provided for the release candidate.

1. Open the drawing and observe startup, import/rebuild feedback, and basic responsiveness.
2. Pan, zoom, and orbit continuously for one minute; switch tabs and open/close a panel.
3. Leave a long operation running, cancel it if cancellation is offered, and continue editing.
4. Record any freeze, crash, rendering corruption, or unusually long wait with the operation and approximate duration.

**Expected:** Common interaction remains usable; long work communicates progress or completion; cancel returns control when offered; no crash or persistent rendering corruption occurs. This manual observation does not replace REQ-100 benchmarking.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### F3. Update, help, and user-facing notices

**Prerequisites:** Release-candidate network access if update checks are enabled; otherwise test help and notices offline.

1. Open Help/About and verify product name, version, and links.
2. Open the release notes/What’s New route and confirm it describes this candidate.
3. Check for updates using the documented control; do not install a newer build unless it is the intended test.
4. Review the privacy/telemetry and crash-report disclosures shown by the app.

**Expected:** Version and user-facing links are correct; update state is clear and does not replace the app without an explicit supported action; disclosures are visible and consistent with the release behavior.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

## G. Individual command tests

This catalog-driven section contains one test for each command listed by category in the current Command Reference. Test every listed alias independently against a reset baseline. The linked command article supplies exact prompt choices and minimal valid values; if it is missing, derive and record the steps from current in-app HELP before testing. Do not skip a command.

### Draw

#### [3DPOLY] — Draw a polyline whose vertices each carry their own elevation

**Aliases to test:** `3DP`, `3DPOLYLINE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [3DPOLY details](../resources/wiki/Commands/3dpoly.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [3DPOLY details](../resources/wiki/Commands/3dpoly.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`3DP`, `3DPOLYLINE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Draw a polyline whose vertices each carry their own elevation. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ARC] — Draw an arc

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ARC details](../resources/wiki/Commands/arc.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ARC details](../resources/wiki/Commands/arc.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Draw an arc. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [CIRCLE] — Draw a circle

**Aliases to test:** `C`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [CIRCLE details](../resources/wiki/Commands/circle.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [CIRCLE details](../resources/wiki/Commands/circle.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`C`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Draw a circle. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ELLIPSE] — Draw an ellipse

**Aliases to test:** `EL`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ELLIPSE details](../resources/wiki/Commands/ellipse.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ELLIPSE details](../resources/wiki/Commands/ellipse.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`EL`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Draw an ellipse. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [FEATURELINE] — Draw a feature line: named 3D linework with per-vertex elevations (REQ-087)

**Aliases to test:** `FL`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [FEATURELINE details](../resources/wiki/Commands/featureline.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [FEATURELINE details](../resources/wiki/Commands/featureline.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`FL`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Draw a feature line: named 3D linework with per-vertex elevations (REQ-087). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [FEATURELINELIST] — List every feature line and its vertices

**Aliases to test:** `FLLIST`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [FEATURELINELIST details](../resources/wiki/Commands/featurelinelist.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [FEATURELINELIST details](../resources/wiki/Commands/featurelinelist.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`FLLIST`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** List every feature line and its vertices. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [HATCH] — Fill a closed area (pick an internal point)

**Aliases to test:** `H`, `BHATCH`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [HATCH details](../resources/wiki/Commands/hatch.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [HATCH details](../resources/wiki/Commands/hatch.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`H`, `BHATCH`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Fill a closed area (pick an internal point). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [LINE] — Draw line segments

**Aliases to test:** `L`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [LINE details](../resources/wiki/Commands/line.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [LINE details](../resources/wiki/Commands/line.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`L`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Draw line segments. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [POLYLINE] — Draw a connected polyline

**Aliases to test:** `PL`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [POLYLINE details](../resources/wiki/Commands/polyline.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [POLYLINE details](../resources/wiki/Commands/polyline.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`PL`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Draw a connected polyline. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [RECT] — Draw a rectangle (two opposite corners)

**Aliases to test:** `RECTANG`, `RECTANGLE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [RECT details](../resources/wiki/Commands/rect.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [RECT details](../resources/wiki/Commands/rect.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`RECTANG`, `RECTANGLE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Draw a rectangle (two opposite corners). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Solids and 3D

#### [BOX] — Create a box solid: BOX <X,Y[,Z]> <length> <width> <height>

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BOX details](../resources/wiki/Commands/box.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BOX details](../resources/wiki/Commands/box.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a box solid: BOX <X,Y[,Z]> <length> <width> <height>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [CONE] — Create a cone solid: CONE <X,Y[,Z]> <base radius> <top radius> <height>

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [CONE details](../resources/wiki/Commands/cone.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [CONE details](../resources/wiki/Commands/cone.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a cone solid: CONE <X,Y[,Z]> <base radius> <top radius> <height>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [CYLINDER] — Create a cylinder solid: CYLINDER <X,Y[,Z]> <radius> <height>

**Aliases to test:** `CYL`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [CYLINDER details](../resources/wiki/Commands/cylinder.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [CYLINDER details](../resources/wiki/Commands/cylinder.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`CYL`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a cylinder solid: CYLINDER <X,Y[,Z]> <radius> <height>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [EXTRUDE] — Extrude a selected closed polyline or circle into a solid: EXTRUDE <height>

**Aliases to test:** `EXT`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [EXTRUDE details](../resources/wiki/Commands/extrude.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [EXTRUDE details](../resources/wiki/Commands/extrude.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`EXT`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Extrude a selected closed polyline or circle into a solid: EXTRUDE <height>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [INTERSECT] — Keep only the volume two selected solids share

**Aliases to test:** `IN`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [INTERSECT details](../resources/wiki/Commands/intersect.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [INTERSECT details](../resources/wiki/Commands/intersect.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`IN`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Keep only the volume two selected solids share. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ISOLINES] — Curves drawn around a curved solid face: ISOLINES [0-256], or bare to report

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ISOLINES details](../resources/wiki/Commands/isolines.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ISOLINES details](../resources/wiki/Commands/isolines.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Curves drawn around a curved solid face: ISOLINES [0-256], or bare to report. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [LOFT] — Loft a solid through two or more selected closed polylines or circles, in pick order

**Aliases to test:** `LFT`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [LOFT details](../resources/wiki/Commands/loft.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [LOFT details](../resources/wiki/Commands/loft.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`LFT`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Loft a solid through two or more selected closed polylines or circles, in pick order. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [POLYSOLID] — Sweep a wall along a path: POLYSOLID, then points (A arc, C close, H/W/J, O object)

**Aliases to test:** `PSOLID`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [POLYSOLID details](../resources/wiki/Commands/polysolid.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [POLYSOLID details](../resources/wiki/Commands/polysolid.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`PSOLID`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Sweep a wall along a path: POLYSOLID, then points (A arc, C close, H/W/J, O object). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PRESSPULL] — Move a solid FACE, or turn a closed shape into a solid: PRESSPULL, select a target, then a distance

**Aliases to test:** `PP`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PRESSPULL details](../resources/wiki/Commands/presspull.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PRESSPULL details](../resources/wiki/Commands/presspull.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`PP`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Move a solid FACE, or turn a closed shape into a solid: PRESSPULL, select a target, then a distance. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PYRAMID] — Create a pyramid solid: PYRAMID <X,Y[,Z]> <sides> <base r> <top r> <height>

**Aliases to test:** `PYR`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PYRAMID details](../resources/wiki/Commands/pyramid.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PYRAMID details](../resources/wiki/Commands/pyramid.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`PYR`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a pyramid solid: PYRAMID <X,Y[,Z]> <sides> <base r> <top r> <height>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [REVOLVE] — Revolve a selected closed polyline or circle about an axis into a solid

**Aliases to test:** `REV`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [REVOLVE details](../resources/wiki/Commands/revolve.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [REVOLVE details](../resources/wiki/Commands/revolve.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`REV`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Revolve a selected closed polyline or circle about an axis into a solid. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SECTION] — Cross-section of the selected solids by the active UCS plane, as a closed polyline

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SECTION details](../resources/wiki/Commands/section.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SECTION details](../resources/wiki/Commands/section.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Cross-section of the selected solids by the active UCS plane, as a closed polyline. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SLICE] — Cut selected solids with a plane (three points), keeping one side or both

**Aliases to test:** `SL`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SLICE details](../resources/wiki/Commands/slice.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SLICE details](../resources/wiki/Commands/slice.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SL`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Cut selected solids with a plane (three points), keeping one side or both. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SOLIDCHECK] — Check every solid (or the selection): closed, manifold, oriented, self-intersecting

**Aliases to test:** `SCHECK`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SOLIDCHECK details](../resources/wiki/Commands/solidcheck.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SOLIDCHECK details](../resources/wiki/Commands/solidcheck.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SCHECK`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Check every solid (or the selection): closed, manifold, oriented, self-intersecting. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SOLIDLIST] — List every solid: kind, layer, volume, surface area, topology counts

**Aliases to test:** `SOLIDS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SOLIDLIST details](../resources/wiki/Commands/solidlist.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SOLIDLIST details](../resources/wiki/Commands/solidlist.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SOLIDS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** List every solid: kind, layer, volume, surface area, topology counts. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SPHERE] — Create a sphere solid: SPHERE <X,Y[,Z]> <radius>

**Aliases to test:** `SPH`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SPHERE details](../resources/wiki/Commands/sphere.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SPHERE details](../resources/wiki/Commands/sphere.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SPH`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a sphere solid: SPHERE <X,Y[,Z]> <radius>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SUBTRACT] — Subtract the second selected solid from the first

**Aliases to test:** `SU`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SUBTRACT details](../resources/wiki/Commands/subtract.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SUBTRACT details](../resources/wiki/Commands/subtract.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SU`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Subtract the second selected solid from the first. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SWEEP] — Sweep a closed profile along a line, arc or open polyline path (T twist, A path-alignment)

**Aliases to test:** `SWP`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SWEEP details](../resources/wiki/Commands/sweep.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SWEEP details](../resources/wiki/Commands/sweep.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SWP`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Sweep a closed profile along a line, arc or open polyline path (T twist, A path-alignment). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [TORUS] — Create a torus solid: TORUS <X,Y[,Z]> <radius> <tube radius>

**Aliases to test:** `TOR`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [TORUS details](../resources/wiki/Commands/torus.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [TORUS details](../resources/wiki/Commands/torus.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`TOR`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a torus solid: TORUS <X,Y[,Z]> <radius> <tube radius>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [UNION] — Combine two selected solids into one

**Aliases to test:** `UNI`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [UNION details](../resources/wiki/Commands/union.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [UNION details](../resources/wiki/Commands/union.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`UNI`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Combine two selected solids into one. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [WEDGE] — Create a wedge solid: WEDGE <X,Y[,Z]> <length> <width> <height>

**Aliases to test:** `WE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [WEDGE details](../resources/wiki/Commands/wedge.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [WEDGE details](../resources/wiki/Commands/wedge.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`WE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a wedge solid: WEDGE <X,Y[,Z]> <length> <width> <height>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Annotation

#### [DIMALIGNED] — Aligned dimension

**Aliases to test:** `DAL`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [DIMALIGNED details](../resources/wiki/Commands/dimaligned.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [DIMALIGNED details](../resources/wiki/Commands/dimaligned.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`DAL`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Aligned dimension. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [DIMANGULAR] — Angular dimension

**Aliases to test:** `DAN`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [DIMANGULAR details](../resources/wiki/Commands/dimangular.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [DIMANGULAR details](../resources/wiki/Commands/dimangular.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`DAN`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Angular dimension. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [DIMLINEAR] — Linear dimension

**Aliases to test:** `DLI`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [DIMLINEAR details](../resources/wiki/Commands/dimlinear.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [DIMLINEAR details](../resources/wiki/Commands/dimlinear.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`DLI`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Linear dimension. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [DIMSTY] — Dimension style editor

**Aliases to test:** `DIMSTYLE`, `DSTY`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [DIMSTY details](../resources/wiki/Commands/dimsty.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [DIMSTY details](../resources/wiki/Commands/dimsty.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`DIMSTYLE`, `DSTY`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Dimension style editor. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [MTEXT] — Place multiline text

**Aliases to test:** `MT`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [MTEXT details](../resources/wiki/Commands/mtext.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [MTEXT details](../resources/wiki/Commands/mtext.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`MT`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Place multiline text. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [STYLE] — Text style manager: create / edit named text styles

**Aliases to test:** `ST`, `DDSTYLE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [STYLE details](../resources/wiki/Commands/style.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [STYLE details](../resources/wiki/Commands/style.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`ST`, `DDSTYLE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Text style manager: create / edit named text styles. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [TEXT] — Place single-line text

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [TEXT details](../resources/wiki/Commands/text.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [TEXT details](../resources/wiki/Commands/text.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Place single-line text. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Modify

#### [ALIGN] — Align objects to others

**Aliases to test:** `AL`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ALIGN details](../resources/wiki/Commands/align.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ALIGN details](../resources/wiki/Commands/align.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`AL`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Align objects to others. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ARRAY] — Rectangular or polar array of copies

**Aliases to test:** `AR`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ARRAY details](../resources/wiki/Commands/array.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ARRAY details](../resources/wiki/Commands/array.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`AR`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Rectangular or polar array of copies. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BREAK] — Split an object at one or two picked points

**Aliases to test:** `BR`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BREAK details](../resources/wiki/Commands/break.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BREAK details](../resources/wiki/Commands/break.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`BR`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Split an object at one or two picked points. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [CHAMFER] — Connect two curves with a straight bevel (Distance/Angle/Trim)

**Aliases to test:** `CHA`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [CHAMFER details](../resources/wiki/Commands/chamfer.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [CHAMFER details](../resources/wiki/Commands/chamfer.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`CHA`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Connect two curves with a straight bevel (Distance/Angle/Trim). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [COPY] — Copy objects

**Aliases to test:** `CP`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [COPY details](../resources/wiki/Commands/copy.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [COPY details](../resources/wiki/Commands/copy.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`CP`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Copy objects. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [DELETE] — Erase objects

**Aliases to test:** `DEL`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [DELETE details](../resources/wiki/Commands/delete.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [DELETE details](../resources/wiki/Commands/delete.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`DEL`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Erase objects. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [EXPLODE] — Explode selected block references

**Aliases to test:** `X`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [EXPLODE details](../resources/wiki/Commands/explode.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [EXPLODE details](../resources/wiki/Commands/explode.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`X`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Explode selected block references. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [EXTEND] — Extend objects to a boundary edge

**Aliases to test:** `EX`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [EXTEND details](../resources/wiki/Commands/extend.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [EXTEND details](../resources/wiki/Commands/extend.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`EX`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Extend objects to a boundary edge. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [FILLET] — Round a corner between two curves with a tangent arc (Radius/Trim)

**Aliases to test:** `F`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [FILLET details](../resources/wiki/Commands/fillet.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [FILLET details](../resources/wiki/Commands/fillet.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`F`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Round a corner between two curves with a tangent arc (Radius/Trim). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [JOIN] — Join collinear objects

**Aliases to test:** `J`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [JOIN details](../resources/wiki/Commands/join.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [JOIN details](../resources/wiki/Commands/join.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`J`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Join collinear objects. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [LENGTHEN] — Change an object's length (DElta/Percent/Total/DYnamic)

**Aliases to test:** `LEN`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [LENGTHEN details](../resources/wiki/Commands/lengthen.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [LENGTHEN details](../resources/wiki/Commands/lengthen.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`LEN`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Change an object's length (DElta/Percent/Total/DYnamic). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [MIRROR] — Mirror objects across a line

**Aliases to test:** `MI`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [MIRROR details](../resources/wiki/Commands/mirror.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [MIRROR details](../resources/wiki/Commands/mirror.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`MI`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Mirror objects across a line. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [MOVE] — Move objects

**Aliases to test:** `M`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [MOVE details](../resources/wiki/Commands/move.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [MOVE details](../resources/wiki/Commands/move.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`M`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Move objects. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [OFFSET] — Offset at a distance

**Aliases to test:** `O`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [OFFSET details](../resources/wiki/Commands/offset.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [OFFSET details](../resources/wiki/Commands/offset.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`O`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Offset at a distance. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [OVERKILL] — Remove duplicate geometry

**Aliases to test:** `OK`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [OVERKILL details](../resources/wiki/Commands/overkill.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [OVERKILL details](../resources/wiki/Commands/overkill.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`OK`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Remove duplicate geometry. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PASTE] — Paste from clipboard

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PASTE details](../resources/wiki/Commands/paste.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PASTE details](../resources/wiki/Commands/paste.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Paste from clipboard. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PASTEORIG] — Paste at original coordinates

**Aliases to test:** `PO`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PASTEORIG details](../resources/wiki/Commands/pasteorig.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PASTEORIG details](../resources/wiki/Commands/pasteorig.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`PO`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Paste at original coordinates. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ROTATE] — Rotate objects

**Aliases to test:** `RO`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ROTATE details](../resources/wiki/Commands/rotate.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ROTATE details](../resources/wiki/Commands/rotate.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`RO`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Rotate objects. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SCALE] — Scale objects

**Aliases to test:** `SC`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SCALE details](../resources/wiki/Commands/scale.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SCALE details](../resources/wiki/Commands/scale.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SC`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Scale objects. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [STRETCH] — Crossing/window-select, then move only the vertices inside the box

**Aliases to test:** `S`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [STRETCH details](../resources/wiki/Commands/stretch.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [STRETCH details](../resources/wiki/Commands/stretch.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`S`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Crossing/window-select, then move only the vertices inside the box. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [TRIM] — Trim objects to an edge

**Aliases to test:** `TR`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [TRIM details](../resources/wiki/Commands/trim.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [TRIM details](../resources/wiki/Commands/trim.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`TR`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Trim objects to an edge. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [TRIMSTATE] — TRIM mode: 0 = draw a line to trim (default), 1 = pick cutting edges

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [TRIMSTATE details](../resources/wiki/Commands/trimstate.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [TRIMSTATE details](../resources/wiki/Commands/trimstate.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** TRIM mode: 0 = draw a line to trim (default), 1 = pick cutting edges. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Selection and visibility

#### [HIDEOBJECTS] — Hide the selected objects

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [HIDEOBJECTS details](../resources/wiki/Commands/hideobjects.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [HIDEOBJECTS details](../resources/wiki/Commands/hideobjects.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Hide the selected objects. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ISOLATEOBJECTS] — Hide everything except the selection

**Aliases to test:** `ISOLATE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ISOLATEOBJECTS details](../resources/wiki/Commands/isolateobjects.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ISOLATEOBJECTS details](../resources/wiki/Commands/isolateobjects.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`ISOLATE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Hide everything except the selection. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [QUICKSELECT] — Select by object properties

**Aliases to test:** `QS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [QUICKSELECT details](../resources/wiki/Commands/quickselect.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [QUICKSELECT details](../resources/wiki/Commands/quickselect.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`QS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Select by object properties. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SELECT] — Build a selection set

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SELECT details](../resources/wiki/Commands/select.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SELECT details](../resources/wiki/Commands/select.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Build a selection set. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SELECTSIMILAR] — Select entities matching the current selection's type, layer and colour

**Aliases to test:** `SESIM`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SELECTSIMILAR details](../resources/wiki/Commands/selectsimilar.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SELECTSIMILAR details](../resources/wiki/Commands/selectsimilar.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SESIM`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Select entities matching the current selection's type, layer and colour. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [UNISOLATEOBJECTS] — Show every object hidden by isolation

**Aliases to test:** `UNISOLATE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [UNISOLATEOBJECTS details](../resources/wiki/Commands/unisolateobjects.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [UNISOLATEOBJECTS details](../resources/wiki/Commands/unisolateobjects.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`UNISOLATE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Show every object hidden by isolation. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### View and navigation

#### [CROSSHAIR3D] — 3D crosshair cursor showing the UCS axes: ON / OFF

**Aliases to test:** `CURSOR3D`, `XHAIR3D`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [CROSSHAIR3D details](../resources/wiki/Commands/crosshair3d.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [CROSSHAIR3D details](../resources/wiki/Commands/crosshair3d.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`CURSOR3D`, `XHAIR3D`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** 3D crosshair cursor showing the UCS axes: ON / OFF. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [FOV] — Perspective field of view, in degrees

**Aliases to test:** `LENS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [FOV details](../resources/wiki/Commands/fov.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [FOV details](../resources/wiki/Commands/fov.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`LENS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Perspective field of view, in degrees. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [GIZMO] — What the 3D gizmo does: GIZMO MOVE \| ROTATE \| SCALE

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [GIZMO details](../resources/wiki/Commands/gizmo.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [GIZMO details](../resources/wiki/Commands/gizmo.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** What the 3D gizmo does: GIZMO MOVE \| ROTATE \| SCALE. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ORBIT] — Free orbit the model view (drag with the left mouse button)

**Aliases to test:** `3DORBIT`, `3DO`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ORBIT details](../resources/wiki/Commands/orbit.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ORBIT details](../resources/wiki/Commands/orbit.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`3DORBIT`, `3DO`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Free orbit the model view (drag with the left mouse button). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PAN] — Pan the view (drag with the left mouse button)

**Aliases to test:** `P`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PAN details](../resources/wiki/Commands/pan.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PAN details](../resources/wiki/Commands/pan.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`P`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Pan the view (drag with the left mouse button). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PERSPECTIVE] — View projection: ON (perspective) / OFF (orthographic)

**Aliases to test:** `PROJECTION`, `PERSP`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PERSPECTIVE details](../resources/wiki/Commands/perspective.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PERSPECTIVE details](../resources/wiki/Commands/perspective.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`PROJECTION`, `PERSP`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** View projection: ON (perspective) / OFF (orthographic). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PLAN] — View the XY plane of a coordinate system (does not change the UCS)

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PLAN details](../resources/wiki/Commands/plan.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PLAN details](../resources/wiki/Commands/plan.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** View the XY plane of a coordinate system (does not change the UCS). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [REGEN] — Regenerate the drawing

**Aliases to test:** `RE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [REGEN details](../resources/wiki/Commands/regen.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [REGEN details](../resources/wiki/Commands/regen.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`RE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Regenerate the drawing. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [UCS] — Define the User Coordinate System (N names one; restore/delete in the View Manager)

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [UCS details](../resources/wiki/Commands/ucs.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [UCS details](../resources/wiki/Commands/ucs.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Define the User Coordinate System (N names one; restore/delete in the View Manager). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [UCSFOLLOW] — 0 = changing the UCS leaves the view alone; 1 = it switches to a plan view

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [UCSFOLLOW details](../resources/wiki/Commands/ucsfollow.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [UCSFOLLOW details](../resources/wiki/Commands/ucsfollow.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** 0 = changing the UCS leaves the view alone; 1 = it switches to a plan view. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VIEW] — Named views: VIEW [Save/Restore/Delete/?] <name>, or VIEW alone for the View Manager

**Aliases to test:** `V`, `DDVIEW`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VIEW details](../resources/wiki/Commands/view.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VIEW details](../resources/wiki/Commands/view.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`V`, `DDVIEW`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Named views: VIEW [Save/Restore/Delete/?] <name>, or VIEW alone for the View Manager. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VISUALSTYLE] — Viewport visual style: 2D / HIDDEN / SHADED

**Aliases to test:** `VS`, `VSCURRENT`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VISUALSTYLE details](../resources/wiki/Commands/visualstyle.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VISUALSTYLE details](../resources/wiki/Commands/visualstyle.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`VS`, `VSCURRENT`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Viewport visual style: 2D / HIDDEN / SHADED. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ZOOMEXTENTS] — Zoom to drawing extents

**Aliases to test:** `ZE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ZOOMEXTENTS details](../resources/wiki/Commands/zoomextents.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ZOOMEXTENTS details](../resources/wiki/Commands/zoomextents.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`ZE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Zoom to drawing extents. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ZOOMWINDOW] — Zoom to a window

**Aliases to test:** `ZW`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ZOOMWINDOW details](../resources/wiki/Commands/zoomwindow.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ZOOMWINDOW details](../resources/wiki/Commands/zoomwindow.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`ZW`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Zoom to a window. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Layers and properties

#### [LAYER] — Open the Layer manager

**Aliases to test:** `LA`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [LAYER details](../resources/wiki/Commands/layer.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [LAYER details](../resources/wiki/Commands/layer.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`LA`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Open the Layer manager. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VPFREEZE] — Freeze the picked entities' layers in the current viewport

**Aliases to test:** `VPF`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VPFREEZE details](../resources/wiki/Commands/vpfreeze.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VPFREEZE details](../resources/wiki/Commands/vpfreeze.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`VPF`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Freeze the picked entities' layers in the current viewport. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VPTHAW] — Thaw the picked entities' layers in the current viewport

**Aliases to test:** `VPT`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VPTHAW details](../resources/wiki/Commands/vpthaw.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VPTHAW details](../resources/wiki/Commands/vpthaw.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`VPT`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Thaw the picked entities' layers in the current viewport. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Survey and COGO

#### [CREATEPOINTS] — Create survey points

**Aliases to test:** `CRTPTS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [CREATEPOINTS details](../resources/wiki/Commands/createpoints.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [CREATEPOINTS details](../resources/wiki/Commands/createpoints.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`CRTPTS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create survey points. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [DIST] — 3D distance between two points: dX, dY, dZ, slope distance

**Aliases to test:** `DI`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [DIST details](../resources/wiki/Commands/dist.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [DIST details](../resources/wiki/Commands/dist.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`DI`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** 3D distance between two points: dX, dY, dZ, slope distance. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [EXPORTPOINTS] — Export survey points

**Aliases to test:** `EXPPTS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [EXPORTPOINTS details](../resources/wiki/Commands/exportpoints.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [EXPORTPOINTS details](../resources/wiki/Commands/exportpoints.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`EXPPTS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Export survey points. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ID] — Identify point coordinates

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ID details](../resources/wiki/Commands/id.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ID details](../resources/wiki/Commands/id.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Identify point coordinates. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [IMPORTPOINTS] — Import survey points

**Aliases to test:** `IMPPTS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [IMPORTPOINTS details](../resources/wiki/Commands/importpoints.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [IMPORTPOINTS details](../resources/wiki/Commands/importpoints.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`IMPPTS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Import survey points. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [INVERSE] — Inverse between two points

**Aliases to test:** `INV`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [INVERSE details](../resources/wiki/Commands/inverse.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [INVERSE details](../resources/wiki/Commands/inverse.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`INV`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Inverse between two points. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFELEV] — Surface elevation at a point; grade between two

**Aliases to test:** `SE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFELEV details](../resources/wiki/Commands/surfelev.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFELEV details](../resources/wiki/Commands/surfelev.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Surface elevation at a point; grade between two. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [TRAVERSE] — Open the Traverse Editor

**Aliases to test:** `TRAV`, `TRAVERSEEDITOR`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [TRAVERSE details](../resources/wiki/Commands/traverse.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [TRAVERSE details](../resources/wiki/Commands/traverse.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`TRAV`, `TRAVERSEEDITOR`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Open the Traverse Editor. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VIEWPOINTS] — View / edit survey points

**Aliases to test:** `VWPTS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VIEWPOINTS details](../resources/wiki/Commands/viewpoints.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VIEWPOINTS details](../resources/wiki/Commands/viewpoints.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`VWPTS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** View / edit survey points. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Surfaces and volumes

#### [CATCHMENT] — Catchment at an outlet: CATCHMENT <name>[, <x>, <y>] \| CATCHMENT EXTRACT

**Aliases to test:** `CATCH`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [CATCHMENT details](../resources/wiki/Commands/catchment.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [CATCHMENT details](../resources/wiki/Commands/catchment.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`CATCH`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Catchment at an outlet: CATCHMENT <name>[, <x>, <y>] \| CATCHMENT EXTRACT. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [DESIGNATEBOUNDARY] — Add a picked closed polyline as a surface boundary (outer/hide/show/clip)

**Aliases to test:** `DBD`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [DESIGNATEBOUNDARY details](../resources/wiki/Commands/designateboundary.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [DESIGNATEBOUNDARY details](../resources/wiki/Commands/designateboundary.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`DBD`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Add a picked closed polyline as a surface boundary (outer/hide/show/clip). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [DESIGNATEBREAKLINE] — Add a picked line/polyline as a surface breakline

**Aliases to test:** `DBL`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [DESIGNATEBREAKLINE details](../resources/wiki/Commands/designatebreakline.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [DESIGNATEBREAKLINE details](../resources/wiki/Commands/designatebreakline.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`DBL`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Add a picked line/polyline as a surface breakline. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [DESIGNATECONTOUR] — Add a picked line/polyline as a surface contour source

**Aliases to test:** `DCON`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [DESIGNATECONTOUR details](../resources/wiki/Commands/designatecontour.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [DESIGNATECONTOUR details](../resources/wiki/Commands/designatecontour.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`DCON`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Add a picked line/polyline as a surface contour source. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [EXTRACT] — Bake a surface's displayed contours into polylines: EXTRACT <surface>[, <layer>]

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [EXTRACT details](../resources/wiki/Commands/extract.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [EXTRACT details](../resources/wiki/Commands/extract.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Bake a surface's displayed contours into polylines: EXTRACT <surface>[, <layer>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [FLELEV] — Feature line elevations: FLELEV <n> [SET\|GRADEAHEAD\|GRADEBACK\|RAISE\|INSERT\|DELETE …]

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [FLELEV details](../resources/wiki/Commands/flelev.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [FLELEV details](../resources/wiki/Commands/flelev.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Feature line elevations: FLELEV <n> [SET\|GRADEAHEAD\|GRADEBACK\|RAISE\|INSERT\|DELETE …]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [FLELEVEDIT] — Open the feature line elevation editor: FLELEVEDIT [<n>]

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [FLELEVEDIT details](../resources/wiki/Commands/flelevedit.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [FLELEVEDIT details](../resources/wiki/Commands/flelevedit.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Open the feature line elevation editor: FLELEVEDIT [<n>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [QUICKPROFILE] — Quick profile along two points: QUICKPROFILE <surface>[, <x1>, <y1>, <x2>, <y2>]

**Aliases to test:** `QPROF`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [QUICKPROFILE details](../resources/wiki/Commands/quickprofile.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [QUICKPROFILE details](../resources/wiki/Commands/quickprofile.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`QPROF`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Quick profile along two points: QUICKPROFILE <surface>[, <x1>, <y1>, <x2>, <y2>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACEADDFILE] — Link a point file into a surface: SURFACEADDFILE <surface>, <path>[, <layout>[, HEADER]]

**Aliases to test:** `SFADDFILE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACEADDFILE details](../resources/wiki/Commands/surfaceaddfile.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACEADDFILE details](../resources/wiki/Commands/surfaceaddfile.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFADDFILE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Link a point file into a surface: SURFACEADDFILE <surface>, <path>[, <layout>[, HEADER]]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACEADDPOINT] — Add a TIN definition point: SURFACEADDPOINT <surface>[, <x>, <y>, <z>]

**Aliases to test:** `SFADDPT`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACEADDPOINT details](../resources/wiki/Commands/surfaceaddpoint.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACEADDPOINT details](../resources/wiki/Commands/surfaceaddpoint.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFADDPT`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Add a TIN definition point: SURFACEADDPOINT <surface>[, <x>, <y>, <z>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACECREATE] — Create a surface from point groups: SURFACECREATE <name>, <group>[, <group>…]

**Aliases to test:** `SFCREATE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACECREATE details](../resources/wiki/Commands/surfacecreate.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACECREATE details](../resources/wiki/Commands/surfacecreate.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFCREATE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a surface from point groups: SURFACECREATE <name>, <group>[, <group>…]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACECREATECORR] — Create a corridor surface: SURFACECREATECORR <name>

**Aliases to test:** `SFCORR`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACECREATECORR details](../resources/wiki/Commands/surfacecreatecorr.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACECREATECORR details](../resources/wiki/Commands/surfacecreatecorr.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFCORR`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a corridor surface: SURFACECREATECORR <name>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACECREATEGRID] — Create a grid surface: SURFACECREATEGRID <name>, ox, oy, sx, sy, cols, rows[, z…]

**Aliases to test:** `SFGRID`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACECREATEGRID details](../resources/wiki/Commands/surfacecreategrid.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACECREATEGRID details](../resources/wiki/Commands/surfacecreategrid.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFGRID`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a grid surface: SURFACECREATEGRID <name>, ox, oy, sx, sy, cols, rows[, z…]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACECREATEVOLGRID] — Grid volume surface: SURFACECREATEVOLGRID <name>, <base>, <comparison>

**Aliases to test:** `SFVOLGRID`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACECREATEVOLGRID details](../resources/wiki/Commands/surfacecreatevolgrid.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACECREATEVOLGRID details](../resources/wiki/Commands/surfacecreatevolgrid.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFVOLGRID`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Grid volume surface: SURFACECREATEVOLGRID <name>, <base>, <comparison>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACEDELETE] — Delete a surface: SURFACEDELETE <name>

**Aliases to test:** `SFDELETE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACEDELETE details](../resources/wiki/Commands/surfacedelete.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACEDELETE details](../resources/wiki/Commands/surfacedelete.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFDELETE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Delete a surface: SURFACEDELETE <name>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACEDELPOINT] — Delete nearest TIN definition point: SURFACEDELPOINT <surface>[, <x>, <y>]

**Aliases to test:** `SFDELPT`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACEDELPOINT details](../resources/wiki/Commands/surfacedelpoint.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACEDELPOINT details](../resources/wiki/Commands/surfacedelpoint.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFDELPT`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Delete nearest TIN definition point: SURFACEDELPOINT <surface>[, <x>, <y>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACEIMPORTFILE] — Import a linked point file into the drawing and break the link

**Aliases to test:** `SFIMPORTFILE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACEIMPORTFILE details](../resources/wiki/Commands/surfaceimportfile.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACEIMPORTFILE details](../resources/wiki/Commands/surfaceimportfile.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFIMPORTFILE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Import a linked point file into the drawing and break the link. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACELIST] — List every surface and its full definition

**Aliases to test:** `SFLIST`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACELIST details](../resources/wiki/Commands/surfacelist.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACELIST details](../resources/wiki/Commands/surfacelist.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFLIST`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** List every surface and its full definition. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACEMOVEPOINT] — Move a TIN definition point: SURFACEMOVEPOINT <surface>[, <x1>, <y1>, <x2>, <y2>, <z2>]

**Aliases to test:** `SFMOVEPT`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACEMOVEPOINT details](../resources/wiki/Commands/surfacemovepoint.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACEMOVEPOINT details](../resources/wiki/Commands/surfacemovepoint.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFMOVEPT`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Move a TIN definition point: SURFACEMOVEPOINT <surface>[, <x1>, <y1>, <x2>, <y2>, <z2>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACEREBUILD] — Rebuild a surface now (all surfaces if no name): SURFACEREBUILD [<name>]

**Aliases to test:** `SFREBUILD`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACEREBUILD details](../resources/wiki/Commands/surfacerebuild.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACEREBUILD details](../resources/wiki/Commands/surfacerebuild.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFREBUILD`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Rebuild a surface now (all surfaces if no name): SURFACEREBUILD [<name>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACERENAME] — Rename a surface: SURFACERENAME <old>, <new>

**Aliases to test:** `SFRENAME`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACERENAME details](../resources/wiki/Commands/surfacerename.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACERENAME details](../resources/wiki/Commands/surfacerename.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFRENAME`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Rename a surface: SURFACERENAME <old>, <new>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFACESTATS] — Surface statistics: SURFACESTATS [<name>]

**Aliases to test:** `SFSTATS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFACESTATS details](../resources/wiki/Commands/surfacestats.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFACESTATS details](../resources/wiki/Commands/surfacestats.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFSTATS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Surface statistics: SURFACESTATS [<name>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFDELLINE] — Delete an interior TIN edge: SURFDELLINE <surface>[, <x>, <y>]

**Aliases to test:** `SFDELLINE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFDELLINE details](../resources/wiki/Commands/surfdelline.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFDELLINE details](../resources/wiki/Commands/surfdelline.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFDELLINE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Delete an interior TIN edge: SURFDELLINE <surface>[, <x>, <y>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFSTYLE] — Surface style editor: contours, triangles, border (REQ-070)

**Aliases to test:** `SS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFSTYLE details](../resources/wiki/Commands/surfstyle.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFSTYLE details](../resources/wiki/Commands/surfstyle.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Surface style editor: contours, triangles, border (REQ-070). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SURFSWAPEDGE] — Swap a TIN interior edge: SURFSWAPEDGE <surface>[, <x>, <y>]

**Aliases to test:** `SFSWAP`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SURFSWAPEDGE details](../resources/wiki/Commands/surfswapedge.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SURFSWAPEDGE details](../resources/wiki/Commands/surfswapedge.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`SFSWAP`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Swap a TIN interior edge: SURFSWAPEDGE <surface>[, <x>, <y>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [UNDESIGNATE] — Remove one definition item: UNDESIGNATE <surface>, <BREAKLINE\|BOUNDARY\|POINTFILE\|CONTOUR>, <n>

**Aliases to test:** `UNDES`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [UNDESIGNATE details](../resources/wiki/Commands/undesignate.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [UNDESIGNATE details](../resources/wiki/Commands/undesignate.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`UNDES`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Remove one definition item: UNDESIGNATE <surface>, <BREAKLINE\|BOUNDARY\|POINTFILE\|CONTOUR>, <n>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VOLCSV] — Write CSV of volume results: VOLCSV [<path>]

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VOLCSV details](../resources/wiki/Commands/volcsv.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VOLCSV details](../resources/wiki/Commands/volcsv.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Write CSV of volume results: VOLCSV [<path>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VOLDASH] — Volume Dashboard: live cut/fill/net panel between two surfaces (REQ-073)

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VOLDASH details](../resources/wiki/Commands/voldash.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VOLDASH details](../resources/wiki/Commands/voldash.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Volume Dashboard: live cut/fill/net panel between two surfaces (REQ-073). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VOLREPORT] — Insert MTEXT of the last volume report: VOLREPORT [TABLE]

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VOLREPORT details](../resources/wiki/Commands/volreport.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VOLREPORT details](../resources/wiki/Commands/volreport.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Insert MTEXT of the last volume report: VOLREPORT [TABLE]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VOLTABLE] — Insert a TABLE of the last volume report: VOLTABLE

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VOLTABLE details](../resources/wiki/Commands/voltable.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VOLTABLE details](../resources/wiki/Commands/voltable.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Insert a TABLE of the last volume report: VOLTABLE. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VOLUMES] — Cut/fill/net volume between two surfaces: VOLUMES <base>, <comparison>[, <clip id>]

**Aliases to test:** `VOL`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VOLUMES details](../resources/wiki/Commands/volumes.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VOLUMES details](../resources/wiki/Commands/volumes.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`VOL`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Cut/fill/net volume between two surfaces: VOLUMES <base>, <comparison>[, <clip id>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [VOLUMESURFACE] — Create a TIN volume surface: VOLUMESURFACE <name>, <base>, <comparison>

**Aliases to test:** `VOLSURF`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [VOLUMESURFACE details](../resources/wiki/Commands/volumesurface.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [VOLUMESURFACE details](../resources/wiki/Commands/volumesurface.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`VOLSURF`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create a TIN volume surface: VOLUMESURFACE <name>, <base>, <comparison>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [WATERDROP] — Water-drop path: WATERDROP <name>[, <x>, <y>] \| WATERDROP EXTRACT

**Aliases to test:** `WDROP`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [WATERDROP details](../resources/wiki/Commands/waterdrop.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [WATERDROP details](../resources/wiki/Commands/waterdrop.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`WDROP`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Water-drop path: WATERDROP <name>[, <x>, <y>] \| WATERDROP EXTRACT. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [WATERSHED] — Watershed basins: WATERSHED [<name>]

**Aliases to test:** `WSHED`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [WATERSHED details](../resources/wiki/Commands/watershed.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [WATERSHED details](../resources/wiki/Commands/watershed.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`WSHED`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Watershed basins: WATERSHED [<name>]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Paper space and plotting

#### [MSPACE] — Edit the model through the selected viewport (floating model space)

**Aliases to test:** `MS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [MSPACE details](../resources/wiki/Commands/mspace.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [MSPACE details](../resources/wiki/Commands/mspace.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`MS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Edit the model through the selected viewport (floating model space). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [MVIEW] — Rectangular paper-space viewport (two clicks)

**Aliases to test:** `RECTVIEWPORT`, `RECTVP`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [MVIEW details](../resources/wiki/Commands/mview.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [MVIEW details](../resources/wiki/Commands/mview.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`RECTVIEWPORT`, `RECTVP`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Rectangular paper-space viewport (two clicks). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PLOTSCALE] — Set the plot scale

**Aliases to test:** `PSCALE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PLOTSCALE details](../resources/wiki/Commands/plotscale.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PLOTSCALE details](../resources/wiki/Commands/plotscale.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`PSCALE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Set the plot scale. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PSPACE] — Return to paper space from a floating viewport

**Aliases to test:** `PS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PSPACE details](../resources/wiki/Commands/pspace.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PSPACE details](../resources/wiki/Commands/pspace.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`PS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Return to paper space from a floating viewport. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Blocks and attributes

#### [ATTDEF] — Attribute definition (in BEDIT)

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ATTDEF details](../resources/wiki/Commands/attdef.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ATTDEF details](../resources/wiki/Commands/attdef.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Attribute definition (in BEDIT). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ATTEDIT] — Edit attribute values on selected inserts

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ATTEDIT details](../resources/wiki/Commands/attedit.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ATTEDIT details](../resources/wiki/Commands/attedit.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Edit attribute values on selected inserts. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ATTEXT] — Extract attributes to the command log / file

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ATTEXT details](../resources/wiki/Commands/attext.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ATTEXT details](../resources/wiki/Commands/attext.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Extract attributes to the command log / file. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ATTSYNC] — Synchronize attributes from definitions

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ATTSYNC details](../resources/wiki/Commands/attsync.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ATTSYNC details](../resources/wiki/Commands/attsync.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Synchronize attributes from definitions. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BCLOSE] — Close the block editor

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BCLOSE details](../resources/wiki/Commands/bclose.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BCLOSE details](../resources/wiki/Commands/bclose.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Close the block editor. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BEDIT] — Open the block editor: BEDIT <name>

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BEDIT details](../resources/wiki/Commands/bedit.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BEDIT details](../resources/wiki/Commands/bedit.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Open the block editor: BEDIT <name>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BLOCK] — Define a block from the selection: BLOCK <name>, <x>, <y>[, CONVERT\|DELETE\|RETAIN]

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BLOCK details](../resources/wiki/Commands/block.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BLOCK details](../resources/wiki/Commands/block.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Define a block from the selection: BLOCK <name>, <x>, <y>[, CONVERT\|DELETE\|RETAIN]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BLOCKFAV] — Favorite blocks

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BLOCKFAV details](../resources/wiki/Commands/blockfav.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BLOCKFAV details](../resources/wiki/Commands/blockfav.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Favorite blocks. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BLOCKIMPORT] — Import block definitions (.dxf/.dwg/.sat); omit the path to browse

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BLOCKIMPORT details](../resources/wiki/Commands/blockimport.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BLOCKIMPORT details](../resources/wiki/Commands/blockimport.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Import block definitions (.dxf/.dwg/.sat); omit the path to browse. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BLOCKLIB] — List the drawing block library with previews

**Aliases to test:** `BLOCKBROWSER`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BLOCKLIB details](../resources/wiki/Commands/blocklib.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BLOCKLIB details](../resources/wiki/Commands/blocklib.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`BLOCKBROWSER`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** List the drawing block library with previews. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BLOCKLIST] — List block definitions

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BLOCKLIST details](../resources/wiki/Commands/blocklist.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BLOCKLIST details](../resources/wiki/Commands/blocklist.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** List block definitions. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BLOCKMODEL] — Switch to model space

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BLOCKMODEL details](../resources/wiki/Commands/blockmodel.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BLOCKMODEL details](../resources/wiki/Commands/blockmodel.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Switch to model space. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BLOCKPAPER] — Switch to the first paper layout

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BLOCKPAPER details](../resources/wiki/Commands/blockpaper.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BLOCKPAPER details](../resources/wiki/Commands/blockpaper.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Switch to the first paper layout. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BLOCKRECENT] — Recently used blocks

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BLOCKRECENT details](../resources/wiki/Commands/blockrecent.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BLOCKRECENT details](../resources/wiki/Commands/blockrecent.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Recently used blocks. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BLOCKSEARCH] — Search block names

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BLOCKSEARCH details](../resources/wiki/Commands/blocksearch.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BLOCKSEARCH details](../resources/wiki/Commands/blocksearch.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Search block names. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BLOCKSTATS] — Definition statistics

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BLOCKSTATS details](../resources/wiki/Commands/blockstats.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BLOCKSTATS details](../resources/wiki/Commands/blockstats.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Definition statistics. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BSAVE] — Save the block being edited

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BSAVE details](../resources/wiki/Commands/bsave.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BSAVE details](../resources/wiki/Commands/bsave.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Save the block being edited. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [BSAVEAS] — Save the edited block under a new name

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BSAVEAS details](../resources/wiki/Commands/bsaveas.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BSAVEAS details](../resources/wiki/Commands/bsaveas.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Save the edited block under a new name. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [COPYCLIP] — Copy the selection to the CAD clipboard

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [COPYCLIP details](../resources/wiki/Commands/copyclip.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [COPYCLIP details](../resources/wiki/Commands/copyclip.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Copy the selection to the CAD clipboard. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [COPYSEL] — Copy selected inserts: COPYSEL dx, dy

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [COPYSEL details](../resources/wiki/Commands/copysel.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [COPYSEL details](../resources/wiki/Commands/copysel.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Copy selected inserts: COPYSEL dx, dy. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [INSERT] — Insert a block (dialog); or INSERT <name>, <x>, <y>[, sx, sy, rotDeg[, sz, z]]

**Aliases to test:** `I`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [INSERT details](../resources/wiki/Commands/insert.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [INSERT details](../resources/wiki/Commands/insert.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`I`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Insert a block (dialog); or INSERT <name>, <x>, <y>[, sx, sy, rotDeg[, sz, z]]. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [MAKEBLOCK] — Create an empty block definition: MAKEBLOCK <name>

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [MAKEBLOCK details](../resources/wiki/Commands/makeblock.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [MAKEBLOCK details](../resources/wiki/Commands/makeblock.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Create an empty block definition: MAKEBLOCK <name>. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [MIRRORSEL] — Mirror selected inserts: MIRRORSEL x0, y0, x1, y1

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [MIRRORSEL details](../resources/wiki/Commands/mirrorsel.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [MIRRORSEL details](../resources/wiki/Commands/mirrorsel.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Mirror selected inserts: MIRRORSEL x0, y0, x1, y1. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [MKLINE] — Add a line: MKLINE x0, y0, x1, y1

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [MKLINE details](../resources/wiki/Commands/mkline.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [MKLINE details](../resources/wiki/Commands/mkline.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Add a line: MKLINE x0, y0, x1, y1. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [MOVESEL] — Move selected inserts: MOVESEL dx, dy

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [MOVESEL details](../resources/wiki/Commands/movesel.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [MOVESEL details](../resources/wiki/Commands/movesel.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Move selected inserts: MOVESEL dx, dy. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PASTEBLOCK] — Paste clipboard block references: PASTEBLOCK dx, dy

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PASTEBLOCK details](../resources/wiki/Commands/pasteblock.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PASTEBLOCK details](../resources/wiki/Commands/pasteblock.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Paste clipboard block references: PASTEBLOCK dx, dy. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PURGE] — Purge unused block definitions

**Aliases to test:** `-PURGE`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PURGE details](../resources/wiki/Commands/purge.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PURGE details](../resources/wiki/Commands/purge.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`-PURGE`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Purge unused block definitions. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [ROTATESEL] — Rotate selected inserts: ROTATESEL x, y, deg

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ROTATESEL details](../resources/wiki/Commands/rotatesel.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ROTATESEL details](../resources/wiki/Commands/rotatesel.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Rotate selected inserts: ROTATESEL x, y, deg. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SCALESEL] — Scale selected inserts: SCALESEL x, y, factor

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SCALESEL details](../resources/wiki/Commands/scalesel.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SCALESEL details](../resources/wiki/Commands/scalesel.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Scale selected inserts: SCALESEL x, y, factor. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SELBLOCK] — Select the last block reference

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SELBLOCK details](../resources/wiki/Commands/selblock.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SELBLOCK details](../resources/wiki/Commands/selblock.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Select the last block reference. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [SELLINE] — Select the last line

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [SELLINE details](../resources/wiki/Commands/selline.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [SELLINE details](../resources/wiki/Commands/selline.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Select the last line. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [WBLOCK] — Write a block definition to its own .dwg file

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [WBLOCK details](../resources/wiki/Commands/wblock.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [WBLOCK details](../resources/wiki/Commands/wblock.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Write a block definition to its own .dwg file. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Settings and utilities

#### [ELEV] — Elevation new geometry is drawn at (W = world Z 0)

**Aliases to test:** `UCS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [ELEV details](../resources/wiki/Commands/elev.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [ELEV details](../resources/wiki/Commands/elev.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`UCS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Elevation new geometry is drawn at (W = world Z 0). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [HELP] — Show command help

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in Command Reference. Reset the same baseline before testing each alias.

**Steps:**
1. Open Command Reference and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Show command help. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [OPTIONS] — Open the Options dialog

**Aliases to test:** `OP`, `SETTINGS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [OPTIONS details](../resources/wiki/Commands/options.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [OPTIONS details](../resources/wiki/Commands/options.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`OP`, `SETTINGS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Open the Options dialog. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [UNITS] — Drawing units: display precision & angle format

**Aliases to test:** `UN`, `DDUNITS`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [UNITS details](../resources/wiki/Commands/units.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [UNITS details](../resources/wiki/Commands/units.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`UN`, `DDUNITS`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Drawing units: display precision & angle format. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Import and reference geometry

#### [IMPORTMODEL] — Import a glTF/GLB 3D model as reference geometry

**Aliases to test:** `GLTF`, `IMPORT3D`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [IMPORTMODEL details](../resources/wiki/Commands/importmodel.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [IMPORTMODEL details](../resources/wiki/Commands/importmodel.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`GLTF`, `IMPORT3D`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Import a glTF/GLB 3D model as reference geometry. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PDFATTACH] — Attach a PDF underlay

**Aliases to test:** `PA`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PDFATTACH details](../resources/wiki/Commands/pdfattach.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PDFATTACH details](../resources/wiki/Commands/pdfattach.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`PA`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Attach a PDF underlay. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

#### [PDFVIEW] — Open a PDF in the built-in viewer (REQ-387)

**Aliases to test:** `PV`

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [PDFVIEW details](../resources/wiki/Commands/pdfview.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [PDFVIEW details](../resources/wiki/Commands/pdfview.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Reset the baseline, enter each alias (`PV`) one at a time, and complete the same operation.
4. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored.
5. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** Open a PDF in the built-in viewer (REQ-387). The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________

### Diagnostics

#### [BENCH] — REQ-100 frame-budget benchmark: run each accepted and instrumented cost profile with its recorded size

**Aliases to test:** none

**Prerequisites:** Prepare the disposable drawing, selection, file, or setting described in [BENCH details](../resources/wiki/Commands/bench.md). Reset the same baseline before testing each alias.

**Steps:**
1. Open [BENCH details](../resources/wiki/Commands/bench.md) and prepare its smallest valid test input, including the objects or file it requires.
2. Enter the command named in this test heading and follow the linked article prompts/options to complete one valid operation.
3. Compare the result with the expected behavior below. Undo successful edits and confirm the baseline is restored, or restore any setting changed by a query/settings command.
4. If the command is interactive, start it again and cancel at a prompt; if it is one-shot, use a documented invalid value if available. Confirm no unintended partial change remains.

**Expected:** REQ-100 frame-budget benchmark: run each accepted and instrumented cost profile with its recorded size. The command reports a clear refusal for invalid input and leaves no unintended partial change after cancellation or undo.

Result: [ ] PASS  [ ] FAIL  [ ] BLOCKED — Notes: ____________________


## H. Final reconciliation and sign-off

1. Read [`resources/wiki/Command-Reference.md`](../resources/wiki/Command-Reference.md) and reconcile every listed command category against cases A–F. For any supported command/feature with no meaningful coverage here, add a named test case in the findings/addendum before sign-off.
2. Read [`resources/wiki/Known-Limitations.md`](../resources/wiki/Known-Limitations.md). It may be older than the candidate; verify each relevant entry against current requirements and the running app. Confirm known limitations remain accurately disclosed and no checklist expectation treats an unavailable feature as shipped behavior. Record stale manual entries as documentation findings.
3. Read [`resources/wiki/_Sidebar.md`](../resources/wiki/_Sidebar.md). Confirm each user-manual product area has at least one relevant checklist case above; add cases for any missing area.
4. Confirm every applicable case is marked PASS. A failure or blocked case requires a linked issue/task or a release decision recorded under the normal release gate.
5. Complete [`verification/release-checklist.md`](../verification/release-checklist.md) independently; this feature checklist does not authorize or publish a release.

**Uncovered supported commands/features and added test cases:**

| Command/feature | Test case added or reference | Result |
|---|---|---|
| | | |

**Findings / linked issues:**

| ID | Case | Steps/input | Expected vs. observed | Issue/task |
|---|---|---|---|---|
| | | | | |

**Product checklist outcome:** PASS / FAIL / BLOCKED

**Tester sign-off / date:** ____________________

