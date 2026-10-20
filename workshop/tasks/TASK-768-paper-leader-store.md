# TASK-768 — Paper-space multileader store (increment 1 of REQ-113 widening)

- Type: feature
- Status: in progress
- Opened: 2026-10-20
- GitHub issue: #768

## 1. Authority

- Requirement: REQ-113 (accepted; widened by D-2026-10-20-a). Related: REQ-037, REQ-039, REQ-367.
- Acceptance: REQ-113 items 5-8 are delivered across increments; this one only gives the sheet a store for multileaders (the other new types already have a home — see scope).

## 2. Scope

- Audit result: paper TABLE already exists as a `CadAnnotation` of kind Table in `paperTexts`; fills already have `paperFilledRegions`. Spline, image and point have no model-space type on `beta` to mirror (SPLINE is open PR #980; the #803 spec dropped Point), so they are follow-ups.
- In scope: `PaperLayout::paperMultileaders` (+attrs); `PaperEntityRef::Type::Leader`; click pick, window/crossing select, snap (tip, landing), MOVE/COPY/ERASE, Qt sheet display, PDF plot (paths + label).
- ROTATE/SCALE/MIRROR refuse it by name, as in model space (REQ-367).
- Out of scope (later increments): drawing an MLEADER on a sheet, clipboard (model-space clipboard does not carry multileaders either), DWG/DXF read/write (increments 2-4), spline/image/point stores.

## 3. Architecture check

Mirrors the existing per-type paper stores (ADR-009); no new layer or abstraction.

## 4. Tests

`tests/PaperLeaderTests.cpp` — pick, box select, snap, move/copy, erase, refusal of rotate/scale/mirror.

## 5. Assumptions / debt

- Creation path is import (increment 2) until a paper MLEADER command is specified.

## 6. Increment 2 — DWG paper fills and multileaders (this branch)

- DWG export: paper fills -> HATCH and paper multileaders -> MULTILEADER (R2010+) or LEADER + MTEXT, through `WriteFilledRegionHatch` / `WriteMultileaderEntity`, extracted from `FillFromState` so model and paper space share one writer.
- DWG import: HATCH, MULTILEADER and LEADER on a sheet land in `paperFilledRegions` / `paperMultileaders`.
- Fix found on the way: the model-space reader dropped every entity flagged `entmode == 1` (paper space), so `duke-main-clean-r2018.dwg` lost its sheet text, curves and polylines (REQ-113 acceptance 2). `ImportPaperEntity` now reads them as sheet content.
- Still open: paper TABLE <-> DWG TABLE, paper size/orientation, viewport view direction/frozen layers/lock/on-off, DXF.

## 7. Increment 3 — DWG paper size and viewport state

- Layout plot settings now carry the sheet's size and orientation (they were hard-coded to ANSI B and never read).
- Viewport view direction/twist, frozen layers, lock and on/off are written and read. `Viewport` gains `on` and `locked`; the draw and plot code does not honor them yet (carried, not enforced).
- Import no longer drops viewports wider than 50 in: the sheet-spanning "chrome" viewport is recognised by spanning the sheet (the viewport id does not survive a save).
- Still open: paper TABLE <-> DWG TABLE, DXF, enforcing `on`/`locked`.

## 8. Increment 4 — DXF (DxfIo, the app's own DXF reader and writer)

- Export: paper fills as solid HATCH; viewport on/off, view direction, twist, lock flags and frozen layers (group 331, handles numbered from the same layer order the LAYER table uses); multileaders written as LINE segments plus text (DxfIo has no multileader writer in model space either) and reported.
- Import: layouts from the OBJECTS LAYOUT records (name, tab order, size, rotation); paper entities (group 67, or inside a `*Paper_Space` block) read by the model-space reader into a scratch state and moved onto the layout named by group 410 (`AppendScratchToPaperLayout`, shared with the DWG reader); VIEWPORT with its state.
- Found on the way: an empty `*Paper_Space` skeleton block must not create a layout (it broke `headless.dxf-export-stable`).
- Still open: paper TABLE <-> DWG/DXF TABLE; DXF pattern hatches (solid only, as in model space); honoring viewport `on`/`locked`.

## 9. Increment 5 — first-class paper tables

- `PaperLayout::paperTables` (`CadTable`, REQ-148) with `PaperEntityRef::Type::Table`: pick, box select, corner snap, MOVE/COPY/ROTATE/SCALE/MIRROR/ERASE, Qt display, PDF plot (outline, dividers, cell text).
- DWG: grid LINEs + cell MTEXT (same fallback as model space, `WriteCadTable` now takes origin and scale explicitly); DXF: LINEs + text, logged. DWG TABLE import lands in `paperTables` via `AppendScratchToPaperLayout`.
- Fixed on the way: sheet fills were never drawn by the Qt viewport (only plotted).
- Debt: legacy sheet tables stored as `CadAnnotation::Kind::Table` in `paperTexts` keep the old path; a native AutoCAD TABLE read is untested (no sample).

## 10. AutoCAD oracle check (REQ-113 acceptance 7)

Fixture writer: `GOSURVEY_ORACLE_DIR=<dir> GoSurveyTests "[.oracle768]"` (hidden test). It writes five feature levels (0 none, 1 sheet geometry, 2 + viewport, 3 + viewport state, 4 + fill/leader/table) at R2000, R2010 and R2018, plus a DXF. Opened with `accoreconsole.exe /i <file> /s <script>` (AutoCAD 2026); the script counts entities per layout with `ssget "_X"` filtered on group 410. `SECURELOAD` must be 0 and the lisp goes inline in the script: `(load ...)` is refused.

Result 2026-10-20:
- R2000 DWG opens at every level. Layout "Plan" at level 4 holds VIEWPORT 1, CIRCLE 1, ARC 1, TEXT 1, LWPOLYLINE 2, HATCH 1, LEADER 1, LINE 7 (1 + 6 table), MTEXT 5 (leader label + 4 table cells); layout "Tall" holds its CIRCLE.
- DXF opens: HATCH 1, LINE 8, TEXT 5, MTEXT 1, circle/arc/polyline, VIEWPORT 2 (ours plus the sheet's own).
- The second LWPOLYLINE on "Plan" is the invisible boundary scaffold `dwg_add_HATCH` leaves behind (same as model space).
- R2010 and R2018 DWGs are rejected ("Drawing file is not valid", status 434) **at level 0**, a drawing with no paper space at all, so the cause is not issue #768. Not re-checked against `beta`.
