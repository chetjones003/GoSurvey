# TASK-017 — REQ-028 per-viewport layer freeze: audit + harden (plot WYSIWYG)

- Type:    bug (hardening of shipped REQ-028)
- Status:  done — user verified in app 2026-08-18
- Opened:  2026-07-13
- Owner:   chetjones003

## 1. Authority
- Goal:         Paper Space milestone (M-PaperSpace)
- Requirements: REQ-028 (accepted) — per-viewport layer freeze
- Constraints:  CLAUDE.md rules 1–5 (simplest change, no new abstraction/dependency/global,
                architectural consistency); no broken functionality; ADR-006/007.
- Acceptance (verbatim, REQ-028): freezing a layer in one viewport hides its geometry in that
  viewport while it remains visible in other viewports and in model space; thawing restores it.
- Owning subsystem: Renderer/IO (PdfPlot) + Domain (test). Per REQ-028 owner-layer UI/Domain/Renderer.

## 2. Scope
- Context: REQ-028 was delivered by TASK-007 (2026-06-16) — domain field, GsIo persistence, on-screen
  viewport render filtering, UI "Frozen Layers" panel, and toggle unit tests. User asked to audit +
  harden it before manual verification.
- Audit findings:
  - F1 (defect, fixed): the PDF plot (`PdfPlot.cpp`) filtered only the GLOBAL layer state
    (on/frozen/plottable), never the per-viewport `frozenLayers`. A layer frozen in a viewport was
    hidden on screen but still PRINTED — a WYSIWYG violation of REQ-028 for plotted output.
  - F2 (debt, deferred): the shared snap/pick engine (`PickClosestCadEntity`, `CadSnap::FindBest`)
    does not consult a viewport's frozen set, so in floating model space you can snap/select an
    entity on a layer that freeze has made invisible. Fixing needs the frozen set threaded through
    the model-space snap/pick path — invasive + regression-prone; deferred as tech debt.
  - F3 (adjacent pre-existing, noted): the plot's arc loop applies no GLOBAL `plottable` filter at all
    (unlike lines/circles). Out of REQ-028 scope (REQ-029 plotting); noted, not fixed here.
- In scope:     make the PDF plot honor per-viewport frozen layers (lines/polylines/circles/arcs/
                survey points), matching the on-screen viewport render; add a domain test for the
                "hidden only in that viewport" acceptance.
- Out of scope: F2 (snap/pick through frozen), F3 (arc global plottable), ellipses/text/hatch inside
                viewports (not rendered in viewports at all — a REQ-027 overlay limitation).
- Smallest change: add `IsLayerFrozenInViewport(vp, layer)` to the existing per-viewport emit guards
                in `PlotLayoutsToPdf`; no new types/helpers (predicate already exists in PaperSpace.hpp).

## 3. Architectural boundary check
- [x] No — proceed. Reuses the existing `IsLayerFrozenInViewport` predicate inside the plot's existing
      per-viewport loop; no new abstraction/layer/dependency/global/data-format/algorithm. Filtering at
      plot time is the IO/Renderer's own concern (mirrors the on-screen render already doing this).

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | REQ-028 already ships — how to proceed? | 2026-07-13 | User chose "Audit + harden first", then build and manually verify. |

## 5. Assumptions
```
ASSUMPTION-1: plotted output should match the on-screen viewport (WYSIWYG) for frozen layers.
- Because:       REQ-028 says a frozen layer is "hidden only in that viewport"; a plot renders that
                 viewport, so a frozen layer must not print. Acceptance names on-screen visibility;
                 the plot is the same viewport materialized.
- Risk if wrong: low — if a user wanted frozen-on-screen-but-plotted they'd use global plottable; the
                 per-viewport freeze is explicitly a per-viewport hide.
- Validate by:   manual verification (freeze a layer in one viewport, plot, confirm it is absent there
                 and present in another viewport / in model space).
```

## 6. Plan
- Approach: inside `PlotLayoutsToPdf`'s `for (const Viewport& vp : L.viewports)` loop, extend each
  entity's skip guard to also skip when `IsLayerFrozenInViewport(vp, layer)`. Match the on-screen
  render's covered set exactly: lines, polylines, circles, arcs, survey-point crosses.
- Files/functions to touch:
  - `src/io/PdfPlot.cpp` — `PlotLayoutsToPdf` per-viewport emit guards (5 entity types).
  - `tests/PaperSpaceTests.cpp` — add REQ-028 "hidden only in its own viewport" test.
- Test approach: happy path = domain test asserts a layer frozen in vpA is hidden there, visible in
  vpB, and freeze/thaw is independent per viewport (the predicate the render + plot both apply).
  failure mode = already covered by "Viewport frozen layers toggle" (empty default, toggle off).
  Plot integration itself is not unit-testable (needs PDFium; the test target is pure-compute only) →
  covered by manual verification.
- Steps:
  - [x] PdfPlot: per-viewport frozen filter on lines/polylines/circles/arcs/survey points
  - [x] Domain test: frozen hidden only in its own viewport (REQ-028)
  - [x] Spec: withdraw REQ-034 (decision log + status + traceability + roadmap)
  - [x] Roadmap: mark 3e done, 3d withdrawn
  - [ ] build clean + tests green
  - [ ] manual verification (user runs the app)

## 7. Workflow-specific notes
- Bug/hardening: root cause of F1 = plot path predated/did not reuse the per-viewport freeze predicate
  the screen render uses; the two render paths diverged. Regression guard is manual (plot needs PDFium,
  excluded from the pure-compute test target) + the domain test locking the acceptance predicate.

## 8. Implementation log
- 2026-07-13 Audit: REQ-028 shipped in TASK-007; verified domain/persist/render/UI/tests present.
  Found F1 (plot ignores per-viewport freeze), F2 (snap/pick through frozen — deferred debt), F3 (arc
  global plottable missing — out of scope, noted).
- 2026-07-13 PdfPlot.cpp: added `IsLayerFrozenInViewport(vp, …)` to the line/polyline/circle/arc/
  survey-point emit guards; arc loop switched to an indexed loop to resolve its layer via userArcAttrs.
- 2026-07-13 PaperSpaceTests.cpp: added "Frozen layer is hidden only in its own viewport (REQ-028)".
- 2026-07-13 Spec: withdrew REQ-034 (unneeded complexity; was blocked on the GL clip pass) with a
  decision-log entry; recorded the REQ-028 plot-freeze hardening decision; updated the roadmap.

## 9. Self-verification
- [x] build-project        — PASS (clean Ninja Release build; no new warnings)
- [x] architecture-review  — PASS (no new abstraction/global/dependency; reuses existing predicate)
- [x] code-review          — PASS (smallest change; matches on-screen render set; readable)
- [x] dependency-audit     — n-a (no dependency change)
- [x] performance-review   — n-a (one O(frozenLayers) membership test per plotted entity; plotting is
      not a hot path; frozen sets are tiny)
- [x] testing              — PASS (53/53 ctest green, incl. new REQ-028 independence case);
      plot integration = manual (PDFium excluded from the pure-compute test target)

## 10. Verification result
- Submitted:  (pending)
- Verdict:    PASS — user manual verification confirmed 2026-08-18
- Findings:   F2, F3 recorded as technical debt (see §2 / decision log).

## 11. Outcome
- Requirements satisfied: REQ-028 (Acceptance met: yes — now including plotted output)
- Tests added:            tests/PaperSpaceTests.cpp "Frozen layer is hidden only in its own viewport (REQ-028)"
- Refactors:              none
- Docs updated:           spec/requirements.md (REQ-034 withdrawn + traceability), spec/project.md
                          (decision log ×2), spec/roadmap.md (3e done / 3d withdrawn)
- Technical debt noted:   F2 (snap/pick through a frozen floating-viewport layer); F3 (arc global
                          plottable filter missing in plot) — both in the decision log / §2.
- Done:                   2026-08-18 (user verified in app: frozen layer absent in that viewport only, on screen and in the plot)
