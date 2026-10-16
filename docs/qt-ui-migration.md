# Qt / Win32 migration status

Branch: `test/win32UI`. Authorized by the user on 2026-10-16.

This is an executable migration baseline, **not a production replacement** for
GoSurvey. The existing executable remains available during parity work.

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
| Viewport | Existing GL renderer, grid, lines/circles/arcs/ellipses/polylines, blocks, feature lines, solid/surface geometry, basic Qt text and point overlays | Exact annotation/dimension/MTEXT/SHX layout, paper-space rendering, tables, point-cloud display inputs, map tiles, gizmo/section overlays, graphics parity |
| Input | Shared command dispatch, basic picking, Shift selection, two-corner selection boxes, object snaps, UCS construction-plane intersection, wheel zoom, middle-button pan/orbit, clickable cube faces and orientation controls | Full gesture and selection-cycle parity, grip editing, UCS-aware ORTHO/POLAR mouse constraints, advanced 3D selection, keyboard focus routing and transient tool UI |
| Properties/Toolspace | Drawing navigation; point/surface/block inventory; selected layer/color edits through shared command helpers | Complete editable geometry properties; layer manager; point database grids; tree context menus; surface and style management dialogs |
| Projects | Native create/open, lock acquisition and read-only choice, join detection on drawing open, shared point sync, settings enforcement, tracked-file restoration/save, point-save checks on close, lock release | Project settings UI; add/convert drawing; copy/link prompts; point conflict prompts; health/pack/turnover UI; stale-lock takeover UI; cross-project clipboard warnings |
| PDF viewer | Independent Qt window, PDFium streaming document open, asynchronous cancellable raster rendering, page navigation, bounded zoom/fit width | Search, thumbnails, annotations, measurement, calibration, snapping, coordinate extraction, split/compare, audit/export, underlay-attach UI |
| Adjustment | Independent Qt window, GSTC editor, FBK/StarNet/RW5/JobXML import, GSTC save, existing least-squares solver, coordinate/residual tables and chi-square output | Full form/raw-set editor, weights UI, result acceptance/export/plot/report workflows, unsaved editor close prompts, asynchronous solve for large networks |

Commands that request an existing ImGui-only modal still need a Qt adapter. Sharing
the command dispatcher does not establish UI parity for those commands.
Basic Qt MTEXT rendering flattens rich spans and does not yet reproduce attachment,
wrapping, SHX, perspective sizing, or all layer/isolation rules.

## Dependency boundary

`src/qt/` does not call ImGui or initialize an ImGui context. `QOpenGLWidget` owns
the context; the existing renderer's color texture is copied into Qt's framebuffer.
Context destruction and viewport destruction release renderer resources while
the context is current, following [Qt's QOpenGLWidget lifecycle](https://doc.qt.io/qt-6/qopenglwidget.html).

Legacy font loading was moved from `CadCommands.cpp` into
`src/ui/LegacyApplicationFonts.cpp`. The shared domain library **still links
ImGui core**, because legacy font measurement, SHX draw-list paths and ribbon
measurement still reference it. Those dependencies must be extracted before an
ImGui-free binary can be claimed. GLFW also remains linked through AppIcon's
legacy platform helpers; the Qt application itself creates no GLFW window.

## Verification on this workstation

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
