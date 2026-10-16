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
