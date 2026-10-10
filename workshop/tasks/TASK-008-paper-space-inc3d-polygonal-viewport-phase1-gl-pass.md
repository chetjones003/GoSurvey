# TASK-008 — Paper Space Inc 3d Phase 1: GL per-viewport scissor pass (foundation)

- Type:    feature
- Status:  reverted — GL pass disabled; paper space stays on the ImGui overlay
- Opened:  2026-06-16
- Owner:   Workshop

## 1. Authority
- Goal:         Paper Space milestone (M-PaperSpace) — foundation for Inc 3d/3e enhancements
- Requirements: REQ-034 (accepted, deferred on this pass); REQ-036 (partial, in-place MSPACE feedback)
- ADRs:         ADR-006, ADR-008 — "the renderer eventually needs the per-viewport transform/clip pass for MSPACE drawing and for polygonal clipping"
- Constraints:  no broken functionality; maintain performance (frame budget REQ-100); no new dependency
- Acceptance:   rectangular viewports render model geometry clipped to viewport rects via GL scissor + transform; 
                existing paper-overlay rendering still works; performance ≥ prior baseline.
- Owning subsystem: Renderer (ViewportRenderer), UI (viewport/paper-space integration). Per ADR-006/008.

## 2. Scope
- In scope: 
  - Extend RenderScene to accept viewport list + paper-space rendering parameters
  - Add GL scissor rect + transform per viewport
  - Render model geometry once per viewport (no ImGui overlay fallback this increment)
  - Maintain backward compatibility (model space renders unchanged when no viewports)
  - Frame-rate validation (regression test that perf stays within REQ-100)
- Out of scope: 
  - Polygonal clipping (stencil buffer) — Phase 2
  - Polygonal viewport command (REQ-034) — Phase 3
  - Floating MSPACE in-place snapping optimization — deferred

- Smallest change: add viewport data to RenderScene parameters; loop over viewports with scissor/transform; reuse existing geometry batches.

## 3. Architectural boundary check
- [x] No new architectural decision — ADR-006/008 explicitly authorize the GL scissor pass ("paper-space pass"; "renderer eventually needs...").
      Scissor + transform are implementation details; no new abstraction/dependency.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Should paper overlay continue as fallback if GL pass fails? | — | No. Commit fully to GL. If GL unavailable, paper space is disabled (constraint check in Init). |
| Q2 | Viewport transform: model→paper→screen inline, or pre-computed? | — | Pre-computed in ViewportRenderer (model→paper via viewport; paper→screen via ortho). |
| Q3 | Layer filtering: GL-side (via stencil/discard) or CPU-side (geometry skip)? | — | CPU-side for Phase 1 (simpler; reuse existing layer logic). Stencil for Phase 2 (polygonal). |

## 5. Assumptions
```
ASSUMPTION-1: ViewportRenderer::RenderScene will remain the primary render entry point; paper-space rendering is a mode switch (activeSpace), not a separate path.
- Because:       Keeps the call site (main.cpp) simple; reuses existing frame/buffer management.
- Risk if wrong: Code duplication between model and paper render paths.
- Validate by:   Code review; manual inspection of call sites.

ASSUMPTION-2: Scissor rect is sufficient for rectangular viewport clipping (no GL_POLYGON_OFFSET, no custom clip planes).
- Because:       Scissor is standard, available on all GL versions in use; fast.
- Risk if wrong: Floating-point precision at viewport edges may show clipping artifacts.
- Validate by:   Visual inspection in paper space with tight viewports; if artifacts appear, revisit.

ASSUMPTION-3: The paper→screen transform (ortho matrix) is unchanged from the current ImGui overlay path; reuse the same ortho math.
- Because:       Existing path is proven; no need to invent new transforms.
- Risk if wrong: Paper geometry renders at wrong position/scale in GL.
- Validate by:   Visual test: paper-space sheet outline, viewport rects match prior ImGui rendering.

ASSUMPTION-4: Existing model geometry batches (lines, circles, arcs, polylines) require no modification; only transform/scissor change.
- Because:       Geometry is stored in model-space coordinates; transform happens at render time (uniform matrix).
- Risk if wrong: Some geometry types may not transform correctly.
- Validate by:   Test with all geometry types in paper viewports.
```

## 6. Plan
- Approach: 
  1. Extend ViewportRenderer::RenderScene to accept viewport list + paper parameters (activeSpaceIndex, paperLayouts).
  2. Add per-viewport scissor + ortho transform logic to the render loop.
  3. Replace ImGui paper overlay with GL-rendered paper geometry (sheet, viewport rects, grips, selections).
  4. Validate frame rate (perf review against REQ-100) and visual correctness.

- Files/functions to touch:
  - `src/render/ViewportRenderer.hpp` — add viewport list parameter to RenderScene; add per-viewport state helpers.
  - `src/render/ViewportRenderer.cpp` — implement per-viewport scissor loop; move paper-geometry GL rendering here.
  - `src/ui/CadUi.cpp` — stop drawing paper overlay via ImGui; move to GL; update paper interaction input (still ImGui-based, screen→paper calc only).
  - `src/app/main.cpp` — pass viewport list to RenderScene.

- Test approach:
  - happy path: render paper space with 2+ viewports; verify sheet outline + viewport rects visible at correct positions and scales.
  - failure mode: render with empty viewport list (model space); verify no regression vs prior baseline.
  - performance: measure frame time before/after; must not exceed REQ-100 budget on benchmark scene.
  
- Steps:
  - [ ] Extend RenderScene signature + add viewport loop skeleton
  - [ ] Implement per-viewport scissor + ortho transform
  - [ ] Port paper-geometry drawing (sheet, rects, grips, selection) from ImGui overlay to GL
  - [ ] Remove ImGui overlay paper drawing; keep ImGui for interaction only
  - [ ] Validate paper-space viewport alignment + appearance
  - [ ] Performance test (frame time, no regression)
  - [ ] Manual test: create/select/move/freeze viewports; verify GL rendering

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1–Q3); tests visual + performance; no unit test harness (GL integration test).
- Risk: larger refactoring; touches critical rendering path. Requires careful verification.

## 8. Implementation log
- 2026-06-16 plan written; boundary check = No (covered by ADR-006/008).
- 2026-06-16 Extended ViewportRenderer::RenderScene signature with paper-space parameters (activeSpaceIndex, paperLayoutsPtr, origins, MUP).
- 2026-06-16 Implemented RenderPaperSpace skeleton: sets up paper-space ortho projection, renders sheet outline, viewport borders.
- 2026-06-16 Added per-viewport scissor logic (placeholder for full model geometry rendering).
- 2026-06-16 Wired up paper-space parameters from AppCommandState to RenderScene in main.cpp.
- 2026-06-16 Implemented per-viewport model→paper transform matrix (accounts for origin, scale, viewport center).
- 2026-06-16 Added model geometry rendering: lines, circles with scissor clipping per viewport.
- 2026-06-16 Build clean; ready for visual testing and performance validation.
- 2026-06-16 VISUAL TEST FAILED. The GL pass used a fixed full-framebuffer ortho
  (Ortho(0, sheetW, sheetH, 0, ...) over glViewport(0,0,fbW,fbH)) with NO pan/zoom.
  So the GL sheet outline + viewport border were anchored to the screen and could
  not move/scale with the view. They appeared as a large blue box that did not
  track the (pan/zoom-aware) ImGui paper overlay. The opaque ImGui sheet covered
  the GL model geometry, so the GL pass delivered no visible benefit and one big bug.
- 2026-06-16 REVERTED. Removed RenderPaperSpace (decl + def); simplified RenderScene
  paper-space params to a single `int activeSpaceIndex` (>= 0 ⇒ skip GL geometry,
  leave cleared background); updated main.cpp call; dropped unused PaperSpace.hpp
  include. Paper space remains rendered by the ImGui overlay (CadUi), which already
  draws the sheet, viewport rects, and model geometry clipped+scaled per viewport,
  pan/zoom-aware. Commit 08ffa1b.

## ROOT-CAUSE / RETROSPECTIVE
A GL scissor pass CAN replace the overlay, but ASSUMPTION-3 was wrong: the paper→
screen transform is NOT a fixed ortho — it must use the same pan/zoom mapping the
overlay's `w2s` uses (worldLeft/worldRight/worldTop/worldBottom → imgPos/avail).
Any retry of Phase 1 must:
  1. Pass the view pan/zoom (or the worldLeft/Right/Top/Bottom window) into the GL pass.
  2. Build the paper ortho from that window so the GL sheet aligns with the overlay sheet.
  3. Decide ownership: either GL draws geometry AND the overlay draws only chrome
     (handles/grips/selection), or keep the overlay fully — but never both drawing the
     sheet/border at different transforms.
This is an architectural alignment decision → SPEC GAP candidate before any retry.

## 9. Self-verification
- [ ] build-project        — PASS
- [ ] architecture-review  — PASS (no new decision; scissor/transform within ADR-006 scope)
- [ ] code-review          — PASS (render loop readable; state management clear)
- [ ] dependency-audit     — PASS / n-a (no new dependency)
- [ ] performance-review   — PASS (frame time within REQ-100; no regression on model-space render)
- [ ] testing              — PASS (visual: paper viewports align + render; performance: frame budget met)

## 10. Verification result
- Submitted:  —
- Verdict:    —
- Findings:   —

## 11. Outcome
- Requirements satisfied: none — GL pass reverted. Paper space continues to satisfy
  REQ-026/027/028 via the existing ImGui overlay (unchanged). REQ-034/036 NOT advanced.
- Tests added:            —
- Refactors:              GL paper pass removed; RenderScene paper-space params reduced to activeSpaceIndex.
- Docs updated:           this task log (root-cause + retry requirements).
- Done:                   reverted 2026-06-16 (commit 08ffa1b). Retry needs pan/zoom-aware GL transform (see retrospective).
