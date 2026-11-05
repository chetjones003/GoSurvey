# TASK-issue-770-plot-lineweights — PDF plot honours lineweights and linetypes

- Type:    feature (SPEC-approved behaviour)
- Status:  in progress
- Opened:  2026-10-29
- Owner:   chetjones003 (decision), Claude (implementation)
- GitHub:  #770 (SPEC GAP S3; labels: needs-triage at intake)
- Branch:  feat/implement-issue-770 (from origin/beta)
- Decision: D-2026-10-20-g — "both" (lineweights and linetypes), user choice 2026-10-20. SPEC recorded in PR #1022 (merged 2026-10-21).

## 1. Authority
- Requirement: REQ-446 (accepted, catalogued 2026-10-20). Depends on REQ-049, REQ-048, REQ-029.
- Acceptance (REQ-446):
  1. a line with a 2 mm lineweight plots at a stroke width measurably thicker than the default line;
  2. a dashed linetype plots with gaps; a continuous line plots solid;
  3. a plot with only default lineweights and continuous linetypes is identical to today's plot;
  4. a test exists for lineweight and for linetype.
- Constraints: CON for IO/Renderer; no new dependency; SPEC not changed by this task.
- Owning subsystem: IO / Renderer (src/io/PdfPlot.cpp).

## 2. Scope
- In scope:
  - Model-space lines, polylines, circles, arcs, splines, points, surface edges, dimension strokes, survey-point crosses and the viewport border (src/io/PdfPlot.cpp).
  - Native paper-space lines, circles, arcs, ellipses, splines, points, polylines, multileader leaders and arrows, table grids and blocks (src/io/PdfPlot.cpp).
  - SHX text strokes take their entity's lineweight (never a dash).
- Out of scope:
  - Plot styles (CTB/STB) — D-2026-10-20-g chose "both", not "plus plot styles".
  - The Page Setup "Plot object lineweights" checkbox named in the wiki: the dialog has no such control in code, so lineweights are always honoured. Recorded as debt.

## 3. Implementation approach
- `src/io/PlotStroke.hpp` (new, header-only, pure): `WidthPt` (unset → 0.5 pt, the historical width; otherwise mm × 72/25.4), `WidthKey` (1/100 pt grouping key), `DashUnitIn` (0.045 × half the sheet/viewport paper size — the on-screen `dashPatScale` proportion).
- `PdfPlot.cpp`:
  - Stroke groups are keyed by (colour, width) instead of colour only. Default-only plots emit the same paths as before.
  - `addChain` strokes a chain of paper-inch points. Solid linetypes emit each chord as before; dashed linetypes call `CadTessellateLinetypeChainVc` (the shared on-screen tessellator) over the whole chain so the pattern carries across joints, then clip each dash.
  - Lineweight and linetype resolve ByLayer through the entity's layer (`setLineStyle`).
- Architectural boundary: no new abstraction used once; no SPEC change; no global state.

## 4. Tests
- `tests/PlotStrokeTests.cpp` (new, registered in CMakeLists.txt next to PlotFontTests):
  - unset lineweight → 0.5 pt, key 50; 2 mm → 5.67 pt (acceptance 1);
  - 0 mm is an explicit zero width, not the default;
  - widths round to 1/100 pt for grouping;
  - dash unit proportional to paper size and positive for degenerate sheets;
  - DASHED leaves gaps, CONTINUOUS is one solid run (acceptance 2);
  - a dashed chain of two chords draws the same total dash length as one chord (pattern carries across joints).
- Not covered by a unit test: the `PdfPlot` end-to-end byte output (acceptance 3 — default plot unchanged). Verified by reasoning (default path emits the same chords in the same order) and recorded as a gap, to be checked by a manual plot comparison.

## 5. Documentation review (REQ-453)
- README.md — reviewed; no update needed (no lineweight/linetype/plot-stroke statements).
- PRIVACY.md — reviewed; no update needed.
- In-app wiki (resources/wiki/) — reviewed; `Plotting.md` already lists "Layer colours, linetypes, and lineweights" as plotted, which this change makes true. No update needed.
- Public GitHub wiki — cloned read-only and reviewed; same pages, same statements; no update needed.

## 6. Assumptions and technical debt
- Dash size follows AutoCAD's viewport scale rule with LTSCALE fixed at 1 (D-2026-10-29-a); there is no drawing LTSCALE setting yet.
- Surface edges join into one chain only where consecutive edges share an end point; unconnected triangle edges still restart per edge.
- Dashes are laid in paper space after projection, so a rotated camera dashes in projected units.
- The viewport border uses default style, not its layer's lineweight.
- Lineweight 0 mm plots as a zero-width stroke (the PDF thinnest-line convention).
- The Page Setup "Plot object lineweights" checkbox is not implemented (see §2).

## 7. Build and verification status
- Baseline Release build started before the change (`build.bat`).
- Pending: rebuild with the change, run `ctest --preset ninja-release -R "^build[.]output-layout$"` and the new test case, and review the diff.
