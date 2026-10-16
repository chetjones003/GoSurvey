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
