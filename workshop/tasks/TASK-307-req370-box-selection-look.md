# TASK-307 — AutoCAD-style box selection (REQ-370)

- Type:    feature
- Status:  self-verify
- Opened:  2026-10-05
- Owner:   Claude / Chet

## 1. Authority
- Goal:         UI parity with AutoCAD / Civil 3D (project.md)
- Requirements: REQ-370 (accepted, D-2026-10-05-a)
- Constraints:  REQ-205 build budget; no new dependencies
- Acceptance:   see REQ-370 (window blue/solid, crossing green/dashed, live preview == click result,
                selected objects blue, preview does not touch `selection`)
- Owning subsystem: UI / Renderer / Commands

## 2. Scope
- In scope: box look (model + floating viewport); preview highlight; selected colour yellow -> blue.
- Out of scope: cursor "Specify opposite corner" tooltip; sub-object (face/edge) selection colours;
  grips; paper-space box look beyond the floating viewport.
- Smallest change: restyle the two existing overlay draw sites; add one preview helper that runs
  `ComputeSelectionFromRect` on a scratch selection and feeds the existing hover-highlight channel;
  change the two selection colour constants in `ViewportRenderer.cpp`.

## 3. Architectural boundary check
- [x] No new abstraction/layer/dependency/data format. Preview reuses the hover channel.

## 4. Questions
None open.

## 5. Assumptions
ASSUMPTION-1: window "solid" and crossing "dashed" borders are drawn with ImGui segments (no dash primitive).
- Risk if wrong: cosmetic only.
ASSUMPTION-2: preview tint = hover channel recoloured bluish-white; hover is never shown during a box drag.
- Risk if wrong: hover and preview indistinguishable while dragging (harmless).

## 6. Plan
- Files: `src/ui/CadUi.cpp` (box overlays), `src/viewport/TransformPreview.{hpp,cpp}` (preview build),
  `src/app/main.cpp` (feed hover channel), `src/render/ViewportRenderer.cpp` (colours), tests.
- Tests: preview set == committed set (window, crossing); `selection` unchanged; empty box.
- Steps:
  - [x] box look
  - [x] preview helper + wiring + cache
  - [x] selected colour
  - [x] tests, build, verify
