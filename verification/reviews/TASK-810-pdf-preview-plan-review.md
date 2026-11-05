# Verification review — TASK-810 PDF plot preview plan

- Date: 2026-11-04
- Layer: Verification (plan review, before implementation)
- Verdict: **APPROVE**

## Authority and acceptance

REQ-486 is accepted from the user's confirmed feature request. It maps to observable checks: the active sheet's saved page size/placement/style appear in a one-page vector PDF; the existing viewer opens without a filename prompt; its annotation and Save As tools remain usable; the drawing and undo state stay unchanged; and plot errors are shown without a false preview. REQ-029/445/485 govern PDF content and REQ-201 requires visible failure.

## Architecture

- **Layering:** the Qt ribbon routes the user action; `PlotLayoutsToPdf` in IO produces the document; `PdfWindow` displays it. This uses existing ownership boundaries and no new output backend.
- **Ownership:** one temporary directory belongs to the preview viewer and is removed with that viewer. If its existing unsaved-annotation close prompt is cancelled, the viewer remains open and continues owning the directory. Save As writes a separate copy, as it does for ordinary PDFs.
- **Data/API/dependencies:** no drawing schema, file format, output-device configuration, public service, or dependency is added. A narrow viewer method/field for temporary-directory ownership is acceptable within the current subsystem.
- **Scope:** one active paper layout only. Batch preview, DWF, raster, direct printing, Plotter Manager, and CTB/STB editing remain excluded.

## Risks and controls

1. **Wrong active space:** validate a paper layout before creating any temporary output; model space produces a clear message and no viewer.
2. **Plot failure:** propagate `PlotLayoutsToPdf`'s log/error, do not open the viewer, and let RAII remove the temporary directory.
3. **PDFium concurrency:** call the writer to completion before asking the PDF viewer to load the file; the existing plotter/viewer mutex continues to govern PDFium use.
4. **Temporary-file lifetime:** retain the directory for as long as the PDF document/viewer may read it; release only when the viewer is actually destroyed, not when a close with unsaved edits is cancelled.
5. **Drawing isolation:** snapshot/check layout values and undo depth in the UI regression; Preview must not take an undo snapshot or mutate the source drawing.
6. **UI evidence:** capture the Output tab and opened preview under `build/artifacts/`. Civil 3D is absent on this machine, so the mandated paired comparison must be reported as inconclusive rather than passed.

## Required tests

- Preview opens exactly one PDF page for the active paper layout and uses the plotter's saved Page Setup and CTB/STB values.
- No save-file dialog is invoked by the initial preview action; the viewer exposes the existing annotation and Save As actions.
- Saving annotated output creates the chosen independent file and leaves drawing state unchanged.
- A missing/invalid selected table shows the plot failure, opens no PDF viewer, and leaves no persistent temporary directory.
- Preview refuses model space without using layout zero as a fallback.
- Temporary output exists while the viewer is open and is removed on successful viewer close; a cancelled close retains it.
- Existing REQ-445 and REQ-485 PDF tests continue to prove placement and plot-style output.

## Implementation plan review

The plan is the smallest change that meets the requirement: bind the existing disabled ribbon control, call the existing single-layout PDF writer into a uniquely owned temporary directory, then open the resulting PDF in the existing viewer. Keep annotation and Save As paths unchanged. Update only the affected in-app help. No new architecture decision is required. No blocking findings.

**Plan approved for Workshop implementation.**
