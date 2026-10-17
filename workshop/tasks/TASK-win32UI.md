# Qt / Win32 UI migration

Authority: user request, 2026-10-16, branch `test/win32UI`. This explicitly authorizes
the installed Qt dependency and replacement of the ImGui presentation layer.
Existing document, precision, command validation, and adjustment requirements remain
in force (REQ-055, REQ-058/059, REQ-101, REQ-175, REQ-201, REQ-374, REQ-387,
REQ-417/418).

Approach: introduce a separate Qt executable against the existing domain library;
keep Qt types above Commands. Use Qt Widgets and the Windows platform plugin for
native windows, QOpenGLWidget for the existing renderer, and bundled icon assets.
Move legacy font loading out of command implementation. Preserve the existing
application during parity work. Do not call a partial migration production ready.

Validation: configure/build with installed Qt 6.8.3 MSVC x64, run Qt integration
smoke checks, run existing domain tests, and inspect the resulting desktop UI.
Track remaining parity gaps in docs/qt-ui-migration.md.

Plan review: additive target avoids replacing a functioning editor before parity;
render resource cleanup must occur with a current context; Qt's framebuffer is
not framebuffer zero; save/close must preserve dirty drawings. UI dependencies
must not be introduced into document or command APIs.

2026-10-16 continuation: user requests autonomous iteration to production parity
with master. Fetched origin/master at 90aa196d and merged its missing commits.
Keep a named upstream revision in the parity inventory. Next gates: reproducible
Qt integration tests, lossless adjustment sessions and report/commit workflows,
correct viewport input, native modal adapters, graphics parity, and removal of
remaining presentation dependencies. A gate is complete only with passing
positive/failure tests; a working shared command alone is not UI parity.

Iteration 1: merged current master, added a source inventory (190 commands / 40
window flags), native Qt tests, adjustment persistence/acceptance/commit/report
workflows, layer/units dialogs, command focus/history and worker completion,
camera-plane pan/zoom, Qt annotation/dimension rendering, PDF annotation/scale/
Save As/extract workflows, and removed GLFW from the Qt target. Survey label
measurement now uses a frontend callback, avoiding an ImGui-context crash.
Fixed FBK closed-loop identity, invalid profile arc endpoints, DWG layer colors,
and updated format-aware regression fixtures. Domain suites: 1,632 + 831 cases
passed; Qt suite: 17 checks passed. Full parity remains the active objective.

Iteration 2: added native Quick Select with type-specific properties, numeric
validation, layer suggestions, normalized color values, include/exclude, scope
and append options. Filter settings apply only on acceptance. Qt build passed;
18 Qt results passed, including length filtering and cancellation preservation.
Domain code was unchanged; full domain results above remain the last baseline.

Iteration 3: connected CREATEPOINTS to a modeless Qt panel and viewport placement,
existing-marker selection, numbering, description/elevation/layer, duplicate
policies, JSON save/load, and Escape. Rejected placement/load for read-only
projects. Fixed missing undo snapshot for the shared Merge duplicate policy.
Verified native placement, selection, number advancement, merge coordinate/
description/elevation restoration, read-only rejection, Escape and creation undo.
Qt build and 19 Qt results passed; reran both complete domain suites: 1,632 cases /
9,201,236 assertions and 831 cases / 9,511 assertions passed. Point grids, advanced
point rendering and project conflict workflows remain open parity gates.

Iteration 4: native VIEWPOINTS and POINTDATABASE grids use stable point identities,
numeric view sorting, filters, world-coordinate doubles, source/group/visibility
columns, selection, local deletion and per-cell local undo. Shared points route
through the project database APIs, including overwrite/renumber/cancel conflicts
and read-only cells. Verified sorted edits preserve domain order, label identity
and raw codes; world precision, undo, every conflict choice, nonfinite rejection,
and read-only behavior. Qt build and 21 Qt results passed. Shared domain unchanged.
Inspection found the legacy survey JSON loader clears the live list before full
validation; hardening its parsing, transactionality, undo and Unicode paths is a
required follow-up release gate, alongside the remaining inventory.

Iteration 5: replaced fragment-based survey JSON parsing with full schema/value
validation before mutation. Imports preserve raw codes and world positions across
document origins, remove old labels through their owner, and form one undo step.
Legacy local-coordinate files remain readable. Saves serialize before opening
output, stage a sibling, check write/flush/close, and replace only on success;
UTF-8 filesystem paths support Unicode filenames. Added native shared-point
overwrite/renumber/cancel and project-delete/hide/cancel prompts, including timer
servicing when no log/revision changes. Verified every warning choice, malformed
and partially valid JSON, duplicates/ranges, failed writes, Unicode paths, origin
precision, labels/raw codes and undo. Qt and legacy builds passed; all 25 Qt
results, 1,632 domain cases / 9,201,236 assertions, and 831 snap/document cases /
9,511 assertions passed. JSON integrity gate above is resolved; full UI parity
remains active.

User-directed chrome iteration: moved File/Edit/View/Survey/Help into the logo's
application menu and moved New/Open/Save/Undo/Redo into a compact custom title bar.
Added minimize/maximize/restore/close, system drag, double-click maximize and native
resize/work-area handling. Replaced the docked command panel with a viewport overlay:
horizontal drag, width resizing, retained layout preferences, fading recent lines,
F2 console, Ctrl+9 visibility, and shared clickable command hints. Input events stop
at the overlay to prevent duplicate empty submissions into the viewport. Verified
menu access, quick actions, command/coordinate entry, hint dispatch, history toggles,
drag/resize, viewport bounds and maximize/restore. Qt build, all 26 Qt results and
styled smoke checks pass; inspected build/qt-editor-smoke.png. No domain changes.

Visual steering: user prefers the supplied nanoCAD reference's compact classic
CAD finish. Replaced oversized raised command cards with flat 22 px icon groups
and panel captions, reduced title-bar/quick-access/view controls, unified gray
dock/table headers and selection colors, tightened Properties/Toolspace rows,
and replaced the command settings emoji with the existing settings icon. Preserved
the requested logo application menu and floating command-line layout. Qt Fusion,
explicit light palette, theme-adapted neutral icon strokes and black viewport are
now the default. Optional GOSURVEY_QT_RUNTIME_DIR permits rebuilding a comparison
without closing a previous preview's drawing. Current preview:
build/classic-cad/GoSurveyQt.exe. Build, styled smoke, and all 26 Qt results passed;
inspected build/classic-cad/qt-editor-smoke.png. Full functional parity remains open.

Gray reference iteration: neutral medium-gray Fusion chrome, darker tree/property
fields, controlled muted-blue selection, paper-light command input and retained
black viewport. Extracted reusable QtAppearance service; logo menu > View >
Appearance offers live gray/light comparison with saved preference and refreshes
the existing icon artwork for contrast. Tinted dock controls for legibility,
added Toolspace folder/drawing icons and point counts, corrected the supplied
survey-point icon alias, and retained tree expansion/current item through command
refreshes. Verified appearance changes preserve geometry and undo plus collapsed/
expanded branches and selected tree item. Qt build, gray smoke and all 27 Qt
results passed. Preview: build/neutral-gray/GoSurveyQt.exe.

Input/layout correction: removed the Top/Front/etc toolbar row; retained named
orientations and Zoom Extents under View and the existing clickable cube. Drawing
tabs reserve height before the first OpenGL composition and refresh on show,
verified in the styled startup screenshot before any viewport click. Replaced
viewport-only character routing with a scoped application event filter for this
editor and its floating docks (ownership checked across window boundaries).
Ordinary typing moves to command entry from tabs, Toolspace, toolbar buttons and
read-only history; editable fields, modifiers, popups, modal dialogs and other
windows retain their handling. The filter is removed before editor destruction.
Verified focus transfer from docked and floating Toolspace, commands/history/
Escape, editable layer text, modal text and initial tab geometry. Qt build, styled
smoke and all 28 Qt results passed. Preview: build/editor-input/GoSurveyQt.exe.
Full parity remains open.

Native point transfer iteration: fetched master again (still 90aa196d); branch
already contains it. Connected IMPORTPOINTS/EXPORTPOINTS to native Qt dialogs and
Survey ribbon tools with preview, layout/header controls, skip/replace confirmation
and read-only guards. Added native clipboard warning conversion/paste/cancel adapter
and generated survey report dock with copy and safe Save As. Shared CSV validation
now scans the complete file, import stages records and creates one undo snapshot,
elevations remain doubles, UTF-8 BOMs are accepted, point integer overflow is
rejected, and export stages/flushes/closes output before replacement with failure
cleanup. Qt integration checks cover these flows including conversion before paste
placement. Full UI parity remains open; no production-complete claim.
Verified this checkpoint: both executables build; 32 Qt checks, 1,632 domain
cases / 9,201,236 assertions and 831 snap cases / 9,511 assertions pass. The
point-transfer preview smoke test passes. Corrected native Copy (direct shared
clipboard service) and Paste (shared PASTE command, plus PASTECLIP compatibility)
routing after the conversion integration test exposed the missing dispatcher alias.
Preview: build/point-transfer/GoSurveyQt.exe.

Version 0.7.2 ribbon pass: replaced the small Qt button lists with an audited
catalog covering all 268 release entries and 280 native entries overall. Kept the
existing supplied icons and prior Qt point/PDF/modify access. Added release tabs,
panels, modeling/piping inventory, contextual selection tabs, native flyouts,
shared Layers/color and responsive panel/More menus. Added live view/UCS/pipe/cloud
selectors and undoable hatch appearance controls. Pipe Run uses the selected size.
Release placeholders and still-unported native dialogs are explicitly disabled
with explanatory help, so presence is not misrepresented as full functionality.
The headless release inventory/icon audit, Qt build and 36 Qt checks pass. Preview
output is build/ribbon-v072/GoSurveyQt.exe. Full migration remains open.
Final visual pass retained the release's compact icon grids for Home Draw/Modify/
Palettes, which keeps the core panels expanded at desktop widths. Rechecked the
narrow-layout/icon-access test after this presentation adjustment and reran the
styled smoke test successfully; inspected the final screenshot. All existing
preview windows were preserved.
