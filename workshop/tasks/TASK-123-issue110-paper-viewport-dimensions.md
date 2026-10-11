# TASK-123 — Draw model dimensions through Paper Space viewports (#110)

- Type:    bug
- Status:  done
- Opened:  2026-08-27
- Owner:   Workshop

Upstream issue: chetjones003/GoSurvey#110.

## 1. Authority
- Goal:         paper space / drawable entities (REQ-027)
- Requirements: REQ-027 (accepted) — model geometry renders inside each viewport clipped to its rectangle at the viewport's scale
- Constraints:  CON-06 smallest change; no new layer
- Acceptance:   GitHub #110 AC list (lines, extensions, arrows, text, style, Model→VP→Screen, alignment, zoom/pan/scale, clip, no bleed, multi-VP, model-space unchanged, tests, all dim kinds)
- Owning subsystem: UI overlay (`CadUi.cpp`) + IO plot (`PdfPlot.cpp`) + domain geometry (`CadDimGeom.hpp` / `CadDimStroke.hpp`)

## 2. Scope
- In scope: model DimAligned / DimLinear / DimAngular drawn and plotted through layout viewports; header-only stroke builder + tests
- Out of scope: model TEXT/MTEXT through viewports; rotated viewports (not in Viewport); annotative scaling beyond existing plottedHeight × viewport scale
- Smallest change: project existing dim strokes with the same `m2s` / clip the linework already uses

## 3. Architectural boundary check
- [x] No — header-only extraction of geometry already in CadCommands.cpp (same pattern as other testable geom)

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | none | — | Paper-native DimAngular included because the shared stroke path covers it |

## 5. Assumptions
```
ASSUMPTION-1: Dimensions stay in cadAnnotations (model store) and are projected, not copied onto the sheet.
- Because: the issue is Model Space → Viewport → Screen
- Risk if wrong: paper-owned dims would be a different store
- Validate by: issue repro steps
```

## 6. Plan
- Approach: CadDimBuildWorldStrokes; viewport overlay + PdfPlot consume it; overlay skips dims when a layout is active so they are not painted in paper inches
- Files: CadDimGeom.hpp, CadDimStroke.hpp, CadCommands.hpp/.cpp, CadUi.cpp, PdfPlot.cpp, DimViewportTests.cpp, CMakeLists.txt
- Tests: happy path all three kinds; degenerate fail; scale/pan/multi-VP; clip in/out
- Steps: [x] geom header [x] overlay [x] plot [x] tests

## 7. Workflow-specific notes
- Bug: root cause = model annotation overlay gated off on layouts and viewport loop never drew cadAnnotations
- Regression test: DimViewportTests would fail without ModelToPaperIn of dim strokes

## 8. Implementation log
- 2026-08-27 implement + `./dev/test` 650/650 PASS

## 10. Verification result
COMPLETION REPORT — TASK-123 — 2026-08-27
- Requirements satisfied: REQ-027 + GitHub #110 (Acceptance met: yes)
- Summary: Model dimensions project through Paper Space viewports and plots via shared world strokes.
- Tests: DimViewportTests (10 cases)
- Verification verdict: PASS
- Architectural decisions: none (header-only extraction of existing geom)
- Dependencies: none
