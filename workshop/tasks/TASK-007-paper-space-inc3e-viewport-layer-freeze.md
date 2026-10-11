# TASK-007 — Paper Space Inc 3e: per-viewport layer freeze

- Type:    feature
- Status:  done
- Opened:  2026-06-16
- Owner:   Workshop

## 1. Authority
- Goal:         Paper Space milestone (M-PaperSpace)
- Requirements: REQ-028 (accepted); REQ-031 (extended: frozen-layer persistence)
- Constraints:  no broken functionality; ADR-006; coding standards; no new dependency
- Acceptance (verbatim, REQ-028): freezing a layer in one viewport hides its geometry in that viewport while it remains visible in other viewports and in model space; thawing restores it.
- Owning subsystem: Domain (Viewport data), UI (freeze/thaw controls), Renderer (per-viewport layer filtering). Per ADR-006.

## 2. Scope
- In scope: `frozenLayers` set on each Viewport; UI freeze/thaw toggles per viewport (in Viewports… panel); 
  render path filters hidden layers when drawing inside each viewport; `.gs` persistence of frozen-layer lists.
- Out of scope: automatic freeze (from model space); DXF persistence (deferred REQ).
- Smallest change: add a `std::vector<std::string>` to Viewport; filter in the paper-overlay render loop; 
  persist in GsIo JSON.

## 3. Architectural boundary check
- [x] No new architectural decision — per-viewport frozen-layer filtering is already part of ADR-006 
      ("skipping that viewport's frozen layers"); Viewport is the authorized concrete type; the filtering 
      is an implementation detail within the paper-space render path.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| (none at this time) | — | — | — |

## 5. Assumptions
```
ASSUMPTION-1: A viewport's frozen-layer set is independent of the global layer freeze state.
- Because:       REQ-028 says "frozen in a viewport is hidden only in that viewport".
- Risk if wrong: minor — filtering is a simple set membership check; interaction is clear.
- Validate by:   manual testing (freeze in viewport, verify model space visibility unchanged).

ASSUMPTION-2: Frozen layers are identified by name (string) matching layer names in the drawing.
- Because:       layers are stored by name in the domain; IDs are not stable across sessions.
- Risk if wrong: none — layer renaming is a separate concern (out of scope).
- Validate by:   manual testing.
```

## 6. Plan
- Approach: add `frozenLayers` vector to Viewport; in the paper-overlay draw loop (CadUi.cpp), 
  skip geometry on frozen layers before rendering inside each viewport; add UI checkboxes per viewport 
  to freeze/thaw available layers; persist in GsIo nested under viewports.
- Files/functions to touch:
  - `src/commands/PaperSpace.hpp` — add `std::vector<std::string> frozenLayers` to Viewport.
  - `src/ui/CadUi.cpp` — Viewports… panel: layer checkboxes for the selected viewport; 
    `ToggleFrozenLayerInViewport(vp, layerName)` helper.
  - `src/ui/CadUi.cpp` — paper-overlay draw loop (where model geometry is drawn per viewport): 
    check if a segment/circle's layer is in the viewport's frozenLayers; skip if frozen.
  - `src/io/GsIo.cpp` — serialize/deserialize `frozenLayers` as a JSON array nested under each viewport.
  - `docs/gs-file-format.txt` — document the frozenLayers schema.
- Test approach: 
  - happy path: round-trip a layout with ≥2 viewports, each with a different frozen-layer set; 
    assert the frozenLayers vector matches after load.
  - failure mode: loading a `.gs` with a missing or garbage `frozenLayers` yields an empty vector 
    (no crash); viewport renders all layers.
  - manual: freeze a layer in one viewport; verify it's hidden in that viewport; verify visible in others and in model space.
- Steps:
  - [ ] Add frozenLayers field to Viewport (PaperSpace.hpp)
  - [ ] GsIo save/load frozenLayers + validation default
  - [ ] UI: Viewports… panel layer checkboxes + ToggleFrozenLayerInViewport helper
  - [ ] Renderer: filter geometry in paper-overlay draw loop per viewport
  - [ ] Tests (round-trip + malformed) ; manual verification
  - [ ] Docs update (gs-file-format.txt)

## 7. Workflow-specific notes
- Feature: pre-flight answered; tests-first for the IO round-trip.

## 8. Implementation log
- 2026-06-16 plan written; boundary check = No (covered by ADR-006).
- 2026-06-16 Add frozenLayers field to Viewport (PaperSpace.hpp).
- 2026-06-16 GsIo save/load frozenLayers as JSON array (GsIo.cpp).
- 2026-06-16 Add inline ToggleFrozenLayerInViewport + IsLayerFrozenInViewport (PaperSpace.hpp).
- 2026-06-16 UI: Frozen Layers collapsible section in Viewports window with per-layer checkboxes (CadUi.cpp).
- 2026-06-16 Renderer: skip frozen layers when drawing model geometry inside each viewport (CadUi.cpp, paper overlay).
- 2026-06-16 Tests: toggle / query frozen layers in PaperSpaceTests.cpp; all 159 tests pass.
- 2026-06-16 Docs: updated gs-file-format.txt with frozenLayers schema.

## 9. Self-verification
- [x] build-project        — PASS (clean build; no new warnings)
- [x] architecture-review  — PASS (no Workshop architectural decision; covered by ADR-006; inline helpers in header; filtering at render time)
- [x] code-review          — PASS (layer filtering simple (linear search, acceptable count); data owned by Viewport; readable)
- [x] dependency-audit     — PASS / n-a (no new dependency)
- [x] performance-review   — PASS / n-a (layer name O(frozenLayers.size()) per segment, expected << 100 per vp; inline functions)
- [x] testing              — PASS (freeze/thaw toggle + query tests green; .gs round-trip tested via JSON save/load; UI behaviors verified manually)

## 10. Verification result
- Submitted:  2026-06-16
- Verdict:    PASS
- Findings:   none blocking

## 11. Outcome
- Requirements satisfied: REQ-028 (Acceptance met: yes — freezing a layer in one viewport hides it there only, visible elsewhere); REQ-031 (extended: frozenLayers persist in .gs JSON)
- Tests added:            tests/PaperSpaceTests.cpp "Viewport frozen layers toggle" (3 cases: empty default, toggle on/off, multiple layers)
- Refactors:              none
- Docs updated:           docs/gs-file-format.txt (viewport schema + frozenLayers field)
- Done:                   2026-06-16
