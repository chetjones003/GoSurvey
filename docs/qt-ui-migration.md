# Qt / Win32 migration status

Branch: `test/win32UI`. Authorized by the user on 2026-10-16.

This is an executable migration baseline, **not a production replacement** for
GoSurvey. The existing executable remains available during parity work.

## Ongoing parity work

The user requested autonomous iteration to full production parity. Fetched and
merged `origin/master` at `90aa196d34692562effa18502c9b8ad0197e9e80`.
`tools/qt-parity.ps1` extracts its 190 registered commands and 40 window flags
into `build/qt-parity.json`. Source references do not count as verified parity.

Build the native regression tests with `build-qt.bat GoSurveyQtTests`, then run
`ctest --test-dir build -R '^qt.editor$'`. `ctest` runs them on a private invisible desktop
(`tools/run-qt-tests-hidden.ps1`): no windows appear on screen, focus is not stolen, and a hung run is
killed after its timeout. Run `build/GoSurveyQtTests.exe` directly to watch them on screen, or pass
test function names to the script to run only those.
They exercise lossless adjustment sessions, failures without mutation, worker
solves, result acceptance, drawing commits and undo, network controls/elevations,
unsaved-close cancellation, command input, camera pan, layers, units, PDF annotation
Save As/source preservation, dimension strokes, and rich text colors.

Domain regressions found during this gate were repaired: FBK closing observations
now share the start station ID, malformed profile arc endpoints are rejected,
and modern DWG layer colors retain their palette RGB. Tests were also updated for
R2018 Unicode decoding, GSTC column alignment, and post-save fixture preconditions.

## Build and launch

Run `build-qt.bat` from the repository. It initializes the installed MSVC environment,
configures the existing Release build with `GOSURVEY_BUILD_QT=ON`, and builds
`build/GoSurveyQt.exe`. The default kit is `C:\Qt\6.8.3\msvc2022_64`.
Set `GOSURVEY_QT_ROOT` to use another installed compatible kit.

Alternatively configure with `-DGOSURVEY_BUILD_QT=ON` and
`-DCMAKE_PREFIX_PATH=<installed-kit>`, then build target `GoSurveyQt`.
`windeployqt` from that kit deploys runtime plugins beside the executable.
The Qt option defaults to OFF, so existing build workflows do not require Qt.

The current appearance uses Qt Fusion with a compact neutral-gray CAD treatment:
flat grouped command tools, restrained borders, gray dock/header gradients, dense
property rows, Segoe UI and a black drawing viewport. Logo menu > View > Appearance
switches between Neutral gray and Classic light without reopening the drawing;
the choice is saved. `--light-style` starts with the light appearance. The title-bar/ribbon/dock/
floating-command layout is retained. `--default-style` remains an accepted alias.
The current comparison build is at `build/ribbon-v072/GoSurveyQt.exe`; configure
`GOSURVEY_QT_RUNTIME_DIR` to select a separate preview output directory, or set it
empty to restore the normal `build/GoSurveyQt.exe` output. Existing preview windows
can stay open while another output directory is rebuilt.

The application uses Qt's Windows platform plugin and Win32 top-level windows
with a custom Qt title bar; Qt Widgets own all presentation and input. The Autodesk-inspired
chrome uses the repository's existing PNG icons, Segoe UI, ribbon tabs, command
line, drawing tabs, dockable Toolspace and Properties, and a status bar.

## Current coverage

The release-specific beta 0.7.4 comparison, including command routes, dialogs,
workflow evidence, and linked open gaps, is maintained in the
[0.7.4 Qt parity ledger](qt-074-parity-ledger.md).

| Area | Implemented | Remaining parity work |
|---|---|---|
| Startup | Splash using existing artwork; Start page with recent drawing/project cards and thumbnails; Markdown What's New window with version-based display policy; REQ-091 sign-in choice and saved skip preference | Update/telemetry/crash-report wiring |
| Editor chrome | Custom title bar with logo application menu, integrated quick access, window controls and native resize/work-area handling; v0.7.2 ribbon catalog, responsive panels and contextual tabs; dock layout persistence; viewport-floating command line with draggable position/resizable width, fading history, F2 console, Ctrl+9 and clickable command hints | Remaining native dialog adapters, advanced contextual widget parity, remaining shortcut parity and multi-monitor/DPI checks |
| Documents | New/open DWG, save/save as, document switching, undo/redo, dirty markers, save/discard/cancel on close; successful opens/saves update the recent drawings list | Export-loss disclosures, full import/export UI, template workflow |
| Viewport | Existing renderer, point-cloud inputs, dimension strokes, rich MTEXT wrapping/attachment/colors, SHX stroke adapter | Exact graphics parity, paper-space rendering, tables, map tiles, gizmo/section overlays, annotation scale/isolation and perspective verification |
| Input | Shared command dispatch, basic picking, Shift selection, two-corner selection boxes, object snaps, UCS construction-plane intersection, wheel zoom, middle-button pan/orbit, clickable cube faces and orientation controls | Full gesture and selection-cycle parity, grip editing, UCS-aware ORTHO/POLAR mouse constraints, advanced 3D selection, keyboard focus routing and transient tool UI |
| Properties/Toolspace | Drawing navigation; point/surface/block inventory; selected layer/color edits; native layer manager and units/scale dialog; drawing/project point grids; Surface Create / Properties / Surfaces manager / Surface Style dialogs, the Toolspace surface and style menus and the read-only Surface properties section (#893) | Complete geometry properties, context menus |
| Survey points | Modeless creation controls, viewport placement, existing-marker selection, numbering and duplicate policies, validated/undoable JSON load and staged save; read-only protection; sortable/filterable drawing/project grids with source/group/visibility columns, edits, selection, database number conflict resolution and local undo; native CSV/TXT import/export with full-file validation, preview and column/header controls; generated report tabs with copy/save | full point-marker visibility/style parity, other project dialogs |
| Projects | Native create/open, lock acquisition, stale-lock detection and the read-only / take-over / cancel choice, join detection on drawing open, shared point sync, settings enforcement, tracked-file restoration/save, shared-point conflict/delete/hide/cancel prompts, Copy / Link / Cancel attach question (point cloud import, Add PDF to project), cross-project/unit/coordinate clipboard prompts with convert/paste/cancel choices, point-save checks on close, lock release | Project settings UI; add/convert drawing; health/pack/turnover UI |
| PDF viewer | Async page rendering, navigation/zoom, text/line/rectangle/ellipse annotations, scaled measurements, scale presets, undo/redo, Save As, close protection, page extraction | Search/thumbnails, calibration/checks, snapping, coordinate extraction, compare/audit, underlay attachment, editable persisted annotations, rotated-page/high-DPI parity, async file operations |
| Adjustment | Lossless `.gstrav` load/save, command text, raw-data imports, async solve, instrument weights, result acceptance, station commits, report TXT/RTF/PDF export, unsaved-close protection | Full form/raw-set editor, complete project settings, plotting, cancellation, merge-import and command-editor parity |

Commands that request an existing ImGui-only modal still need a Qt adapter. Sharing
the command dispatcher does not establish UI parity for those commands.
Native Quick Select now exposes object/property filters, include/exclude, current
selection scope and append through the shared selection service. Numeric input
is validated before applying; Cancel preserves the selection and saved filter.
Native command typing routes from editor controls and floating docks to command
entry while preserving editable fields and dialogs. Tabs reserve space before
the first viewport paint. Named orientations are under View rather than a toolbar
row. History, autocomplete and Escape are connected; pan/zoom
uses the camera plane, and completed surface/cloud workers are serviced by Qt's
event loop. Exact annotation and all selection/gesture parity remain release gates.

## Version 0.7.2 ribbon inventory

The native ribbon now uses `src/qt/RibbonCatalog.hpp`, audited against the local
`v0.7.2` tag's `CadUi.cpp` and `ModelingRibbon.hpp`. Run
`python tools/audit-qt-ribbon.py`: all 268 audited release button entries are
represented, with 280 native entries including style-manager accessors and the
previous Qt point, PDF-viewer and modify controls. Every icon resolves to the
existing `resources/icons` library. The legacy vector-only survey-data pin uses
Point Style artwork; the original text-only block-close button uses Block Editor
artwork. No new bitmap artwork was generated.

Home, Insert, Annotate, View, Manage, Output, Survey and Modeling use their release
panels. Manage retains its release placeholder. Model and layout tabs sit below
the drawing viewport; layout tabs support creation and a rename/delete context menu.
The sheet renderer draws paper geometry and tables, and composites the shared CAD
renderer through each saved layout viewport. Contextual Surface, Survey Point,
Point Cloud, Block Editor, Geolocation, PDF Underlay and Hatch pages follow editor
state; Layout tools appear in paper space. Selection contexts activate on entry
and restore the previous normal tab on exit; geolocation does not take focus.
Three-row panels collapse to native menus, with More overflow at narrower widths.
Expanded and collapsed presentations share QActions. Live selectors remain
available in collapsed panels, and the shared Layers/color panel stays beside
every tab. Menus include surface data/edit/extract, object isolation, fitting part
types, map providers/capture and geographic marker choices.

Native selectors cover visual style/projection/FOV, saved views, UCS presets/names,
text style, layer/color, nominal pipe size, point-cloud size/LOD/color and centerline
fit parameters. Pipe Run starts at the selected size, and the size control locks
during a run. Hatch controls edit creation defaults or selected regions; selected
changes are undoable and reject writes to read-only projects.

**Inventory coverage is not workflow parity.** Release placeholders remain disabled.
Buttons whose view/style/group/block/page-setup/batch-plot dialogs still
lack native adapters are present but explicitly disabled with a tooltip, rather
than silently setting an unserviced UI flag. Their shared routes remain identified
in the catalog/router for later migration. Settings currently opens the native
units dialog; the complete settings workflow remains outstanding. Existing domain,
rendering, advanced PDF/layout, accessibility and packaging gates still apply.

## Dependency boundary

`src/qt/` does not call ImGui or initialize an ImGui context. `QOpenGLWidget` owns
the context; the existing renderer's color texture is copied into Qt's framebuffer.
Context destruction and viewport destruction release renderer resources while
the context is current, following [Qt's QOpenGLWidget lifecycle](https://doc.qt.io/qt-6/qopenglwidget.html).

Legacy font loading was moved from `CadCommands.cpp` into
`src/ui/LegacyApplicationFonts.cpp`. The shared domain library **still links
ImGui core**, because legacy font measurement, SHX draw-list paths and ribbon
measurement still reference it. Those dependencies must be extracted before an
ImGui-free binary can be claimed. The Qt target no longer compiles AppIcon.cpp or
links GLFW; icons and windows are managed by Qt.

## Latest verified iteration

- Qt executable and the existing GoSurvey executable build successfully.
- `GoSurveyQtTests`: 36 passed, 0 failed (34 workflow/render tests plus setup/teardown).
- `GoSurveyTests`: all 1,632 cases / 9,201,236 assertions pass.
- `GoSurveySnapTests`: all 831 cases / 9,511 assertions pass.
- Domain suites were run from `build/` outside the sandbox because GPU registry
  tests and the temporary AppData block-library fixture need that access.
- These green gates do not establish full UI parity. The outstanding workflow,
  graphics, dependency-removal and packaging gates above remain open.

Survey JSON version 1 saves drawing-local double coordinates with `originX` and
`originY`, retaining world positions when loaded into another drawing. It also
retains raw field descriptions. Unversioned files without origin metadata keep
the legacy local-coordinate interpretation. Invalid files leave points, labels,
revision and undo unchanged; successful replacements are one undo step. Save
serializes first, writes/closes a temporary sibling and renames it over the
destination. Tests cover Unicode paths, existing-file replacement, failed final
replacement cleanup, malformed/duplicate/out-of-range data, and label restoration.

Native point-file dialogs now route `IMPORTPOINTS` / `EXPORTPOINTS` to the shared
CSV engine with column/header controls, debounced preview, invalid-row confirmation,
read-only protection, and overwrite confirmation. Validation scans the complete file
rather than a 256 KB prefix. Imports stage rows before mutation and create one undo
snapshot; elevations retain double precision and UTF-8 BOM headers are supported.
Exports stage, flush and close a sibling file before replacement and remove failed
staging files. Generated survey reports open in a native dock with tabs, clipboard
copy and safe Save As. View > Survey reports reopens it. Tests cover native file
flows, Unicode paths, undo/redo, full-file counts, invalid exports, failed final
replacement cleanup, read-only rejection, report copy, blocked paste cancellation,
and Copy/Paste menu routing plus clipboard unit conversion before placement. These checks do not establish all
CSV dialect or project/clipboard parity.

## Initial baseline verification (superseded by the iteration above)

- `build-qt.bat`: passed with installed Qt 6.8.3 and MSVC x64.
- `build.bat release --target GoSurvey GoSurveyTests GoSurveySnapTests`: passed.
- `GoSurveyQt.exe --smoke-test`: passed. Checks renderer initialization, LINE,
  undo/redo, document isolation and restoration, a DWG round-trip, generated PDF
  open/render, and a closed-loop solve through the Qt adjustment editor and tables.
  Saves `build/qt-editor-smoke.png`; inspected the rendered editor window.
- Full existing tests, executed from `build/`: GoSurveyTests reported 1626/1632
  cases passing in the sandbox; three of the six failing cases were GPU registry
  access. Rerunning `[gpu]` outside the sandbox passed all 3 cases / 14 assertions.
  Remaining failures: two traverse cases and one B-rep case.
- GoSurveySnapTests from `build/`: 822/831 cases passing; nine failing cases in DWG,
  block import, and project file flows. Running from the repository root adds two
  ribbon-source lookup failures, so use the build directory.
- These failures have not been classified against an unchanged baseline. They
  must be resolved or explained before release. No installer or deployment was
  published, and no claim of full feature parity is made.

## Next migration gate

Inventory every existing action and modal, implement its Qt adapter and observable
state transitions, then verify representative real projects and drawings side by
side. Remove the ImGui/GLFW links only after their remaining shared services are
replaced. Switch the shipped application/installer to Qt only after full parity,
regression, graphics, Unicode-path, high-DPI, accessibility and packaging checks.
