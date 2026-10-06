# TASK-732-p1 — Built-in PDF viewer window (issue #732, phase 1)

- Type:    feature
- Status:  submitted
- Opened:  2026-10-06
- Owner:   Claude

## 1. Authority
- Requirements: **REQ-387** (accepted, PR #733), ADR-067, D-2026-10-06-a
- Acceptance: unit tests for open/sizes/errors, cache cap + farthest-first eviction, read-ahead plan
  order and cancellation; `BENCH PDFVIEW` first page <= 250 ms and no viewer-caused frame > 16 ms on
  a 500-page file; manual: every route opens the viewer.
- Owning subsystem: `src/pdf/` (pure core + PDFium wrapper), `src/ui/` (window)

## 2. Scope
- In: `PdfViewerCore` (layout, visible range, read-ahead plan, bounded cache, synthetic PDF),
  `PdfDocument` (PDFium, streamed open, cancellable render), `PdfViewerWindow` (floating ImGui window,
  one render worker, textures, thumbnails, zoom/pan/page nav), `PDFVIEW` + `BENCH PDFVIEW` commands,
  Project-tab double-click routed to the viewer (`OpenWithDefaultApp` removed), `pdfview-bench`
  DevShell test, `PdfiumMutex` shared with `PdfPlot`.
- Out: annotations (REQ-388), split (REQ-389), search, printing.

## 3. Architectural boundary check
- No new dependency or layer. PDFium work is in `src/pdf/`; the window is UI; the Commands layer only
  sets request fields on `AppCommandState`.

## 4. Assumptions and technical debt
- The viewer manager is file-static state in `PdfViewerWindow.cpp` (like other UI windows' caches);
  freed by `ShutdownPdfViewers()` before the GL context goes.
- `PdfiumMutex` (recursive) is taken by the viewer, `PdfPlot` and the PDF underlay code (`PdfAttach.cpp`
  loader/thumbnail/raster/snap/build sites). `PlotLayoutsToPdf` holds it for the whole plot, so a plot
  waits for a page render in progress and viewer renders wait for the plot (user-initiated, not scrolling).
- The "attached underlay: open" and "recent files" routes named in REQ-387 clause 1 do not exist in the
  app today (the recent list holds drawings only; underlays have no open action), so the routes wired
  are Project-tab double-click and the `PDFVIEW` command.
- Measured (release devshell build, 500 pages, 400 lines/page): first page 19-25 ms, sharp 31-39 ms;
  viewer cost per frame p95 0.8-2.1 ms, worst 6-15 ms; whole-frame worst ~35 ms (not yet attributed; it appears at the same size on every run, the viewer share is the line above).
