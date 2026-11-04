# Verification review — TASK-810 Current-layout PDF Export plan

- Date: 2026-11-04
- Layer: Verification (plan review, before implementation)
- Verdict: **APPROVE**

## Authority and acceptance

REQ-488 is accepted under D-2026-11-04-d and derives PDF geometry and saved plot behavior from REQ-029, REQ-445, and REQ-485. The acceptance checks are observable: one current paper layout becomes a PDF page; its stored paper size, orientation, placement, and CTB/STB selection are honored; cancellation and failure do not mutate drawing state or publish a partial final file; and the action is unavailable outside paper space.

## Architecture

- The ribbon action and file selection stay in Qt UI; `PlotLayoutsToPdf` remains the PDF generation owner.
- A temporary output and `QSaveFile` commit protects the user-selected destination without changing the existing Plot/Batch routes.
- No drawing format, public API, external dependency, output-device backend, or new cross-layer dependency is introduced.
- The selected Current-layout PDF scope does not alter DWF, Display-area choices, Plotter Manager, or Display/Current controls.

## Risks and controls

1. **Wrong space/layout:** gate the action in `RefreshRibbon` and revalidate at invocation, including floating model space.
2. **Settings drift:** pass only the active layout index to the existing plotter, which reads its saved Page Setup and CTB/STB values.
3. **Partial output:** plot into a unique temporary file and commit to the selected destination only after success; discard staging data on all failure and cancel paths.
4. **Drawing mutation:** do not call setup initialization or undo APIs; compare layout/drawing state and undo depth before/after.
5. **Read-only handling:** exporting reads the drawing and writes an external PDF, so keep the action available on read-only drawings.
6. **Visible UI:** capture Output tab and filename dialog. Civil 3D is unavailable, so comparison must be recorded inconclusive.

## Required tests

- Paper-layout action opens the PDF filename dialog; accepting a path produces a one-page vector PDF with the selected layout's page size and saved settings.
- Selected CTB/STB behavior is covered by existing plotter tests, with Export's call path proven to pass the active layout.
- Cancel creates no output and does not change layout, drawing, or undo state.
- Invalid/missing style table reports failure and publishes no final PDF; staged output is removed.
- Action is disabled in model and floating model space, and remains enabled for a read-only drawing.
- Existing Plot and Batch Plot regression coverage remains green.

## Verdict rationale

The plan reuses accepted PDF behavior and avoids changing the existing plot routes. The temporary staging step is directly required by REQ-488's no-partial-file condition and does not change subsystem ownership. No blocking scope, architecture, dependency, or test gap remains. Workshop may implement.
