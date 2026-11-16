# GoSurvey Rust/Tauri drafting spike

Throwaway prototype on `feat/rust-tauri-spike`, governed by
[REQ-9901](../../spec/spikes/rust-tauri.md). This is a runnable desktop experiment,
not a replacement for the production application.

From **this worktree's root**, run:

```powershell
.\run-tauri-spike.ps1
```

This starts Tauri and the loopback Vite server at `127.0.0.1:1420`. React/CSS
changes hot reload; Rust changes rebuild and restart the native app. Install
Node 22.12+ (Node 26 used here), stable Rust's MSVC toolchain, MSVC C++ build tools
and WebView2. The helper finds `%USERPROFILE%\.cargo\bin` without changing your
global PATH and runs `npm ci` when dependencies are missing.

```powershell
.\run-tauri-spike.ps1 -Mode build  # debug desktop with bundled assets, no installer
.\run-tauri-spike.ps1 -Mode run    # launch that build without Vite
.\run-tauri-spike.ps1 -Mode test   # Rust engine and camera tests
```

Cargo output lives in `build/.cmake/tauri-spike/`, screenshots/reports in
`build/artifacts/tauri-spike/`. Frontend output is ignored under this spike's
`dist/`; dependency lockfiles are checked in. `GoSurveySpike.exe` is separate
from production `GoSurvey.exe`. No Qt/C++ sources or native build scripts change.

## Using the workspace

- `LINE` / `L`: first point, next point(s), Enter to finish. Click the viewport
  or type `X,Y`; `@dX,dY` is relative to the preceding point. Chained segments
  are individual line entities. Example: `L`, `0,0`, `@20,10`, Enter.
- Click a line to select it; Shift + click toggles additional lines. Properties
  shows real coordinates, layer, length and angle. The Objects tab lists entities.
- `MOVE` / `M`, `COPY` / `CO` / `CP`: use a preselection, or pick lines and Enter;
  enter a base point then a destination. Relative destination entry is supported.
- `DELETE` / `ERASE` / `E` or the Delete key removes preselected lines.
- `UNDO` / `U` / Ctrl+Z and `REDO` / Ctrl+Y; undo records individual edits.
- `PAN` / `P`: left-drag pans 1:1. Enter, Escape or right click exits. Middle
  drag pans at any time. Wheel zoom is anchored to the cursor.
- `ZOOM` / `Z`: enter a positive factor or `E` for extents. `ZE` fits directly.
  Toolbar zoom controls and View tab work too.
- F3 toggles endpoint snap; F8 toggles ORTHO. F2 expands command history.
  Up/Down recalls command input. Escape cancels a pending command/selection.
- Layer selection chooses the layer for new lines. Eye buttons hide display
  layers. The help button explains available commands.

## Architecture and limits

Rust owns `f64` line entities, monotonic IDs, selection, command parsing/phases,
ORTHO constraints and undo/redo. Its engine is independent of Tauri and React.
Tauri serializes events through one owned engine. React renders native snapshots
and owns the camera, hover/snap preview, panel state and transient command text.
Camera/pointer updates do not round-trip to Rust on every animation frame.
Frontend reloads request a fresh snapshot from the living native owner; native
restarts reset the in-memory document.

The focused port follows GoSurvey's `CadCommands.cpp` selection/base/destination
interaction, `OrthoConstrain.hpp` dominant-axis rule, and `ZoomFraming.hpp`
minimum-span framing. This experiment uses 20% framing space for labels instead
of native's 8%. It uses retained SVG for 2D linework, **not** the production
OpenGL renderer and not a large-drawing GPU performance demonstration.

There is no disk persistence, file interchange, projects, survey database, COGO,
surfaces, 3D, PDF or release installer. The sample is synthetic survey-style
linework. Unsupported ribbon tools are disabled or explicitly identified as
outside this spike. Properties is an inspector, not an editor. Endpoint snap
only; no crossing/window selection. Command coordinates are limited to finite
values within ±1e12 drawing units. Undo retains at most 100 snapshots.

## Verification and captures

From this directory, after a debug build:

```powershell
npm run test:desktop
npm run test:hmr
```

These launch their own hidden native WebView2 instance with an isolated profile,
then stop only their own processes. The desktop smoke uses port 9227 and the
HMR test uses 9228; Vite uses 1420. The tests require these ports to be free.
Playwright attaches to the **actual Tauri app**, not a JavaScript backend mock.
The HMR test temporarily appends a CSS probe and restores the source afterward.

Initial verification on 2026-11-15:
- Rust engine tests: passed (typed/relative state-plane entry, transforms,
  delete/undo/redo, invalid/degenerate input, selection/ORTHO/cancellation).
- Camera tests: passed (world/screen precision, cursor-anchored zoom, extents).
- TypeScript check, Vite production build and Windows Tauri debug build: passed.
- Native interaction smoke: all 12 workflow checks passed; no page errors.
- Warm CSS HMR observed in 176 ms including file write/CDP/polling overhead;
  no Rust recompile, native geometry survived HMR and full frontend reload.
  This is one local observation, not a general latency guarantee.

No production C++/Qt suite was run because the spike changes no production code
or build targets. No large-scene rendering or native Rust rebuild timing claim
is made. The user approved the neutral UI on 2026-11-16 and requested preserving
the branch without a PR. Further migration work is deferred; no production
migration decision has been made.
