# TASK-160 — REQ-310 3D crosshair: pickbox-aware arm gating and 2D fallback

- Type:    fix
- Status:  in progress
- Opened:  2026-09-01
- Owner:   chetjones003
- Follows: PR #152 (TASK-150). Branch cut from `pr152` head; lands via its own PR to `beta`.

## 1. Authority

- Goal:         GOAL-01 (a CAD/survey editor whose cursor describes the drawing correctly)
- Requirements: REQ-310 (accepted, 3D crosshair). Context: REQ-201 (a bad/degenerate state degrades
  to the safe default, never to "no cursor"), REQ-121 (object-selection pickbox).
- Constraints:  CON-07 (Windows/MSVC/Ninja build authoritative).
- Acceptance:
  - When fewer than two UCS axes are actually drawable in the current view **and** pickbox size,
    the viewport shows the standard 2D crosshair, not a partial triad.
  - No configuration of `viewportCrosshairPickHalfPx*` can produce a 3D crosshair that renders with
    an arm silently missing and no fallback.
  - Plan view (Z collapsed, X and Y drawable) still renders the 3D crosshair — the fallback must not
    over-trigger.
  - The pure-geometry tests in `Crosshair3dTests.cpp` are unchanged and still pass.

## 2. Problem

`src/viewport/Crosshair3d.hpp` marks an axis `visible` at projected length >= `kMinArmPx` (6 px) and
`Degenerate()` asks for the 2D fallback only when fewer than two axes clear that bar. The only
consumer, `src/ui/CadUi.cpp` (`drawAxis` lambda in the 3D-crosshair block, ~line 17193), then skips
any axis whose projected length is <= `gap`, the pickbox half-extent along that axis — up to ~32 px
when `viewportCrosshairPickHalfPx*` is large. An axis can therefore be `visible`, count toward the
not-degenerate total, and draw nothing: a one-armed crosshair with no fallback.

## 3. Files affected

- `src/ui/CadUi.cpp` — the 3D-crosshair draw block. Add a pickbox-aware `drawable` test per axis;
  fall through to the existing 2D path when fewer than two are drawable.
- `src/viewport/Crosshair3d.hpp` — doc comment only: state that the consumer layers a pickbox-aware
  check on top of `Degenerate()`. No behaviour change to the header.
- `tests/headless/transcripts/` + `HeadlessDriver.cpp` (or a `CadUi` test) — near-edge-on axis with
  a large pickbox setting yields the 2D crosshair.

## 4. Approach

1. After `crosshair3d::Compute(...)`, compute `gap` once per axis exactly as `drawAxis` does and a
   per-axis `drawable = arm.visible && projectedLen > gap`.
2. If `(#drawable) < 2`, do not draw the triad; let control fall through to the 2D crosshair path
   that already handles the `Degenerate` case.
3. `drawAxis` draws only axes already known drawable (no second threshold inside it).
4. Leave `Degenerate()` as the pure-geometry guard.

## 5. Test approach

- Headless: set a large `viewportCrosshairPickHalfPx`, orbit so one UCS axis is near edge-on, assert
  the rendered cursor is the 2D crosshair (arm count / class), not a partial triad.
- Regression: plan view with default pickbox still reports the 3D crosshair.

## 6. Architectural-boundary check

No new subsystem. `Crosshair3d.hpp` stays pure (no ImGui/GL/AppCommandState). The pickbox-aware
decision lives with the renderer that owns the pickbox, matching where REQ-121's pickbox already
lives. No spec change.
