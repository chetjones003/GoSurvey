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
- This is workflow-level acceptance coverage, designed to be completed in a practical manual session. Individual commands, options, and file-format edges do not each have separate cases. During sign-off, reconcile the Command Reference and manual navigation against the workflows below and add a targeted workflow case only when a meaningful product flow is otherwise uncovered. This checklist complements focused command tests and does not replace them.
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

1. Save the drawing as a `.dwg` file to a new path.
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

## G. Workflow coverage reconciliation and sign-off

Use the current Command Reference and manual navigation as a gap check against workflows A–F. Do not execute every command as a separate case. If a supported user workflow is absent, record the gap below and add one focused workflow test with prerequisites, steps, and an observable expected result.

1. Read [`resources/wiki/Command-Reference.md`](../resources/wiki/Command-Reference.md) and reconcile command categories against workflows A–F. Identify missing end-to-end workflows; do not create one test per command.
2. Read [`resources/wiki/Known-Limitations.md`](../resources/wiki/Known-Limitations.md). It may be older than the candidate; verify each relevant entry against current requirements and the running app. Confirm known limitations remain accurately disclosed and no checklist expectation treats an unavailable feature as shipped behavior. Record stale manual entries as documentation findings.
3. Read [`resources/wiki/_Sidebar.md`](../resources/wiki/_Sidebar.md). Confirm each user-manual product area has at least one relevant checklist case above; add cases for any missing area.
4. Confirm every applicable case is marked PASS. A failure or blocked case requires a linked issue/task or a release decision recorded under the normal release gate.
5. Complete [`verification/release-checklist.md`](../verification/release-checklist.md) independently; this feature checklist does not authorize or publish a release.

**Uncovered workflows and added test cases:**

| Uncovered workflow | Test case added or reference | Result |
|---|---|---|
| | | |

**Findings / linked issues:**

| ID | Case | Steps/input | Expected vs. observed | Issue/task |
|---|---|---|---|---|
| | | | | |

**Product checklist outcome:** PASS / FAIL / BLOCKED

**Tester sign-off / date:** ____________________
