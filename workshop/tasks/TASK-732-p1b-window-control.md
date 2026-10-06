# TASK-732-p1b — PDF viewer as a real Windows window: minimize / maximize / docking (issue #732)

- Type:    feature
- Status:  plan
- Opened:  2026-10-06
- Owner:   Claude

## 1. Authority
- Requirements: **REQ-387 clause 7**, ADR-067 (a) revised, D-2026-10-06-b (user chose "Separate Windows window").
- Acceptance: see REQ-387 clause 7 manual bullets and the Developer Shell regression bullet.
- Owning subsystem: `src/app/main.cpp` (platform loop), `src/ui/PdfViewerWindow.cpp`

## 2. Scope
- In: enable ImGui multi-viewport (GLFW + OpenGL3 backends), the per-frame platform-window update and
  render with GL-context restore, the viewer's window class (OS decoration, task-bar entry, opens detached),
  docking, shutdown order.
- Out: making other panels detach on purpose (they may, as a side effect); annotations; split.

## 3. Architectural boundary check
- Changes the platform loop (new architecture decision, recorded and approved: D-2026-10-06-b).

## 4. Risks to verify
- Custom title bar / `WinFrameControls`, splash screen, saved dock layout, `p696-e2e`, `pdfview-bench`.
- Textures made on the main context are valid in detached windows (GLFW context sharing).
