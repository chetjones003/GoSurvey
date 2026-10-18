# TASK-899 — Qt map, geolocation, and marker workflows

## 1. Authority

- GitHub issue #899, under parent tracker #878.
- Accepted behavior authority: REQ-427 (Qt parity), REQ-357–REQ-364 (drawing settings, geolocation, transformation, markers, map tiles, capture), REQ-084 (unavailable controls), REQ-101 (coordinate storage), REQ-201 (validation and visible failure), and REQ-302 (ribbon behavior).
- Decision: D-2026-10-18-d (2026-10-18). User selected option 1: accept a complete Qt parity requirement. Existing 0.7.4 behavior remains the baseline; USGS is the only map provider. Visual direction is selected from integrated options before finalizing the UI.
- Issue acceptance requires preserving cancel, validation, failure, undo, read-only, full-workflow, coverage, and parity-ledger behavior, plus 2–3 integrated screenshot options for each major affected surface.

## 2. Scope

- Make Qt Drawing Settings usable for Units and Zone, Transformation, and Object Layers.
- Complete the Qt Geolocation contextual workflow: edit/reorient/remove location, mark positions, choose map, capture/pick/remove captured areas, and finish required viewport interactions.
- Preserve existing shared command, map, marker, settings, persistence, and read-only behavior.
- Add focused Qt workflow coverage and update `docs/qt-074-parity-ledger.md`.
- Present visual options for each major affected surface, apply the selected direction, then capture the final meaningful states for user review.

## 3. Files and boundaries

- Expected: `src/qt/RibbonCatalog.hpp`, `src/qt/EditorRibbon.cpp`, `src/qt/EditorDialogs.cpp`, Qt settings/dialog components as required by existing patterns, `tests/QtEditorTests.cpp`, `docs/qt-074-parity-ledger.md`, this task record, and `spec/requirements.md` / `spec/project.md` for the accepted requirement and decision.
- Reuse the existing shared drawing settings, geolocation commands, map/capture services, marker model, persistence and renderer. No new provider, domain model, command implementation, or persistence format.
- Plan check: REQ-357 through REQ-364 explicitly define each underlying workflow, provider choice, validation/failure cases, undo and persistence. REQ-427 scopes the port to native Qt UI and focused workflow evidence. Current Qt already has ribbon entries and a starter geolocation test; issue scope is complete workflow usability, not route presence. Work stays in the Qt UI/test/documentation layer. No architectural change or remaining behavior gap is identified at planning time.

## 4. Plan

- [x] Read repository governance, issue #899, underlying accepted requirements, Qt routes/tests, and parity ledger.
- [x] Record D-2026-10-18-d and accept REQ-427 based on the user's option 1 decision.
- [x] Verify the plan against REQ-357–REQ-364, architecture boundaries, issue scope, and existing Qt implementation.
- [x] Inventory each 0.7.4 workflow against Qt and identify the major screenshot surfaces/states.
- [x] Present three integrated visual options for Drawing Settings and the Geolocation/map surface; user selected C for both on 2026-10-18.
- [x] Implement the selected Qt workflows using existing services and native UI patterns.
- [x] Add focused coverage for the settings apply, layer lock, provider selection, capture start/cancel and units/scale apply/undo paths.
- [x] Update the parity ledger with routes, test evidence, and remaining gaps.
- [x] Build the Qt target, capture final affected surfaces/states under `build/artifacts/`, and report the verification result.
- [ ] Obtain user approval of final screenshots before marking this Qt UI task complete.

## 5. Implementation log

- 2026-10-18 — plan verified; current Qt ribbon entries exist, but the issue requires native complete workflows. No code changes made before the user's visual-direction selection.
- 2026-10-18 — presented options A/B/C for the Drawing Settings dialog and Geolocation/map surface. User selected C for both: left navigation rail for Drawing Settings; map task panel for Geolocation. These are the chosen visual directions.
- 2026-10-18 — implemented the section-based Drawing Settings workflow, including coordinate-system catalog, transform picking, object layer picker/modifier/lock controls, and staged apply/cancel. Routed the Qt viewport through the shared online map/tile service and added the Map task dock for providers and capture operations. Added Qt position/geographic marker overlays.
- 2026-10-18 — added `issue899DrawingSettingsAndMapTaskPanelUseSharedWorkflows`; focused Qt runs passed (4 tests including the existing units apply/undo workflow). The Qt target builds. Final captures are `qt-899-settings-units-zone.png`, `qt-899-settings-transformation.png`, `qt-899-settings-object-layers.png`, `qt-899-map-task-panel.png`, `qt-899-map-task-panel-active.png`, and `qt-899-map-capture-prompt.png` under `build/artifacts/Release/`.

## 6. Verification result

- Focused Qt tests passed and the Qt release target built. Full `qt.editor` reported 109 passed and one pre-existing command-chrome focus assertion failed (`tabsAppearInitiallyAndTypingRoutesFromEditorChrome`). The #899 workflow test passed in the full run. User review of the final captures is pending, as required by the repository's Qt UI workflow.
