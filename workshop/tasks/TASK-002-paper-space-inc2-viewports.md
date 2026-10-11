# TASK-002 — Paper Space Increment 2: layout viewports at independent scales

- Type:    feature
- Status:  done
- Opened:  2026-06-15
- Owner:   Workshop

## 1. Authority
- Goal:         Paper Space milestone (M-PaperSpace)
- Requirements: REQ-027 (accepted); REQ-031 (viewport persistence)
- Constraints:  no broken functionality; ADR-006; coding standards; no new dependency
- Acceptance (verbatim, REQ-027): the user can add a viewport and set its scale and
  model geometry appears inside it at that scale; a layout can hold ≥2 viewports
  showing the model at different scales simultaneously; viewports can be moved and
  resized.
- Owning subsystem: Domain (Viewport data), UI (create/edit/render overlay), IO (.gs). Per ADR-006.

## 2. Scope
- In scope: Viewport data on PaperLayout (paper rect in inches, model center, scale);
  create a viewport; render model geometry clipped + scaled inside each viewport;
  select + move + resize + set scale + delete; `.gs` persistence; data-model test.
- Out of scope: per-viewport layer freeze (Inc 3); plotting (Inc 4); high-performance
  GL-batch viewport pass (tracked debt — see §below); DXF persistence (deferred REQ).
- Smallest change: extend PaperLayout with a Viewport vector; reuse the existing paper
  overlay (ImGui worldToScreen + clip) to draw the model inside each viewport.

## 3. Architectural boundary check
- [x] No new architectural decision — Viewport is the concrete data ADR-006 already
      authorized ("a viewport is a model-space window with its own scale, center, rect").
      The render mechanism (ImGui overlay clip vs GL scissor pass) is an implementation
      choice; the performant GL-batch pass is recorded as technical debt, not a new decision.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| (carried) | plot/delivery/DXF | 2026-06-15 | PDFium / incremental / .gs-now |

## 5. Assumptions
```
ASSUMPTION-1: Viewport scale = model units per paper inch (matches plot-scale MUP semantics).
- Because:       AutoCAD viewport scale is model:paper; MUP is already model-units-per-plotted-inch.
- Risk if wrong: a display-only conversion; easily adjusted.
- Validate by:   Inc 4 plotting review.
ASSUMPTION-2: A new viewport defaults to a centered rect at the layout's plot scale, centered on model extents.
- Because:       gives an immediately useful viewport; user adjusts after.
- Risk if wrong: cosmetic; user edits rect/scale/center.
- Validate by:   user feedback.
```

## 6. Plan
- Approach: add `Viewport` to PaperLayout; render each viewport in the paper overlay via
  a model→paper→screen transform with an ImGui clip rect; interactions (select/move/resize)
  via corner/body grips in paper space; numeric scale field; `.gs` nested persistence.
- Files: `src/commands/PaperSpace.hpp` (Viewport), `src/io/GsIo.cpp` (nested array),
  `src/ui/CadUi.cpp` (create/select/move/resize/scale/delete + render-inside overlay),
  `tests/PaperSpaceTests.cpp` (viewport transform/defaults), `docs/gs-file-format.txt`.
- Test approach: happy = a pure transform helper maps a model point through a viewport to
  the expected paper point at a given scale/center; failure = zero/invalid scale is clamped.
- Steps:
  - [ ] Viewport struct + a pure `ModelToPaperIn` transform helper
  - [ ] create/delete + select + move + resize + scale UI in paper space
  - [ ] render model (lines/polylines/circles/arcs) clipped + scaled inside each viewport
  - [ ] `.gs` persistence (viewports nested under layouts) + docs
  - [ ] tests (transform + clamp) ; build ; manual ≥2 viewports at different scales

## 7. Workflow-specific notes
- Feature: tests-first for the pure transform; UI behaviors manual.
- Technical debt: model-inside-viewport is drawn via the ImGui overlay this increment.
  For very large drawings this may pressure REQ-100; the performant path is the GL
  scissor+transform batch pass named in ADR-006. Removal condition: a profiled frame-budget
  miss on a large drawing in paper space. Follow-up: TASK (Inc-2b GL viewport pass).

## 8. Implementation log
- 2026-06-15 plan written; boundary check = No (Viewport authorized by ADR-006).
- 2026-06-15 Viewport struct + ModelToPaperIn/safeScale in PaperSpace.hpp; viewports vector on PaperLayout.
- 2026-06-15 AddViewport/DeleteViewport + selectedViewport* state; persist nested in .gs; gs-file-format documented.
- 2026-06-15 paper overlay renders each viewport: border + grips (selected) + model (lines/polylines/circles/arcs/survey) clipped + scaled inside.
- 2026-06-15 model-space mouse pick/right-click gated off in paper space (pan/zoom kept).
- 2026-06-15 Viewports… popup: add / select / edit X/Y/W/H/scale/center / delete.
- 2026-06-15 tests (transform + safeScale clamp); build clean; 20 cases green.

## 9. Self-verification
- [x] build-project        — PASS (clean; pre-existing CRT warnings only)
- [x] architecture-review  — PASS (Viewport authorized by ADR-006; no new dependency/global; data on owned PaperLayout)
- [x] code-review          — PASS (pure transform tested; concrete types; readable)
- [x] dependency-audit     — PASS / n-a
- [x] performance-review   — overlay draw per viewport; debt recorded (GL-batch pass, §7) with removal condition
- [x] testing              — PASS (transform + clamp green; create/edit/render + .gs round-trip verified manually)

## 10. Verification result
- Submitted:  2026-06-15
- Verdict:    PASS
- Findings:   none blocking; one tracked tech-debt item (GL viewport pass for REQ-100 on large drawings)

## 11. Outcome
- Requirements satisfied: REQ-027 (yes); REQ-031 extended (viewports persist)
- Tests added:            "Viewport model->paper transform", "Viewport safeScale clamps"
- Refactors:              none
- Docs updated:           docs/gs-file-format.txt (viewports schema)
- Technical debt:         model-in-viewport via ImGui overlay; GL-batch pass deferred (remove on profiled REQ-100 miss)
- Done:                   2026-06-15
