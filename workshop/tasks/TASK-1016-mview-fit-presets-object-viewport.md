# TASK-1016 — Add MVIEW fit, presets, and closed-polyline viewports (issue #1016)

- Type: feature
- Status: implementation complete; ready for combined PR review (Civil 3D comparison inconclusive)
- Opened: 2026-11-03
- GitHub issue: #1016

## 1. Authority

- Requirement: REQ-490 (accepted), with REQ-027/031/033/113/201.
- Acceptance: REQ-490 Acceptance 1–6.
- Owning subsystems: Commands/Domain for MVIEW and viewport state; Qt Renderer and IO for clipping, plotting, and persistence.

## 2. Scope

- In scope: retain two-click rectangular MVIEW; add full-sheet Fit, fixed 2/3/4 tilings, and Object from a closed paper-space polyline; make clip shape editable, undoable, plottable, and persistent in DWG/DXF.
- Out of scope: importing a viewport boundary from another drawing; a vertex-drawn polygon command; arbitrary selected object types; changing existing viewports when a preset is created.
- Smallest change: extend the existing viewport value with optional copied boundary geometry, extend the current MVIEW command route, and update its existing renderer/plotter/native DWG-DXF paths without a new dependency or rendering abstraction.

## 3. Architectural boundary check

- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
  - [x] Yes — the viewport gains optional value-owned clip geometry and DWG/DXF writers/readers gain standard native clip-boundary handling. **Specification decision recorded in ADR-077 before Workshop.** No new dependency or layer is planned.
- Ownership: the `Viewport` value owns its copied boundary. It does not refer to the polyline array index or track later source edits. The source polyline remains unchanged.

## 4. Questions

| # | Question | Asked (date) | Answer |
|---|----------|-------------|--------|
| Q1 | Should Fit fill the usable sheet or zoom drawing extents? | 2026-11-03 | Fill the usable sheet; user then specified the full sheet rectangle edge-to-edge. |
| Q2 | What should Object accept and how should it clip? | 2026-11-03 | Follow closed-polyline outlines. |
| Q3 | Which 2/3/4 arrangements? | 2026-11-03 | Fixed: two equal side-by-side; one full-height left plus two stacked right; equal 2×2 grid. |
| Q4 | May the source polyline or existing viewport state be changed? | 2026-11-03 | No. The confirmed feature request leaves the source and existing layout objects unchanged; each option adds viewport(s). |
| Q5 | How should PDF text behave at polygon clip edges? | 2026-11-03 | Keep searchable text. Labels anchored outside the polygon are omitted; glyphs anchored inside may cross the edge. |

## 5. Assumptions

None. Full sheet means the active layout's stored paper width and height; printable margins are not modeled (REQ-025). Object boundaries preserve the source polyline's supported straight and bulge-arc segments as a copied viewport value (ADR-077).

## 6. Plan

- Approach: add command options to the existing MVIEW route; compute the selected fixed rectangles from active sheet dimensions; copy valid closed paper polylines into viewport-owned boundary state; clip the existing Qt viewport composition and PDF plot paths; read/write the standard DWG/DXF clip-boundary references; preserve the rectangular fast path for legacy viewports.
- Files/functions to inspect and likely touch:
  - `src/commands/PaperSpace.hpp` and the viewport creation/selection/edit/undo paths in `src/commands/CadCommands.cpp` / `.hpp`.
  - `src/qt/EditorViewport.cpp` for on-screen clipping and viewport interaction bounds.
  - `src/io/PdfPlot.cpp` / `.hpp` for vector plot clipping.
  - `src/io/LibreDwgCad.cpp` and `src/io/DxfIo.cpp` for standard clip-boundary import/export; `src/io/GsIo.cpp` only if the current drawing-owned persistence path requires the new value.
  - Focused paper viewport, command, editor, plot, and DWG/DXF IO tests in their existing test files; add a headless transcript if command coverage needs a scripted user-visible seam.
  - `resources/wiki/Commands/mview.md` and any affected product documentation after implementation.
- Test approach:
  - Happy path: default two-click rectangle; Fit extents; exact count and coordinates for each tiling; closed polyline with straight and bulge-arc edges; visible and plotted geometry clipped to the outline; undo/redo; DWG and DXF round-trip.
  - Failure path: Model-space invocation, open polyline, non-polyline selection, degenerate boundary, and Esc all create no viewport and preserve source/layout/undo state.
  - Compatibility: legacy rectangular viewports retain their old default state and rendering; existing viewports and source paper objects remain unchanged when a new option is used.
- Steps:
- [x] Implement the optional viewport boundary value and command creation helpers.
- [x] Add Qt and PDF clipping while retaining the rectangle fast path.
- [x] Add native DWG/DXF clip-boundary read/write and focused round-trip coverage.
- [x] Add command, undo/redo, and invalid-input regression coverage; update MVIEW help.
- [x] Run final focused tests/build.
- [x] AutoCAD interoperability check: accoreconsole AUDIT reports zero errors/repairs for clipped R2000/R2010/R2018 DWGs; AutoCAD 2025 DXFOUT preserves the clip-boundary reference and bulged polyline. Full UI render verification remains outstanding; paired comparison is inconclusive because Civil 3D is unavailable.
- [x] Pre-PR documentation review; see `TASK-paper-space-issue-batch-pr.md`.

## 7. Workflow-specific notes

- Feature: accepted scope implementation is in progress. Tests are planned at the command, render/plot, and native-file seams.

## 8. Implementation log

- 2026-11-03: User confirmed #1016 scope. SPEC added as REQ-490, with D-2026-11-03-i and ADR-077. Verification plan review approved.
- 2026-11-03: Implemented Fit, fixed 2/3/4 arrangements, Object selection from closed paper-space polylines, copied clip-boundary persistence in GS/DXF/DWG, Qt clipping, PDF vector-stroke clipping, and viewport label-anchor filtering. Added command geometry, two-click compatibility, invalid selection, undo/redo, clipped PDF path, and persistence round-trip coverage; updated MVIEW wiki and ribbon help. The DWG/DXF import readers accept two-vertex bulged closed boundaries.
- 2026-11-03: User confirmed searchable PDF text, accepting possible glyph overflow at polygon edges; labels whose insertion anchors lie outside remain omitted. REQ-490 records this as the intended plot behavior.
- 2026-11-03: Rebuilt Release successfully; `[req490],[issue768]` passed (258 assertions / 26 cases); all six `qt.editor.shard-*` tests passed (430.61 seconds total). `git diff --check` passed.
- 2026-11-04: Added hidden `[.][oracle1016]` fixture writer for a paper viewport with a bulged polygon clip. The Release build passed, the fixture writer passed (7 assertions), and AutoCAD 2025 accoreconsole AUDIT reported `Total errors found 0 fixed 0` for R2000, R2010, and R2018 DWGs. DXF fixture generated for future UI/open verification. Focused `[req490]` passed (113 assertions / 10 cases). AUDIT verifies database integrity but not visible clipping behavior in AutoCAD UI.
- 2026-11-04: AutoCAD 2025 accoreconsole exported the R2018 clipped-viewport DWG to `autocad_roundtrip.dxf`. The resulting VIEWPORT record retains status 32800 and clip-boundary handle `340 → 39`; handle 39 resolves to the closed LWPOLYLINE with the expected vertices and bulge 0.25. This confirms AutoCAD can read and write the clip data; visual clipping behavior remains unverified without the full UI.

## 9. Self-verification

- [x] build-project — Windows Release build passed after implementation changes and on 2026-11-03 recheck
- [x] architecture-review — plan PASS; implementation review finds no boundary violation; AutoCAD DWG database audit passed; visible product behavior remains pending
- [x] code-review — plan PASS; implementation review finds no source-level blocker; focused tests pass; AutoCAD DWG database audit passed
- [x] dependency-audit — no new dependency introduced
- [x] performance-review — clipping is a render/plot path change; no regression identified in focused review; product comparison pending
- [x] testing — `[req490]` 113 assertions / 10 cases passed on 2026-11-04; oracle fixture writer passed (7 assertions); AutoCAD 2025 AUDIT passed for three DWG versions. `[req490],[issue768]` 258 assertions / 26 cases passed on recheck. All six `qt.editor.shard-*` tests passed on 2026-11-03 recheck (430.61 seconds). `build.output-layout` passed.

## 10. Verification result

- Submitted: 2026-11-03
- Verdict: APPROVE plan for implementation.
- Findings: no blocking plan findings. AutoCAD 2025 accoreconsole rejected MVIEW on the Model tab; a scripted paper-layout query did not produce usable output. Autodesk Help fetch failed and Civil 3D is not installed, so product behavior/visual comparison remain external verification items.

## 11. Outcome

- Requirements satisfied: implementation and focused regression coverage complete; external validation gates remain.
- Tests added: MVIEW arrangements, two-click workflow, Object creation/refusals, undo/redo, PDF stroke clipping, and GS/DXF/DWG clip round-trips.
- Refactors: none.
- Docs updated: SPEC and task/verification records; MVIEW in-app wiki and ribbon help updated. README, PRIVACY, in-app wiki, and public manual wiki were reviewed; outcomes are in `TASK-paper-space-issue-batch-pr.md`.
- Visual evidence: `build/artifacts/Release/req480-paper-viewport-clipping.png`; the Qt test confirms a point inside the outline shows viewport rendering and a point outside it but inside its bounding box shows the sheet.
- External limits: Civil 3D is unavailable, so paired screenshot review is **inconclusive**. AutoCAD 2025 accepted and rewrote the clip data, but full UI render verification remains outstanding. PDF model strokes are geometrically clipped; model text anchors outside the polygon are omitted, while glyphs anchored inside may extend beyond the edge because PDFium exposes no arbitrary per-object clip-path setter in the bundled API.
- Completion: implementation, GoSurvey render/plot coverage, AutoCAD DWG audit, and DWG→DXF clip-data round-trip are complete. Full AutoCAD UI visual clipping was not directly checked. Civil 3D is unavailable, so the paired product comparison is inconclusive and no parity pass is claimed. The user requested closure of issues included in the combined PR; retain this validation limitation in the PR and issue closure note.
