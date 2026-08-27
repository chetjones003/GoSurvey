# TASK-124 — Font and text-style rendering audit (#115)

- Type:    bug
- Status:  done
- Opened:  2026-08-27
- Owner:   Workshop

Upstream issue: chetjones003/GoSurvey#115.

## 1. Authority
- Goal:         drawing text / dimensions (REQ-044, REQ-027, REQ-049, REQ-050)
- Requirements: REQ-044 (accepted) named text styles; REQ-027 viewport contents; REQ-049 sheet text plot
- Constraints:  CON-06 smallest change; no new layer
- Acceptance:   GitHub #115 AC list (audit + honor fontFamily on dims/text in model, paper, viewports; missing fonts graceful; tests)
- Owning subsystem: UI overlay (`CadUi.cpp`) + FontReg + plot (`PdfPlot.cpp`) + pure font-name helper (`font/CadFontName.hpp`)

## 2. Scope
- In scope: dimension font honor (model, paper-native, through viewports, DIMSTY preview/Apply); shared SHX/TTF draw; model TEXT/MTEXT through viewports and plot; missing SHX → TTF fallback
- Out of scope: BLOCK/INSERT/ATTDEF (roadmap Someday — not in the product); tables
- Smallest change: one classification header + reuse existing FontReg/Shx; no new renderer

## 3. Architectural boundary check
- [x] No — header-only name classification (PlotFont.hpp precedent); draw helpers stay in CadUi.cpp

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|------|--------|
| — | none | — | Blocks/attributes do not exist yet; AC treated as N/A |

## 5. Assumptions
```
ASSUMPTION-1: Dimension font is CadAnnotation::fontFamily baked from DimensionStyle::textFont.
- Because: bake-on-write is ADR-020 for text and DIMSTY already wrote fontFamily
- Risk if wrong: live resolve from activeDimensionStyle would ignore per-dim overrides (none exist today)
- Validate by: DIMSTY Apply tests + overlay using a.fontFamily

ASSUMPTION-2: UI chrome (command line, ribbon) stays on the application UI font.
- Because: the issue is drawing/annotation text, not ImGui widgets
```

## 6. Plan
- Approach: classify SHX vs TTF once; draw dim labels through one helper; project TEXT/MTEXT with the same m2s as dims
- Files: CadFontName.hpp, FontRegistry.cpp, CadUi.cpp, PdfPlot.cpp, MtextRichFormat.cpp, DimensionStyle.hpp, CadFontNameTests.cpp
- Tests: happy SHX/TTF/degree; DIMSTY bake; TEXT viewport mapping; missing font does not take SHX path
- Steps: [x] helper [x] overlay [x] plot [x] tests

## 7. Workflow-specific notes
- Bug: paper/viewport dim text used ImGui::GetFont(); DIMSTY preview ignored textFont; FontReg baked size 0; model TEXT omitted from viewport overlay
- Regression test: CadFontNameTests would fail if PreferShxStrokes allowed ° through SHX or bake skipped DimAngular

## 10. Verification result
COMPLETION REPORT — TASK-124 — 2026-08-27
- Requirements satisfied: REQ-044 / REQ-027 / REQ-049 + GitHub #115 (Acceptance met: yes, with BLOCK/INSERT N/A)
- Summary: Dimension and drawing text honor `fontFamily` in model, paper, and viewports; missing SHX falls back to TTF.
- Tests: CadFontNameTests (4 cases) + GoSurveyTests 576 green
- Verification verdict: PASS
- Architectural decisions: none
- Dependencies: none

