# TASK-022 — REQ-044: import DXF STYLE table as live text styles; REQ-050: MTEXT sized by viewport scale

- Type:    feature + fix (un-defer REQ-044 DXF round-trip; new REQ-050)
- Status:  done — user verified in app 2026-08-18 (all four manual checks in §6)
- Opened:  2026-07-29
- Owner:   chetjones003

## 1. Authority
- Goal:         AutoCAD-parity text: editing a text style updates the drawing (incl. imported text);
               MTEXT plots at a constant sheet height per viewport.
- Requirements: REQ-044 (named text styles — amended to register the DXF STYLE table as live styles);
               REQ-050 (MTEXT sized by the viewport scale it is drawn through).
- Constraints:  no new dependency/abstraction/global; reuse the bake-on-write model (ADR-020) and the
               existing `CurrentViewport`/`InFloatingModelSpace` seam; no stored coordinate/height change.
- Decision:     recorded 2026-07-29 (project.md): (a) amend REQ-044/ADR-020 to un-defer the DXF STYLE
               round-trip — user chose "import the STYLE table as real styles" over link-all-to-Standard
               or a manual per-object assign; (b) accept REQ-050.
- Owning subsystem: IO (DxfIo) for the style round-trip; Renderer (CadUi) for MTEXT sizing.

## 2. Root cause (font "bug")
The STYLE dialog already re-bakes and re-renders correctly; freshly-drawn text (styleName="Standard")
updates on a style edit. But **DXF-imported text carried no style reference** — `DxfIo` set the font but
left `styleName` empty, because ADR-020 deferred the DXF STYLE round-trip. `RebakeAllForStyle` matches on
`styleName`, so a style edit skipped every imported label. Since the user works in Civil 3D/DXF, all their
viewport text was imported text → editing a style's font changed nothing.

## 3. Plan
- DxfIo: register each `STYLE` record as a GoSurvey `TextStyle` (font + italic); link imported TEXT/MTEXT
  to it by name; mark the DXF group-40 height as a per-text override (`ovHeight`) so a style edit changes
  font/oblique but not each label's height. Never clobber a pre-existing drawing style (fill an unset font
  only).
- CadUi: size plain MTEXT off the active viewport's `scaleModelPerPaperIn` (floating model space) else the
  drawing plot scale; TEXT and survey-label MTEXT unchanged.

## 4. Architectural boundary check
- [x] Decision recorded before code (project.md 2026-07-29). No new dependency/layer/global/abstraction.
      DxfIo reuses the tested bake-on-write model (TextStyle.hpp) and `.gs` persistence; CadUi reuses the
      existing viewport seam. Depends downward only. No coordinate/height stored-data change (REQ-101).

## 5. Implementation log (2026-07-29)
- src/io/DxfIo.cpp:
  - `#include "TextStyle.hpp"`.
  - `RegisterDxfTextStylesIntoDrawing(st, dxfStyleMap)` — add-or-fill each DXF style into `st.textStyles`
    (called right after `BuildTextStyleTable`).
  - `resolveStyle` now also returns the canonical registered style name.
  - TEXT and MTEXT import: set `an.styleName = <resolved name>` and `an.ovHeight = true`.
- src/ui/CadUi.cpp: MTEXT branch of `drawAnnotationVisual` sizes off `CurrentViewport(cmd)->scaleModelPerPaperIn`
  when one is active (non-survey MTEXT only), else `modelUnitsPerPlottedInch`.
- Spec: REQ-044 statement/acceptance/revision + traceability amended; REQ-050 added; two decision-log rows.

## 6. Verification
- Build: `cmake --build build --target GoSurvey` — clean (CadUi.cpp + DxfIo.cpp compiled + linked).
- Tests: `ctest` — 66/66 pass. The contract imported text relies on (font re-bakes while an `ovHeight`
  override is preserved) is covered by `TextStyleTests` "Editing a style re-bakes referencing text except
  overridden properties". An in-suite DXF import test is not feasible — the pure test target does not link
  DxfIo's dependency chain — so DXF import stays on the IO/DXF **manual** verification convention.

### Manual verification (user)
1. Import a Civil 3D DXF with TEXT/MTEXT. Open STYLE — the drawing's DXF styles are listed.
2. Select an imported style, change its Font Name → all imported TEXT/MTEXT using that style change font in
   the viewport; their heights stay put.
3. Editing a *different* style leaves that text alone (per-style, not global).
4. MTEXT: double-click into a paper-space viewport whose scale differs from the drawing scale — MTEXT sizes
   to that viewport's scale; single-line TEXT and survey-point labels look unchanged.

## 7. Notes / debt
- MTEXT sizing change is on-screen only this pass; matching the PDF-plot MTEXT sizing to the per-viewport
  scale is a noted follow-up if the two diverge (recorded on REQ-050).
- Case-collision edge: a DXF style whose name differs only in case from an existing style registers a
  second style; imported text still links correctly. AutoCAD writes "Standard", so this is rare.

## 8. Verification result
- Submitted:  2026-08-18
- Verdict:    **PASS** — user confirmed all four §6 manual checks in the running application:
              DXF styles listed in STYLE, a font edit re-baked imported text with heights preserved,
              a different style left that text alone, and MTEXT sized to the viewport scale while
              TEXT and survey labels were unchanged.
- Done:       2026-08-18
