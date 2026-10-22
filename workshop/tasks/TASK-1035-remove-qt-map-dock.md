# TASK-1035 — Remove the Qt Map dock and keep geolocation in the ribbon

- Type:    feature
- Status:  self-verify
- Opened:  2026-10-22
- Owner:   Codex

## 1. Authority
- Goal:         No numbered GOAL is defined in `spec/project.md`; project purpose is the applicable project direction.
- Requirements: REQ-358, REQ-359, REQ-363, REQ-364, REQ-427 (accepted)
- Constraints:  None specific to this UI change; preserve `spec/architecture.md` ownership and existing map behavior.
- Acceptance:
  - REQ-359: the Geolocation contextual tab is visible while the active drawing is geolocated and hidden when it is not; it does not steal focus when it appears.
  - REQ-363: Map Off or an un-geolocated drawing makes no tile request; map choice remains per-drawing and Map Off draws nothing.
  - REQ-427: Qt workflows use native surfaces and existing shared services; preserve REQ-357 through REQ-364 semantics and do not duplicate state in Qt.
  - Issue #1035: no Map dock is created/shown; location, marker, online map, and capture controls remain available through the Geolocation contextual ribbon as their sole UI location.
- Owning subsystem: Qt UI (`src/qt/`); shared settings, map, capture, marker, command, rendering, and persistence owners retain behavior ownership.

## 2. Scope
- In scope: remove the Map dock and its duplicate controls; keep and verify the contextual ribbon routes; add width-adaptive tab navigation and ribbon panel fitting; load coordinate-system dictionaries for Drawing Settings; add DRAWINGSETTINGS to File; keep coordinate dropdowns compact and opening down from their fields; start maximized by default while retaining a visible resize grip when restored; keep interactive resizing smooth; update focused Qt workflow tests; amend REQ-427 and record the issue's decision.
- Out of scope: changes to map providers, map/capture semantics, persistence, domain state, or unrelated Qt surfaces.
- Smallest change: remove Map dock construction and refresh-time widget synchronization; preserve the end-of-list contextual tab order and provide a hamburger tab menu when visible tabs do not fit.

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| — | None. Issue #1035 explicitly directs the UI entry point and required REQ-427 update. | — | — |

## 5. Assumptions
None.

## 6. Plan
- Approach: remove the standalone panel/dock, retain only existing ribbon commands, add adaptive tab navigation and panel collapse, repair Drawing Settings entry/dictionary loading and dropdown placement, and show Qt's size grip while the main window is restored.
- Files/functions to touch: `src/qt/EditorWindow.cpp` (`BuildDocks`, `Refresh`); `src/qt/EditorDialogs.cpp` (`ShowUnits`); `src/qt/EditorChrome.cpp`/`EditorWindow.cpp` (window resize affordance); `tests/QtEditorTests.cpp`; `spec/requirements.md`; `spec/project.md`; this task log.
- Test approach: Qt workflow seam verifies no dock, tab visibility and access, map selection/capture; Drawing Settings checks File entry, dictionary categories, dropdown geometry and resize grip visibility in restored/maximized states; existing map tests cover Map Off/no-geolocation requests.
- Steps:
  - [x] Record the narrow REQ-427 amendment and decision for issue #1035.
  - [x] Update the focused Qt test to prove controls remain accessible through the ribbon and the dock is absent.
  - [x] Remove Map dock construction and obsolete refresh synchronization.
  - [x] Run focused Qt verification and inspect the final diff.
  - [x] Capture and present the affected workspace state for user approval before commit.
  - [ ] Commit after visual approval.

## 7. Workflow-specific notes
- Feature: issue #1035 supplies the behavior; use the existing `GeolocationCtx` ribbon as the test seam.

## 8. Implementation log
- 2026-10-22 — issue #1035 fetched; existing accepted requirements and Qt test seam identified. Plan recorded before implementation.
- 2026-10-22 — regression test failed first because `mapTaskDock` existed; after removing the panel, the test passed and confirmed the contextual ribbon routes still select a map and start/cancel Pick Area.
- 2026-10-22 — initialized the bundled coordinate dictionary in the Qt test and selected its full Texas zone code so the capture prompt screenshot represents the valid geolocated workflow.
- 2026-10-22 — Release build passed without new warnings; final full CTest passed 191/191, including all six Qt shards and the shared REQ-363 no-fetch test. `git diff --check` passed.
- 2026-10-22 — captured and presented un-geolocated, geolocated, and capture-prompt workspace screenshots under `build/artifacts/Release/`; awaiting user visual approval before commit.
- 2026-10-22 — follow-up to the user's ribbon-tab feedback briefly moved Geolocation after Survey and before Modeling; the user rejected that placement. It was reverted, so contextual tabs remain appended at the end as REQ-359 requires.
- 2026-10-22 — user clarified contextual tabs must remain appended and requested a hamburger menu at compact widths, with the tabs and ribbon adapting dynamically. Added a hamburger tab menu when visible tabs cannot fit, hides the shared layer shortcuts when they compete for width, and keeps the existing ribbon panel collapse menus. Focused Qt coverage checks compact/wide transitions and selects the final Geolocation item from the menu. Release build and focused Qt test passed (3 cases); `git diff --check` passed. Latest screenshots are captured under `build/artifacts/Release/`; final code review and visual approval are pending.
- 2026-10-22 — user reported missing `DRAWINGSETTINGS` in File and broken coordinate categories. Added File → Drawing Settings, load the bundled CS-MAP dictionaries before creating the dialog, and report dictionary errors while disabling only the coordinate-system group. Added a focused Qt test that forces a dictionary-load failure, reopens through the File menu, then loads categories and selects `HARN/TX.TX-C`.
- 2026-10-22 — user requested downward compact coordinate dropdowns and a visible restored-window resize handle. Coordinate selectors now cap at eight visible rows (220 px view); the regression test checks left alignment, downward placement (allowing the one-pixel native frame overlap), and popup height, and captures both open lists. The user's live screenshot showed Qt still placing one popup incorrectly, so a shared `CoordinateSystemComboBox` now explicitly aligns the popup below its selector and limits it to available space. Enabled the QMainWindow status bar size grip; the title-bar test verifies it appears in the restored state and hides while maximized. Added captures for both states under `build/artifacts/qt-editor/manual/shard-5/artifacts/`.
- 2026-10-22 — full Release build passed. All six Qt shards have passed across the initial run and the focused rerun after the popup placement fix (shard 5 was rerun). The earlier first run's sole failure was the dropdown test's overly strict one-pixel border comparison; it also left the modal dialog open on failure, so a close-on-failure guard was added. Final shard 5 passed, `git diff --check` passed. Final Standards and Spec reviews found no actionable issues. Visual approval remains pending.
- 2026-10-22 — after the user's screenshot exposed the native popup still appearing oversized, added explicit popup geometry correction. Focused coordinate test and shard 5 pass, Release build passes, refreshed captures confirm the corrected layout. Standards and Spec reviews found no actionable issues; visual approval remains pending.
- 2026-10-22 — user reported hover-edge auto-scroll in the coordinate lists and that the restored-window resize affordance was still too hard to find. Disabled `QAbstractItemView` auto-scroll on both coordinate selector lists and enlarged the status-bar `QSizeGrip` to 20×20; added regression assertions and updated REQ-427. Release build and the two focused Qt tests pass (4 cases), `git diff --check` passes, and the refreshed restored-window capture shows the larger grip. The full CTest regex found no registered tests in this checkout, so the focused Qt test executable was run directly on the hidden desktop. The application was relaunched from the updated Release build; visual approval remains pending.
- 2026-10-22 — user confirmed both symptoms persisted. Qt's menu-style combo popup creates separate top/bottom hover-scroller widgets, which `QAbstractItemView::setAutoScroll(false)` does not control; changed the coordinate selectors to a standard list popup via a local `QProxyStyle` and retained the regular scrollbar. The frameless Windows HWND lacked `WS_THICKFRAME`, so its existing `WM_NCHITTEST` edge zones and Qt `QSizeGrip` could not start system resizing; restore that style on show and assert it in the window test. Release build passed; both focused GUI tests passed (4 cases); `git diff --check` passed. Current app instance was relaunched from the new build for hands-on confirmation.
- 2026-10-22 — user reported the custom title bar was clipped and resizing had severe lag spikes. Reverted the `WS_THICKFRAME` mutation, which introduced the clipped native frame; replaced native-frame resizing with eight frameless app-owned edge/corner handles (20px bottom-right grip) and 24ms coalesced geometry updates. `resizeEvent` now changes start-page layout only at the breakpoint and defers recent-list refresh until resizing settles. Added restored/maximized visibility and actual drag-resize coverage, plus checks that the frame stays frameless and title bar remains in-window. The focused settings/window tests passed (4 cases); screenshots captured under `build/artifacts/issue1035-resize-r3/`; visual approval pending.
- 2026-10-22 — user reported drawing stutter and compact ribbon dropdowns failing at minimum width. Root cause for stutter: `EditorViewport::paintGL` calls `SetSize` every resize step; `EnsureFramebuffer` destroyed and allocated color/depth targets for every exact dimension change, also rebuilding the multisample targets. Renderer allocations now grow in 256px buckets and retain capacity while drawing uses the current viewport width/height. Compact panel buttons now have an explicit 40×24 bottom-center arrow hit area and open from the full button body; a regression test shrinks from normal to minimum width and opens every visible compact/More menu from both targets. Release build passed and focused compact-menu, title-bar/resize, and coordinate-system tests passed (3 cases each). Updated screenshots are in `build/artifacts/issue1035-resize-r5/`; visual approval pending.

## 9. Self-verification
- [x] build-project — PASS (Release build; no changed-file warnings; generated artifacts confined to `build/`)
- [x] architecture-review — PASS (Qt presentation change only; shared domain and map services retain ownership)
- [x] code-review — prior popup geometry received Standards and Spec review with no findings; this follow-up was checked against Qt 6.8.3 and Win32 behavior and has focused regression assertions.
- [x] dependency-audit — n/a
- [x] performance-review — PASS (framebuffer and multisample buffers are reused across viewport size changes within 256px capacity buckets)
- [x] testing — PASS (focused compact ribbon, resize/title-bar and coordinate-system GUI checks; each reported 3 cases passed)

## 10. Verification result
- Submitted: 2026-10-22
- Verdict: pending user confirmation of resize smoothness and compact dropdown behavior
- Findings: compact menu targets open after shrinking from normal width; viewport render targets are reused while resizing. The app remains frameless so the custom title bar stays intact.

## 11. Outcome
- Requirements satisfied: REQ-358, REQ-359, REQ-363, REQ-364, REQ-427 and issue #1035 acceptance met pending user visual approval
- Tests added: `issue899DrawingSettingsAndGeolocationRibbonUseSharedWorkflows`, `drawingSettingsFileMenuLoadsCoordinateSystemCategories`, `compactRibbonDropdownButtonsOpenAtMinimumWindowWidth`; restored/maximized and drag-resize assertions in `customTitleBarAndFloatingCommandLineWork`; existing REQ-363 zero-fetch regression reused
- Refactors: none
- Docs updated: `spec/requirements.md`, `spec/project.md`, `docs/qt-074-parity-ledger.md`
- Done: pending

## 12. Follow-up: resize rendering, compact menus, and Windows snap
- Observed: drawing content still stutters through manual resize; compact ribbon dropdowns still fail in live use; dragging the title bar to screen edges no longer invokes Windows snap.
- Requirement authority: REQ-427 acceptance clause 4 (usable native Qt interaction, dynamic ribbon access, restored-window resizing with drawing visible and correctly updated at final dimensions). No SPEC change is proposed.
- Root-cause evidence:
  - `EditorViewport::paintGL` rebuilds preview/highlight geometry and calls `RenderScene` for every size event. `WindowResizeHandle::ApplyResize` updates geometry every 24 ms. Reusing allocations does not avoid those full scene builds and draw calls.
  - The compact test observes `QMenu::aboutToShow` after synthetic clicks, but does not select a command from the popup. The current `MenuButtonPopup` splits pointer handling between the main body and a small style-dependent arrow region.
  - `EditorWindow` is frameless and has no `WS_THICKFRAME`; resize is performed by app-owned widgets calling `setGeometry`. The title strip uses Qt `startSystemMove`, which leaves Windows without a native caption hit-test for shell snap behavior.
- Plan:
  - Add a viewport resize preview that scales the last completed frame during live resizing, then performs a full render after resize settles. Allocate the renderer texture/depth targets to their recorded capacity so the cached image remains valid across growth within a capacity bucket.
  - Use `InstantPopup` for collapsed ribbon and More buttons so every point on the control opens its menu. Strengthen the GUI regression to select a real command from a compact popup.
  - Send the standard Windows non-client caption-drag gesture for title-strip drags on Windows; retain Qt `startSystemMove` on other platforms. Preserve custom title-bar geometry and app-owned resize handles.
- Regression coverage: verify the renderer keeps the previous frame dimensions during the resize preview and reaches final dimensions after settling; verify compact menu command selection; verify the Windows native title drag message path.
- Architectural boundary: Qt presentation/render scheduling and existing Win32 window integration only; no new layer, dependency, state owner, or domain behavior.
- Follow-up implementation (2026-10-22): the previous framebuffer-only change still rebuilt CAD preview/highlight vectors and issued the full scene draw on every resize step. The viewport now scales the last completed model frame while an app-owned edge/corner resize handle is held and performs the full render after release. Renderer textures and depth storage now match their declared 256-pixel allocation capacity (the prior code recorded a rounded capacity while allocating only the first exact viewport size). Compact ribbon buttons use whole-surface instant popups; the minimum-width regression now selects the Line command from the Draw menu. On Windows, a drag on the unobstructed custom title strip sends `WM_NCLBUTTONDOWN/HTCAPTION` so the OS can apply its ordinary snap behavior; other platforms retain Qt `startSystemMove`.
- Focused verification: Release build PASS. `customTitleBarAndFloatingCommandLineWork`, `compactRibbonDropdownButtonsOpenAtMinimumWindowWidth`, and `drawingSettingsFileMenuLoadsCoordinateSystemCategories` each PASS (3 Qt cases including setup/cleanup). The resize test captures the scaled drawing during a live handle drag, then asserts the final renderer dimensions after release. `git diff --check` PASS. Updated screenshots are under `build/artifacts/Release/qt-1035-*`.
- Status: implementation and screenshots ready for user review; a hands-on check of Windows snap-to-edge/top remains part of the visual feedback step.

## 13. Follow-up: use the supplied Qt splash background
- Observed: the startup splash used an older background image; launch location affected whether the requested asset was found.
- Root cause: `main.cpp` requested `splash-bg.png`, while the supplied Qt art is `resources/qt-splash-bg.png`; CMake staged only the former. `QtAsset` could mask this packaging gap when launched from the repository root.
- Change: startup now loads `qt-splash-bg.png`; CMake checks and stages that exact file beside the executable; the splash regression test decodes the same packaged asset.
- Verification: Release build PASS. `issue1028SplashProgressBarAndPhaseLabelAdvanceDuringTheDisplayTime` PASS (3 Qt cases including setup/cleanup), executed with a separate artifacts working directory. Source and staged image SHA-256 values match. Captured splash frame under `build/artifacts/Release/qt-1028/` shows the intended blue contour artwork.
- Status: pending user visual review.

## 14. Follow-up: avoid resizing the CAD OpenGL surface on every drag step
- Baseline probe: in the hidden-desktop test harness, synchronous Qt layout during a resize measured about 3–6 ms and a direct viewport paint about 1–2 ms; the event-pump timings were 70–120 ms and did not isolate actual paint cost. This confirms the scene paint itself is not the long pole, but the hidden harness's full event-pump latency is not a reliable user-visible frame-time metric.
- Change: at entry to a model-space window size loop, capture the already-rendered viewport once, hold the `QOpenGLWidget` at its original size, disable its updates, and show a mouse-transparent pixmap overlay scaled to the resized drawing area. At exit, remove the overlay, restore the widget's size constraints and run one full render at the final size. This prevents the GL surface and its backing framebuffer from being resized and repainted for every intermediate window size.
- Regression: `customTitleBarAndFloatingCommandLineWork` now asserts that during 30 resize steps the viewport remains at its captured size while the preview overlay follows the model-page bounds, then verifies the final render matches the final viewport size. The check failed before the change (missing overlay) and passes after it.
- Status: implementation is available for user review; interactive smoothness should be confirmed on the normal desktop because the hidden test runner's event-pump timing does not represent physical resize presentation.

## 15. Follow-up: make the unmaximized resize grip visible
- User feedback: the resize handles were no longer visible.
- Inspection: the app had reopened maximized; Windows hides resize grips in that state. When restored, the native window returned `HTTOPLEFT`, `HTTOPRIGHT`, `HTBOTTOMLEFT`, `HTBOTTOMRIGHT`, `HTTOP`, `HTBOTTOM`, `HTLEFT` and `HTRIGHT` for the corresponding hit-test locations.
- Change: replaced the subtle default `QSizeGrip` rendering with a 24 px visible corner grip; it hides while maximized/full-screen and reappears when restored. Native edge/corner resize hit-testing remains enabled.
- Verification: Release build PASS. `customTitleBarAndFloatingCommandLineWork` PASS and now checks all eight native resize hit zones, visible grip dimensions, and grip visibility across maximize/restore.
- Status: updated restored-window and resize-preview screenshots are ready for review.

## 16. Follow-up: reduce live-resize work and maximize on startup
- User direction: start maximized, keep the restored resize grip, and remove recurring stutter during interactive resizing.
- Regression loop: `compactRibbonDropdownButtonsOpenAtMinimumWindowWidth` now holds a `QPointer` to the hamburger tab action, changes width within compact mode, and asserts the action remains alive. It failed before the fix because each resize rebuilt the menu.
- Change: cache ribbon panel priority order; rebuild overflow menu actions only when collapsed panel membership changes; rebuild hamburger actions only when visible tabs change; avoid reapplying unchanged ribbon visibility; stop raising the resize-preview overlay on every resize event; remove the resize-triggered recent-file refresh timer; call `showMaximized()` on application startup.
- Verification: Release build PASS. The compact-ribbon regression passes; `customTitleBarAndFloatingCommandLineWork` passes with resize hit zones, restored grip, maximize/restore visibility, and resize-preview checks. Running GoSurvey confirms Windows reports the startup window maximized. `git diff --check` passes.
- Status: user approved this state and requested a PR to `beta`.

## 17. CI follow-up for PR #1055
- CI on `0b7d0f8f`: the full Release job had one failed assertion in `drawingSettingsFileMenuLoadsCoordinateSystemCategories` because the dropdown popup was not visible at the first event-loop pass; `msvc-analyze` reported C6011 for the Layers button null check.
- Change: make the test wait up to two seconds for each popup window to become visible, and split the required Layers button/menu checks into explicit fatal-and-return branches before dereferencing either pointer.
- Verification: Release build PASS; the focused coordinate-system Qt test passed three consecutive hidden-desktop runs (3 cases per run); `git diff --check` passes. New CI run will verify the analyzer and full suite.
- Status: follow-up is ready for review and CI monitoring.
