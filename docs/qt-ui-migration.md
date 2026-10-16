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
`build/GoSurveyQtTests.exe` or `ctest --test-dir build -R '^qt.editor$'` on a desktop.
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

The application uses Qt's Windows platform plugin and native Windows top-level
frames; Qt Widgets own all presentation and input. The Autodesk-inspired dark
chrome uses the repository's existing PNG icons, Segoe UI, ribbon tabs, command
line, drawing tabs, dockable Toolspace and Properties, and a status bar.

## Current coverage

| Area | Implemented | Remaining parity work |
|---|---|---|
| Startup | Splash using existing artwork; Start page; Markdown What's New window | Startup auth/update/telemetry/crash-report wiring; recent-file tiles; version-based What's New display policy |
| Editor chrome | Menus, quick access, ribbon tabs, dock layout persistence | Full existing ribbon inventory, contextual ribbons, panel-specific command UI, shortcut parity |
| Documents | New/open DWG, save/save as, document switching, undo/redo, dirty markers, save/discard/cancel on close | Export-loss disclosures, full import/export UI, template workflow, recent-file recording |
| Viewport | Existing renderer, point-cloud inputs, dimension strokes, rich MTEXT wrapping/attachment/colors, SHX stroke adapter | Exact graphics parity, paper-space rendering, tables, map tiles, gizmo/section overlays, annotation scale/isolation and perspective verification |
| Input | Shared command dispatch, basic picking, Shift selection, two-corner selection boxes, object snaps, UCS construction-plane intersection, wheel zoom, middle-button pan/orbit, clickable cube faces and orientation controls | Full gesture and selection-cycle parity, grip editing, UCS-aware ORTHO/POLAR mouse constraints, advanced 3D selection, keyboard focus routing and transient tool UI |
| Properties/Toolspace | Drawing navigation; point/surface/block inventory; selected layer/color edits; native layer manager and units/scale dialog; drawing/project point grids | Complete geometry properties, context menus, surface/style dialogs |
| Survey points | Modeless creation controls, viewport placement, existing-marker selection, numbering and duplicate policies, JSON save/load; read-only protection; sortable/filterable drawing/project grids with source/group/visibility columns, edits, selection, database number conflict resolution and local undo | Import/export preview, full point-marker visibility/style parity, other project conflict dialogs; harden legacy JSON parsing/save failure behavior |
| Projects | Native create/open, lock acquisition and read-only choice, join detection on drawing open, shared point sync, settings enforcement, tracked-file restoration/save, point-save checks on close, lock release | Project settings UI; add/convert drawing; copy/link prompts; point conflict prompts; health/pack/turnover UI; stale-lock takeover UI; cross-project clipboard warnings |
| PDF viewer | Async page rendering, navigation/zoom, text/line/rectangle/ellipse annotations, scaled measurements, scale presets, undo/redo, Save As, close protection, page extraction | Search/thumbnails, calibration/checks, snapping, coordinate extraction, compare/audit, underlay attachment, editable persisted annotations, rotated-page/high-DPI parity, async file operations |
| Adjustment | Lossless `.gstrav` load/save, command text, raw-data imports, async solve, instrument weights, result acceptance, station commits, report TXT/RTF/PDF export, unsaved-close protection | Full form/raw-set editor, complete project settings, plotting, cancellation, merge-import and command-editor parity |

Commands that request an existing ImGui-only modal still need a Qt adapter. Sharing
the command dispatcher does not establish UI parity for those commands.
Native Quick Select now exposes object/property filters, include/exclude, current
selection scope and append through the shared selection service. Numeric input
is validated before applying; Cancel preserves the selection and saved filter.
Native command typing, history, autocomplete and Escape are connected; pan/zoom
uses the camera plane, and completed surface/cloud workers are serviced by Qt's
event loop. Exact annotation and all selection/gesture parity remain release gates.

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
- `GoSurveyQtTests`: 21 passed, 0 failed (19 workflow/render tests plus setup/teardown).
- `GoSurveyTests`: all 1,632 cases / 9,201,236 assertions pass.
- `GoSurveySnapTests`: all 831 cases / 9,511 assertions pass.
- Domain suites were run from `build/` outside the sandbox because GPU registry
  tests and the temporary AppData block-library fixture need that access.
- These green gates do not establish full UI parity. The outstanding workflow,
  graphics, dependency-removal and packaging gates above remain open.

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
