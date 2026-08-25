# TASK-104 — Tabbed ribbon: tab strip + re-home existing sections (REQ-302 increment 1)

- Type:    feature
- Status:  implement
- Opened:  2026-08-25
- Owner:   Workshop (Claude)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         REQ-302 — Tabbed, responsive application ribbon (GitHub issue #83), authorized D-2026-08-25-c
- Requirements: REQ-302 (increment 1 only)   ← accepted
- Constraints:  CON — no unnecessary dependencies (must stay pure ImGui, in-tree); no new abstraction
                without 2+ present-day uses (tab toggle reuses existing `PushModeToggleButtonColors`)
- Acceptance:   (verbatim from spec/requirements.md REQ-302, "Acceptance — Increment 1")
  - tab strip renders at top of ribbon band, 7 tabs in order: Home, Insert, Annotate, View, Manage,
    Output, Survey
  - tab strip reuses `PushModeToggleButtonColors`/`PopModeToggleButtonColors` (CadUi.cpp:6308-6313)
  - clicking a tab sets `cmd.activeRibbonTab`; only that tab's re-homed sections render; a tab with
    no sections shows an empty panel row
  - `activeRibbonTab` persists across restart via UserPrefs.cpp, same shape as `trimState`; default
    Home on a fresh profile
  - switching tabs never touches `cmd.active`, selection, undo stack, or drawing geometry
  - section-to-tab mapping: Home = Edit + (Draw+Modify or Layout); Annotate = Annotate; View = View;
    Survey = Inquiry + Survey; Insert/Manage/Output = empty for now
  - Layers strip and PDF Underlay / Hatch contextual sections render on every tab, unchanged
  - ribbon height grows by exactly the tab strip's height; existing section button sizing unchanged
  - build clean; full regression suite green; manual GUI pass confirms tab switching, persistence,
    and that every ribbon command reachable today is still reachable, under its mapped tab
- Owning subsystem: UI (`src/ui/CadUi.cpp`, `src/ui/CadUi.hpp`) / IO (`src/io/UserPrefs.cpp`) /
  Commands (`src/commands/CadCommands.hpp` — new `AppCommandState` field only)

## 2. Scope
- In scope:
  - `AppCommandState::activeRibbonTab` field (int, 0=Home..6=Survey) + named constants
  - Tab strip UI at the top of `DrawRibbonBar`, reusing existing toggle-button styling
  - Gating the existing Edit/Draw/Modify/Layout/Annotate/Inquiry/Survey/View sections behind their
    mapped tab's condition, with NO change to any section's internal content, condition, or command
  - `UserPrefs.cpp` load/save for `activeRibbonTab`, clamped to [0,6]
  - `ribbonH` constant in `main.cpp` grows by the tab strip's height
- Out of scope:
  - Responsive/overflow layout (increment 2), removing `ImGuiWindowFlags_HorizontalScrollbar`
  - Any new content for Insert/Manage/Output (increment 3)
  - Changing any contextual section's trigger condition (Layout/PDF/Hatch/Layers all keep today's
    conditions, rendering on every tab)
  - Any change to command behavior, icons, tooltips, or layer-strip content
- Smallest change: add the tab strip + one persisted field + wrap existing section blocks in
  `if (cmd.activeRibbonTab == kRibbonTab...)` — no section's internal code is rewritten, only the
  condition it renders under.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed. `activeRibbonTab` is one more `AppCommandState` field persisted the same
          one-line way as `trimState`/`chamferMode` (~20 existing precedents, D-2026-08-23-j already
          declined to generalize this into a registry). The tab strip reuses the existing
          `PushModeToggleButtonColors` toggle-button styling (REQ-025/026 precedent, CadUi.cpp:6308)
          rather than inventing a new button style. No new dependency, no new layer, no public-API or
          `.gs`/DXF data-format change — `gosurvey-user.json` gains one more key, the same way it
          already gains one for every settings-shaped field in the file.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Sequencing: build tab infrastructure first, or the responsive engine first? | 2026-08-25 | Tab infrastructure first (user's explicit choice, recorded D-2026-08-25-c) |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: Edit (Undo/Redo/Copy/Paste) is scoped to the Home tab only, not shown on every tab
- Because:       issue #83's own Home-tab group list does not mention Edit/Undo/Redo as a
                 cross-tab "quick access" concern, and adding a separate always-visible strip for it
                 would be a new UI concept (a quick-access toolbar) beyond what increment 1 scopes
- Risk if wrong: user has to switch to Home to Undo/Redo from the ribbon (Ctrl+Z/Ctrl+Shift+Z keyboard
                 shortcuts are unaffected — this only changes ribbon-button reachability)
- Validate by:   user's manual GUI pass; easy to revisit in increment 3 if it's felt as a regression

ASSUMPTION-2: Tab strip height is a fixed +24px added to the existing 139px `ribbonH` constant
- Because:       the existing file already hardcodes ribbon pixel metrics (kRibbonBottomGutter=9,
                 largeW=60, etc.) rather than deriving them from ImGui font metrics at this call site
- Risk if wrong: tab strip looks cramped or oversized at a different app font size than tested
- Validate by:   manual GUI pass at the default app font
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: within the owning subsystem (`src/ui/`), add one persisted int field, one small tab-strip
  render helper reusing existing toggle-button styling, and wrap each existing section block's
  condition with an `activeRibbonTab` check — no section body is touched.
- Files/functions to touch:
  - `src/commands/CadCommands.hpp` — add `activeRibbonTab` field + `kRibbonTab*` constants near
    `trimState`
  - `src/io/UserPrefs.cpp` — load (clamp [0,6]) + save, same shape as `trimState`
  - `src/ui/CadUi.cpp` — `DrawRibbonBar`: draw the tab strip; wrap Edit+Draw/Modify/Layout under
    Home, Annotate under Annotate, Inquiry+Survey under Survey, View under View
  - `src/app/main.cpp` — `ribbonH` constant grows by the tab strip height
- Test approach:
  - happy path: build; run the full regression suite (Catch2, `GoSurveyTests.exe`) and confirm the
    count matches pre-change (nothing broken by the reflow); manual GUI pass — click every tab, click
    a ribbon button under a non-Home tab (e.g. Zoom Extents under View), confirm it fires; start a
    command, switch tabs and back, confirm the command is still active; restart the app after
    switching to a non-Home tab, confirm it reopens on that tab
  - failure mode: an out-of-range `activeRibbonTab` in `gosurvey-user.json` (hand-edited to e.g. 99)
    loads clamped to a valid tab, not a crash or blank ribbon — this is the one piece of new pure
    logic and gets covered the same way `trimState`'s clamp is (code inspection matching existing
    precedent; no dedicated automated test exists for any settings field's load/save round-trip in
    this codebase today, so none is added here either — consistent with existing coverage, not a gap
    introduced by this task)
- Steps:
  - [x] Add `activeRibbonTab` field + constants to `AppCommandState`
  - [x] Add load/save in `UserPrefs.cpp`
  - [x] Add tab strip rendering to `DrawRibbonBar`
  - [x] Wrap Edit/Draw/Modify/Layout sections under Home tab condition
  - [x] Wrap Annotate section under Annotate tab condition
  - [x] Wrap Inquiry/Survey sections under Survey tab condition
  - [x] Wrap View section under View tab condition
  - [x] Grow `ribbonH` in `main.cpp`
  - [x] Build clean, full regression suite green
  - [ ] Manual GUI pass (user)

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1 above); no tests-first since this is UI reflow with no new
  algorithm — verified by build + full regression suite + manual pass, matching REQ-081's own
  UI-only precedent.

## 8. Implementation log  (append as you work)
- 2026-08-25 — REQ-302 accepted (D-2026-08-25-c), task opened, plan written.
- 2026-08-25 — Implemented: `activeRibbonTab` field + `kRibbonTab*` constants (CadCommands.hpp),
  load/save clamp (UserPrefs.cpp), tab strip + section gating (CadUi.cpp `DrawRibbonBar`), `ribbonH`
  grown by the tab strip height (main.cpp). Restructured the pre-existing
  `if (!ribbonPaperSpace) { Draw; Modify; Annotate; Inquiry; Survey } else { Layout }` into
  independent tab-gated blocks (Home: Edit + Draw/Modify-or-Layout; Annotate: Annotate;
  Survey: Inquiry+Survey; View: View) since Annotate/Inquiry/Survey now belong to different tabs
  than Draw/Modify/Layout — each block keeps its exact original body and `ribbonPaperSpace`
  condition, only the tab gate wrapped around it. Contextual PDF Underlay/Hatch sections and the
  Layers strip left untouched (render on every tab, per REQ-302 acceptance).
- 2026-08-25 — Build: `cmake --build build --config RelWithDebInfo` — GoSurvey.exe, GoSurveyTests.exe,
  gosurvey_headless.exe all link clean (MSVC, vcvars64 x64). No new warnings introduced (diffed
  against pre-change warning set — all warnings present are pre-existing, unrelated lines).
- 2026-08-25 — Tests: `GoSurveyTests.exe` — all tests passed (7,714,750 assertions in 541 test
  cases). `ctest -C RelWithDebInfo` (headless transcript suite) — 591/591 passed, 0 failed (1
  pre-existing disabled test, `headless.dxf-export-stable`, unrelated). Counts match the last
  recorded regression baseline (D-2026-08-25-b: 591/591) — nothing broken by the reflow.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS (GoSurvey, GoSurveyTests, gosurvey_headless all clean)
- [x] architecture-review  — PASS (no Workshop architectural decision; boundary check §3 = No)
- [x] code-review          — PASS (each section's body/condition is byte-for-byte unchanged, only
      re-wrapped in a tab-gate `if`; no command, tooltip, or icon touched)
- [x] dependency-audit     — n/a (no dependency change)
- [x] performance-review   — n/a (one extra `int` compare per section per frame; no hot-path change)
- [x] testing              — PASS (541 Catch2 test cases green, 591/591 headless transcripts green;
      manual GUI pass still pending — see below)

- 2026-08-25 — User's own manual GUI pass (screenshots) found 4 real issues, amended as
  D-2026-08-25-d: (1) tab-strip text flush against the button bottom — added FramePadding +
  gap row; (2) View tab's "2D Wireframe" text clipped — visual-style combo width now measured
  from `ImGui::CalcTextSize` instead of a hardcoded 132px guess; (3) Survey tab still scrolled —
  `RibbonToolsLeft` now sized to the active tab's own precomputed content width instead of a
  blanket window-minus-layer-panel cap, `HorizontalScrollbar` replaced with
  `NoScrollbar`/`NoScrollWithMouse` (partial fix — see REQ-302 Acceptance note on disclosed limits);
  (4) Aligned/Linear moved from Survey's Inquiry into a new Annotate "Dimensions" section; Annotate's
  Text/Mtext/style section relabeled "Text". Rebuilt (GoSurvey.exe; GoSurveyTests/gosurvey_headless
  untouched by this round's files) — 0 build errors, GoSurveyTests.exe 541/541 test cases green.

- 2026-08-25 — User retested: Survey tab still scrolled. Root cause was one level deeper than the
  outer `RibbonToolsLeft` fix: `RibbonSectionBegin` (every individual ribbon panel — Edit, Draw,
  Modify, Inquiry, Survey, all of them) only ever set `ImGuiWindowFlags_NoScrollbar`, never
  `NoScrollWithMouse` — so a panel whose actual content is a hair wider than its computed `width`
  (float rounding in `colW()`) was silently wheel-scrollable with no visible bar. The screenshots
  showed exactly this: Inquiry panel static, Survey panel's own content sliding — one panel
  scrolling internally, not the outer container. Fixed by adding `NoScrollWithMouse` to
  `RibbonSectionBegin`'s `BeginChild` flags, closing this for every ribbon panel, not just Survey's.
  Rebuilt GoSurvey.exe clean, 0 errors, no new warnings.

- 2026-08-25 — User confirmed scrolling is gone, but reported the Survey section's
  Surfaces/Volumes/Grades/Groups column was clipped at the bottom. Root cause: four buttons stacked
  in one `BeginGroup`, but a ribbon column only fits three (`colH`/`rowH`'s fixed 3-row budget) —
  a pre-existing latent bug (the code already had a comment noting "the fourth in a single column
  ... never appeared"), only now visible because scrolling can no longer hide it. Fixed by splitting
  into two 2-item columns (Surfaces/Volumes, Grades/Groups), the same "fourth needs its own column"
  pattern already used in the Modify section. Scanned the rest of the ribbon for the same
  more-than-3-stacked-buttons shape (awk over every `BeginGroup`/`smallBtn`/`EndGroup` block) — no
  other section has it. Rebuilt clean, 0 errors.

## 10. Verification result
- Submitted:  2026-08-25
- Verdict:    PASS (automated); manual GUI pass is the one Acceptance condition that requires the
  user, per this project's own no-UI-automation constraint
- Findings:   4 found and fixed from the user's first manual pass (D-2026-08-25-d, see log above);
  a second manual pass is needed to confirm the fixes read correctly on screen

## 11. Outcome
- Requirements satisfied: REQ-302 increment 1 (Acceptance met: automated conditions yes; manual
  conditions — tab switching, persistence across restart, every ribbon command still reachable —
  pending the user's own pass)
- Tests added:            none (see Test approach above — consistent with existing settings-field
  coverage in this codebase)
- Refactors:              `DrawRibbonBar`'s single flat section sequence split into independent
  tab-gated blocks (no behavior change)
- Docs updated:           spec/requirements.md (REQ-302), spec/project.md (D-2026-08-25-c)
- Done:                   pending manual GUI pass
