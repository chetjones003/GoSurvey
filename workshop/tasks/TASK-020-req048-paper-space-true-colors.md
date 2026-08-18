# TASK-020 — REQ-048/049: true colors in paper space (incremental)

- Type:    feature
- Status:  done — A+B and C1+C2 all user-verified in app (C1+C2 confirmed 2026-08-18)
- Opened:  2026-07-13
- Owner:   chetjones003

## 1. Authority
- Goal:         Paper-space fidelity (AutoCAD parity) — ASSUMPTION-1 follow-up from REQ-046
- Requirements: REQ-048 (true colors, screen+plot); REQ-049 (plot native sheet geometry + text)
- Constraints:  reuse the existing color resolver; VP Color override + selection/hover still win;
                frozen/off/non-plottable visibility unchanged; no geometry/coordinate change (REQ-101);
                no new dependency. Amends ADR-007 (full-color plot).
- Acceptance: see spec/requirements.md REQ-048 / REQ-049.
- Owning subsystem: UI / Renderer (screen) + IO / Renderer (plot).

## 2. Increment plan
- (A) On-screen true colors — model-through-viewport + native sheet geometry.  ← DONE
- (B) Plot true colors — model-through-viewport stroked geometry.              ← DONE
- (C1) Plot native sheet STROKED geometry (lines/circles/arcs/ellipses/polylines), colored. ← DONE
- (C2) Plot native sheet TEXT (REQ-049), SHX as strokes. ← DONE (via ADR-022 font-module relocation).
      Architectural blocker resolved: user chose to relocate the SHX stroke module to a shared lower
      layer (`src/font/ShxFont`, decoupled from imgui via `Shx::Vec2`), with the imgui draw adapter in
      `src/ui/ShxDraw`. `PdfPlot` (IO) now depends downward on `src/font` — no upward dependency.
      TrueType sheet text is NOT yet plotted — logged once per plot as debt (REQ-201), not silent.
      Native sheet FILLED regions in the plot remain deferred per ADR-011.

## Implementation log — increment C (2026-07-13)
- ADR-022 recorded (relocate SHX font module) + architecture.md §10 module layout updated.
- git mv src/ui/ShxFont.{hpp,cpp} -> src/font/; Glyph strokes ImVec2 -> plain Shx::Vec2; removed the
  imgui `DrawText` from the module. New src/ui/ShxDraw.{hpp,cpp} holds the `Shx::DrawText` adapter.
  CadUi include ShxFont.hpp -> ShxDraw.hpp (only caller). CMake: src/ui/ShxFont.cpp -> src/font/ShxFont.cpp
  + src/ui/ShxDraw.cpp; added src/font to include dirs.
- PdfPlot.cpp: (C1) native sheet stroked geometry (lines/circles/arcs/ellipses/polylines) emitted in
  paper inches, colored via `sheetRgb`, honoring plottable. (C2) native sheet TEXT: SHX single-line +
  MTEXT (flattened via MtextRichFlattenToPlain, already used by DxfIo) emitted as strokes through the
  shared font module; TTF text logged as debt. (win `max` macro avoided with a ternary.)

## 3. Architectural boundary check
- [x] Recorded before code: REQ-048 + REQ-049 + ADR-007 amendment + decision log (2026-07-13). Color
      resolution reuses `ResolveEntityRgbaForViewport` (no new abstraction/dependency/global). Increment
      C's PDF text rendering is a new capability under REQ-049 (recorded).

## 4. Implementation log
- 2026-07-13 (A) CadUi.cpp: `vpBaseCol(layer, entityColor)` now resolves the TRUE entity/layer color
  (VP override still wins) instead of the flat `kVpModelCol` (removed); all viewports, always. Native
  sheet geometry: added `paperAttrs` + `paperTrueCol`; `paperCol` and `drawPaperText` now use the true
  entity/layer color; `kPaperGeomCol` removed. Selection/hover colors unchanged; filled regions were
  already color-resolved.
- 2026-07-13 (B) PdfPlot.cpp: `overrideRgb` -> `resolveRgb(layer, entityColor)` — VP override wins, else
  the entity's true color (was black). Applied to line/polyline/circle/arc/survey-point plot emit. The
  per-color path grouping (REQ-046) carries the colors. Border stays black.

## 5. Assumptions
```
ASSUMPTION-1: survey points are colored by their layer (they carry no per-entity color).
- Because:       SurveyPoint has `layer` but no color field.
- Risk if wrong: none — matches model-space treatment.
- Validate by:   manual (a survey point on a colored layer shows that color in the viewport/plot).
```

## 6. Self-verification (increments A+B)
- [x] build-project        — PASS (clean build; kVpModelCol/kPaperGeomCol removals leave no unused warns)
- [x] architecture-review  — PASS (spec recorded first; reuses the resolver; no new global/dependency)
- [x] code-review          — PASS (override + sel/hover precedence preserved; readable)
- [x] dependency-audit     — n-a
- [x] performance-review    — a layer lookup (`FindDrawingLayerRowCi`, linear over layers) per entity per
      frame in the overlay + per plotted entity; layers are few — acceptable; matches model-space cost.
- [x] testing              — 58/58 ctest green (no domain change); colors = manual per UI-REQ convention
- [x] user manual verification — PASS. A+B confirmed earlier; C1+C2 (native sheet geometry + SHX text plot in true colors) confirmed by user 2026-08-18.

## 7. Verification result
- Verdict: PASS — all increments user-verified; C1+C2 confirmed 2026-08-18.

## 8. Outcome (interim)
- Requirements satisfied: REQ-048 increments A+B (screen + model-vp plot). REQ-048-C + REQ-049 pending.
- Tests added:            none (color rendering — manual per convention; domain resolver already relied upon).
- Docs updated:           spec/requirements.md (REQ-048/049 + traceability), spec/project.md (decision log).
- Technical debt:         REQ-049 (native sheet + text plotting) not yet done — sheets still plot without
                          their native geometry/text; tracked as increment C.
- Done:                   2026-08-18 (A+B verified earlier; C1+C2 verified in app)
