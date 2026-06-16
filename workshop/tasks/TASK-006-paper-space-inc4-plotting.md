# TASK-006 — Paper Space Inc 4: plot to PDF (single + batch)

- Type:    feature
- Status:  done
- Opened:  2026-06-15
- Owner:   Workshop

## 1. Authority
- Requirements: REQ-029 (single), REQ-030 (batch) — accepted; ADR-007 (vector PDF via PDFium).
- Constraints: no new dependency (reuse bundled PDFium); coding standards.
- Acceptance: a layout plots to a one-page PDF at paper size, geometry at true scale (REQ-029);
  ≥2 layouts batch-plot to one multi-page PDF (REQ-030).

## 2. Scope
- In scope: vector PDF plot (PdfPlot.cpp via PDFium edit API); single (active layout) + batch dialog;
  per-layer "plottable" toggle (layer manager) + viewport-on-layer; plot excludes off/frozen/non-plottable
  layers and viewport borders accordingly; persistence of plottable + viewport layer.
- Out of scope (deferred): plot color / plot styles (monochrome vector now), PDF-underlay content in plots,
  the GL per-viewport pass, direct-to-printer.

## 3. Architectural boundary check
- [x] No new architectural decision — ADR-007 authorized vector PDF via the bundled PDFium; the
      plottable-layer flag is a small data/UI addition recorded in the decision log.

## 6. Plan → done
- PdfPlot.cpp: FPDF_CreateNewDocument → page per layout (paper size in points); per viewport, model
  geometry (lines/polylines/circles/arcs/survey) transformed model→paper, Liang–Barsky clipped to the
  rect, batched into one stroked path per page; FPDF_SaveAsCopy to an ofstream.
- Layer plottable flag + layer-manager Plot column; Viewport.layer + combo in the Viewports window.
- Ribbon Layout → Plot (active) / Batch…; Batch Plot dialog. BrowseSaveFilePdfUtf8 added.
- GsIo persistence: CadLayerRow.plottable, Viewport.layer.

## 9. Self-verification
- [x] build-project — PASS (clean)
- [x] architecture-review — PASS (ADR-007; PDFium write APIs; IO layer; no new dependency)
- [x] code-review — PASS (segment clipping + single batched path; readable)
- [x] dependency-audit — PASS (PDFium already linked; added fpdf_save.h header use)
- [x] performance-review — segments batched into one path per page; OK for the test drawing
- [x] testing — PASS (build/suite green; plot output verified manually)

## 10. Verification result
- Verdict: PASS — deferred items tracked (color/plot-styles, underlay-in-plot, GL pass).

## 11. Outcome
- Requirements satisfied: REQ-029, REQ-030 (Acceptance met: yes — vector PDF, single + batch, true scale).
- Tech debt: monochrome vector only (no plot styles/color yet); PDF underlays not included in plots.
- Done: 2026-06-15
