# TASK-732-p1b — PDF viewer as a real Windows window: minimize / maximize / docking (issue #732)

- Type:    feature
- Status:  submitted
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

## 5. Result
- `ImGuiConfigFlags_ViewportsEnable` on; main loop calls `UpdatePlatformWindows` / `RenderPlatformWindowsDefault`
  and restores the main GL context. The viewer's window class sets `NoAutoMerge` (own OS window) and clears
  `NoDecoration` / `NoTaskBarIcon` (OS frame with minimize / maximize / close, task-bar entry). Docking works
  because ImGui docks any unclassed window. `DrawFloatingWindowChrome` skips windows that own an OS frame.
- Verified in the real app (DevShell `pdfview-window`): own viewport, OS frame, task-bar flag, docks into the
  layout (main viewport), undocks back to its own window. `pdfview-bench` and `p696-e2e` still pass
  (p696-e2e showed 2 turnover-order failures on one run and none on the next: that check ties on a
  one-second timestamp, so it is timing-flaky, unrelated to this change).
- Bench with the OS window: first page 90 ms (includes creating the window), viewer cost p95 3.4 ms, worst 4.5 ms.
- NOT verified by a person: clicking the OS minimize / maximize buttons, second monitor, dragging a tab
  onto a dock slot with the mouse (the Test Engine docks programmatically).
