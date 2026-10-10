# TASK-021 — REQ-049: plot TrueType sheet text (embed the real font)

- Type:    feature (completes REQ-049's recorded TTF-text debt)
- Status:  done — user verified a plotted PDF in app 2026-08-18 (after the §4b fixes)
- Opened:  2026-07-15
- Owner:   chetjones003

## 1. Authority
- Goal:         Paper-space plot fidelity (AutoCAD parity) — title blocks must plot as composed.
- Requirements: REQ-049 (plot native sheet geometry + text). SHX text already plotted (TASK-020/C2);
                this task closes the TrueType-text debt left there.
- Constraints:  no new dependency (existing PDFium text APIs only); IO-layer only (PdfPlot); reuse
                sheetRgb/REQ-048 color and the existing SHX line-splitting; never silently drop text.
- Decision:     recorded 2026-07-15 (project.md) — user chose EMBED THE REAL TTF over a base-14
                substitute or SHX-stroke stand-in. REQ-049 revision note updated.
- Owning subsystem: IO / Renderer (PdfPlot).

## 2. Plan
- Resolve the text's family → its .ttf under `C:/Windows/Fonts` (alias table for title-block families
  whose file name differs, else de-spaced `<family>.ttf`); embed via `FPDFText_LoadFont`.
- Emit each line as a real PDF text object: `FPDFPageObj_CreateTextObj` (size = H / 0.7 cap-em ratio),
  `FPDFText_SetText` (UTF-16), `FPDFPageObj_SetFillColor` (REQ-048 color), `FPDFPageObj_SetMatrix`
  (rotation + baseline origin, matching the SHX path), `FPDFPage_InsertObject`.
- Fonts loaded once per document (cache keyed by family), closed after the save.
- If a family can't be embedded, degrade to the closest base-14 standard font and log it once (REQ-201).
- Extract the pure logic (family→file candidates, base-14 substitution, UTF-8→UTF-16) to a testable
  header (`io/PlotFont.hpp`) — the OrthoConstrain/ColorContrast precedent.

## 3. Architectural boundary check
- [x] Decision recorded before code (project.md 2026-07-15). No new dependency (PDFium already present),
      no new layer/global/abstraction. Font-path convention matches FontRegistry / CadCommands
      (`C:/Windows/Fonts`). Stays inside the IO layer; depends downward only.

## 4. Implementation log (2026-07-15)
- src/io/PlotFont.hpp (new, pure/header-only): `TtfCandidates`, `StandardSubstitute`, `Utf8ToUtf16`,
  `LowerAscii`.
- src/io/PdfPlot.cpp: `ResolveTtfPath` (pairs candidates with the Fonts dir); `plotFont` lambda (embed
  or substitute, cached, warns once); `emitTtfLine` (real PDF text object); text loop unified behind an
  `emitLine` that routes SHX→strokes / TTF→text object; `closeFonts` before each `FPDF_CloseDocument`.
- Font-path convention: hardcoded `C:/Windows/Fonts` (matches FontRegistry/CadCommands; avoids the
  `std::getenv` MSVC deprecation warning).
- tests/PlotFontTests.cpp (new): 4 cases — alias mapping, de-spaced fallback + empty, base-14 substitute
  selection, UTF-8→UTF-16 (ASCII/2-byte/3-byte/invalid→U+FFFD/empty).

## 4b. Fixes after first manual verification (2026-07-29)
User plotted a title block and found two defects vs. the on-screen sheet:
1. **Missing colors** — the red logo (a `paperFilledRegions` solid fill) did not plot at all. The plot
   rendered lines/circles/arcs/ellipses/polylines/text but never filled regions (the ADR-011 gap recorded
   in REQ-049). Fix: added a solid-fill pass in `PdfPlot.cpp` — one PDF path per region, every loop closed,
   `FPDF_FILLMODE_ALTERNATE` (even-odd, matching the overlay's scanline even-odd), colored by the region's
   entity/layer color (no REQ-048 white/black adaptation — the overlay does not adapt fills). Inserted
   before the linework so fills sit underneath, as on screen.
2. **TTF text mis-positioned** — center/middle MTEXT values ("DRY COOLER", "BOLT ASBUILT", "OMEGA",
   "XAI MEMPHIS") plotted at their box's top-left, colliding with the "PROJECT:/TITLE:/CLIENT:/LOCATION:"
   labels. The plot's MTEXT branch always drew top-left and ignored `mtextAttach`, while the on-screen
   overlay honors it (CadUi.cpp ~8719-8731). Fix: the MTEXT branch now computes the same col (left/center/
   right) and row (top/middle/bottom) offsets. Per-line width comes from `Shx::MeasureWidthPx` (SHX) or a
   new `measureTtfWidthIn` (throwaway text obj → `FPDFPageObj_GetBounds`) so mixed SHX/TTF sheets align the
   same way. Single-line TEXT is unchanged (the overlay treats its insertion as top-left).
- Build: clean, no warnings (only PdfPlot.cpp recompiled). Tests: 66/66 green (logic is PDFium emission —
  manual per convention). User re-plotted 2026-08-18 and confirmed both defects are resolved.

## 5. Assumptions
```
ASSUMPTION-1: CAD text "height" is treated as cap height; TTF em = H / 0.7 (nominal cap-height ratio).
- Because:       matches the SHX path (which scales so cap height = H) so mixed SHX/TTF sheets align;
                 PDFium gives no pre-layout cap height to derive the exact ratio.
- Risk if wrong: TTF text slightly taller/shorter than nominal for fonts far from a 0.7 cap ratio.
- Validate by:   user plots a title block and compares TTF text height to the on-screen sheet.
```

## 6. Self-verification
- [x] build-project        — PASS (clean build, no warnings; getenv warning eliminated by the Fonts-path convention)
- [x] architecture-review  — PASS (decision recorded first; IO-only; no new dependency/global; pure logic extracted + tested)
- [x] code-review          — PASS (SHX path unchanged; fonts closed on every exit path; font size/matrix independent — size preserved)
- [x] dependency-audit     — n-a (no dependency added; existing PDFium text APIs)
- [x] performance-review   — fonts cached per document (loaded once per family); text objects are cheap; acceptable
- [x] testing              — 66/66 ctest green (+4 PlotFont cases). PDF text emission itself is IO/PDFium — manual per convention.
- [x] user manual verification — PASS 2026-08-18. Title block plotted: TTF text in the real embedded font at correct position/size/color, filled regions plot, center/middle MTEXT matches the screen.

## 7. Verification result
- Verdict: PASS — user verified a plotted PDF 2026-08-18, after the two §4b defects were fixed.

## 8. Outcome (interim)
- Requirements satisfied: REQ-049 TTF-text debt closed (SHX + TTF sheet text both plot).
- Tests added:            tests/PlotFontTests.cpp (4 cases).
- Docs updated:           spec/project.md (decision log), spec/requirements.md (REQ-049 revision).
- Technical debt:         `.ttc` font collections and non-resolvable families substitute a base-14 font
                          (logged, not dropped); native sheet FILLED regions in the plot still deferred (ADR-011);
                          cap-em ratio is nominal 0.7 (ASSUMPTION-1).
- Done:                   2026-08-18 (user verified in app)
