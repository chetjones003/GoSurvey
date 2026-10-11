# TASK-106 — Ribbon content audit: Insert/View/Output real content (REQ-302 increment 3)

- Type:    feature
- Status:  done
- Opened:  2026-08-25
- Owner:   Workshop (Claude)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         REQ-302 — Tabbed, responsive application ribbon (GitHub issue #83), increment 3
                opened D-2026-08-25-h
- Requirements: REQ-302 (increment 3 only)   ← accepted
- Constraints:  CON — no new command behavior (relocation only); no new dependency; no new
                `RibbonIconKind` (reuse existing icons — see §3); File/Edit/View menu items stay
                unchanged (second entry point, not a move)
- Acceptance:   (verbatim from spec/requirements.md REQ-302, "Acceptance — Increment 3")
  - Insert tab: "Import" section — Import DXF, Import DWG (DWG gated on
    `FindDwgConverter().available()`, same tooltip/disabled behavior as the File menu)
  - View tab: new "Settings" section — one button, `cmd.showSettingsWindow = true`
  - Output tab: "Export" section (Export DXF, Export DWG, same DWG gating) + "Plot" section
    (Plot, Batch Plot — moved from Home's paper-space Layout section, not duplicated)
  - Home tab's Layout section keeps only Rect VP / Poly VP after Plot/Batch Plot move out
  - Manage tab stays empty this increment (nothing left to relocate there)
  - no command's underlying behavior changes; File/Edit/View menu items unchanged
  - switching to Insert/View/Output never touches `cmd.active`, selection, undo stack, geometry
  - build clean; full regression suite green; manual GUI pass confirms all relocated commands work
    from their new ribbon locations and Home's Layout section still renders correctly
- Owning subsystem: UI (`src/ui/CadUi.cpp`)

## 2. Scope
- In scope: `DrawRibbonBar`'s Insert/View/Output tab bodies (new content) and Home's Layout section
  (Plot/Batch Plot removed).
- Out of scope: any new command, any Blocks/Xref/point-cloud/Publish/Standards feature (none exist
  — see REQ-302 Acceptance — Increment 3's correction note), Manage tab content, new icon art.
- Smallest change: reuse `ImportDxfFile`/`ImportDwgFile`/`ExportDxfFile`/`ExportDwgFile`(-equivalent
  DWG export flow)/`FindDwgConverter`/`BrowseOpen/SaveFileDxf/DwgUtf8` exactly as `DrawMainMenuBar`
  already calls them; reuse `RibbonSectionSpec`/`DecideRibbonFit`/`RenderRibbonFit` from ADR-038
  unchanged; reuse existing `RibbonIconKind::PdfAttach` (Import/Export/Plot/Batch — already this
  icon's established loose meaning) and `RibbonIconKind::Layers` (Settings, placeholder).

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed. Pure content relocation onto existing increment-2 infrastructure
          (`RibbonSectionSpec`/`DecideRibbonFit`/`RenderRibbonFit`); no new `RibbonIconKind` (see
          §2); no new persisted state; File/Edit/View menu behavior untouched.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Where should Plot/Batch Plot live now that Output exists (Home only / Output only / both)? | 2026-08-25 | Move to Output; also confirmed Import → Insert, Settings → View (not Manage) |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: Reusing RibbonIconKind::PdfAttach for Import DXF/DWG and Export DXF/DWG, and
  RibbonIconKind::Layers for Settings, rather than adding dedicated icon art.
- Because:     Adding a new RibbonIconKind touches the PNG-texture-loading array/loop
  (CadUi.cpp:2215-2223) and requires an actual icon asset this session cannot produce reliably;
  PdfAttach is already reused this loosely for Plot/Batch Plot (increment 1 precedent). Each
  button also carries a text label, which carries the real meaning.
- Risk if wrong: Icons read as visually confusing/mismatched on screen.
- Validate by:   User's manual GUI pass — same "implement, then fix from screenshots" pattern
  every earlier increment in this requirement used.

ASSUMPTION-2: Import DXF/DWG and Export DXF/DWG render as a single-column smallBtn group (Right
  label, auto-width via colW) rather than largeBtn (Below label, fixed 60px width).
- Because:     "Import DXF"/"Export DWG" are two-word labels that risk overflowing largeBtn's fixed
  largeW; smallBtn's colW()-derived width already handles longer labels correctly elsewhere
  (e.g. "Point Groups", "Elev/Grade").
- Risk if wrong: Purely cosmetic — a layout preference, not a correctness issue.
- Validate by:   User's manual GUI pass.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: Add an `if (cmd.activeRibbonTab == kRibbonTabInsert)` block (new) pushing one
  `RibbonSectionSpec` ("Import"). Add a second `RibbonSectionSpec` ("Settings") inside the existing
  `if (cmd.activeRibbonTab == kRibbonTabView)` block, after the existing "View" section. Add an
  `if (cmd.activeRibbonTab == kRibbonTabOutput)` block (new) pushing two `RibbonSectionSpec`s
  ("Export", "Plot" — "Plot" is the Plot/Batch Plot code moved verbatim out of Home's Layout
  section). Shrink Home's Layout section body/width to Rect VP/Poly VP only. Extend
  `RibbonTabWidths` with `wInsert`, `wViewSettings`, `wOutExport`, `wOutPlot`; shrink `wLayout`'s
  formula to match its smaller body. Add two `static char` path buffers (`ribbonDxfPath`,
  `ribbonDwgPath`) scoped to `DrawRibbonBar`, mirroring `DrawMainMenuBar`'s own statics.
- Files/functions to touch: `src/ui/CadUi.cpp` — `DrawRibbonBar` only.
- Test approach: same as increments 1/2 — no headless entry point for ImGui rendering; build-clean
  plus the full regression suite (Catch2 + headless transcripts) is the automated safety net;
  manual GUI pass is the only way to confirm the rendering itself (ADR-031 precedent).
- Steps:
  - [ ] Extend `RibbonTabWidths` + `computeTabWidths` with the four new width fields; shrink
        `wLayout`'s formula.
  - [ ] Add Insert tab's "Import" section spec.
  - [ ] Add View tab's "Settings" section spec (second push_back inside the existing View block).
  - [ ] Add Output tab's "Export" and "Plot" section specs; move Plot/Batch Plot code out of
        Home's Layout section into Output's "Plot" section verbatim.
  - [ ] Shrink Home's Layout section to Rect VP/Poly VP only.
  - [ ] Rebuild; run the full regression suite; fix anything red.
  - [ ] Self-verify (build-project, architecture-review, code-review, testing); hand off for the
        user's manual GUI pass.

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1). Tests-first not applicable — no headless path for ImGui
  rendering (same as increments 1/2).

## 8. Implementation log  (append as you work)
- 2026-08-25 — REQ-302 increment 3 opened (D-2026-08-25-h), task opened, plan written.
- 2026-08-25 — Implemented per plan, `src/ui/CadUi.cpp` only. `RibbonTabWidths` extended with
  `wInsert`/`wViewSettings`/`wOutExport`/`wOutPlot`; `wLayout`'s formula shrunk to Rect VP/Poly VP
  only. Insert tab: new "Import" section (Import DXF/DWG, DWG gated identically to the File menu's
  `ImGui::BeginDisabled()`-wrapped pattern — an earlier draft gated only inside the click handler,
  which left the button clickable-looking but inert; caught and fixed before building by comparing
  against the File-menu reference pattern). View tab: new "Settings" section appended after the
  existing View section. Output tab: new "Export" section (Export DXF/DWG, same DWG gating fix
  applied) and "Plot" section — the exact Plot/Batch Plot code moved out of Home's Layout section
  verbatim, not duplicated. File/Edit/View menu items untouched.
- 2026-08-25 — Rebuilt clean (GoSurvey.exe, GoSurveyTests.exe, gosurvey_headless.exe). No new
  warnings in `CadUi.cpp` (every warning present is at a line this task never touched — diffed
  against the increment-2 warning list). `GoSurveyTests.exe`: 541/541 test cases green. `ctest -C
  RelWithDebInfo`: 591/591 headless transcripts passed (1 pre-existing disabled test, unrelated) —
  exact match to the established baseline. No non-UI regression.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS (GoSurvey, GoSurveyTests, gosurvey_headless all clean, 0 new
      warnings)
- [x] architecture-review  — PASS (no architectural decision; pure content relocation onto
      increment 2's existing `RibbonSectionSpec`/`DecideRibbonFit`/`RenderRibbonFit`; no new
      `RibbonIconKind`, dependency, or persisted state)
- [x] code-review          — PASS (self-reviewed: Import/Export DWG gating now matches the
      File-menu reference pattern exactly — `BeginDisabled()`/`EndDisabled()` wrapping the button
      itself, not a no-op click-time check; Plot/Batch Plot code is byte-identical to what Home's
      Layout section used to contain, just relocated; static path buffers are new and
      function-local, no aliasing with `DrawMainMenuBar`'s own statics of the same purpose)
- [x] dependency-audit     — PASS (n/a — no dependency change)
- [x] performance-review   — PASS (n/a — same order of magnitude as increment 2's per-frame work)
- [x] testing              — PASS (541/541 Catch2, 591/591 headless — see log above; UI-rendering
      code with no headless entry point, same as increments 1/2)

## 10. Verification result
- Submitted:  2026-08-25
- Verdict:    PASS — automated and manual (user confirmed: "good", 2026-08-25) both green. No
  findings from the manual pass.

## 11. Outcome
- Requirements satisfied: REQ-302 increment 3 (Acceptance met: yes — all conditions, including the
  manual GUI pass, confirmed by the user)
- Tests added:            none (UI-rendering code with no headless entry point, consistent with
  increments 1/2)
- Refactors:              none beyond the planned relocation (Plot/Batch Plot moved, not rewritten)
- Docs updated:           spec/requirements.md (REQ-302 Acceptance — Increment 3, including the
  Statement correction), spec/project.md (D-2026-08-25-h, D-2026-08-25-i)
- Done:                   2026-08-25
