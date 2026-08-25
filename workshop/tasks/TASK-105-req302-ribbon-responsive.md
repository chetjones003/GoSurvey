# TASK-105 — Ribbon responsive layout engine (REQ-302 increment 2)

- Type:    feature
- Status:  done
- Opened:  2026-08-25
- Owner:   Workshop (Claude)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         REQ-302 — Tabbed, responsive application ribbon (GitHub issue #83), increment 2
                opened D-2026-08-25-e
- Requirements: REQ-302 (increment 2 only)   ← accepted
- Constraints:  CON — pure ImGui, in-tree, no new dependency; no new abstraction beyond what
                ADR-038 scopes; no behavior change to any command; no change to increment 1's
                tab strip, persistence, or contextual-section (Layers/PDF Underlay/Hatch)
                render-on-every-tab rule
- Acceptance:   (verbatim from spec/requirements.md REQ-302, "Acceptance — Increment 2")
  - `RibbonBreakpoint` (Wide/Medium/Narrow) computed per active tab per frame from available
    width vs. that tab's own wideW/mediumW — no persisted state
  - Wide renders byte-for-byte as increment 1 does today
  - Medium: every section renders compact (icon-only small buttons, tighter widths), same
    sections/commands, none dropped
  - Narrow: sections fit left-to-right at Medium metrics until the next wouldn't fit; the rest
    collapse into one "More" button whose popup renders them at full Wide metrics — every
    command stays reachable
  - `RibbonToolsLeft` sized to `min(fittedW, available)` — never wider than available, so no
    clipping
  - no horizontal or vertical ribbon scrollbar at any tested window width (full guarantee,
    not increment 1's partial one)
  - switching breakpoints (resize) never touches `cmd.active`, selection, undo stack, geometry
  - build clean; full regression suite green; manual GUI pass at multiple widths (incl. 2
    narrower than increment 1 was tested at) confirms Wide/Medium/Narrow render correctly, the
    More popup works, no scrollbar/clip anywhere tested
- Owning subsystem: UI (`src/ui/CadUi.cpp`)

## 2. Scope
- In scope: `DrawRibbonBar`'s own section layout for Home (model + paper/Layout), Annotate
  (model), Survey (model), View — the tabs that have real content today. Breakpoint decision,
  compact button metrics, Narrow overflow popup.
- Out of scope (explicitly deferred, recorded not silently dropped):
  - True row-wrap / ribbon-height growth per breakpoint (ADR-038 Alternative 3) — popup-based
    overflow is this increment's mechanism instead.
  - Insert/Manage/Output tab content (increment 3 — currently still empty, trivially "Wide"
    since there is nothing to overflow).
  - The fixed-width Layers strip (`kLayerPanelW = 500.f`) and the PDF Underlay / Hatch
    contextual sections — increment 1 already established these "render on every tab,
    unchanged"; this increment does not add responsive behavior to them. If the app window is
    narrower than `kLayerPanelW` plus a minimal tools width, that edge case is not solved here
    (pre-existing gap, not introduced by this task) — noted as residual, same as increment 1's
    own disclosed partial fixes.
  - The View tab's visual-style combo width and Annotate's text-style combo width do not shrink
    in Medium/Narrow (combos aren't button-label compaction candidates the same way) — labeled
    buttons around them do compact.
- Smallest change: reuse the SAME per-section render code (the large lambda bodies already
  written for each section) for both Wide and Medium rendering by threading one mutable
  `curCompact` bool that `colW()`/`smallBtn()` read — no per-button-call-site changes needed,
  since `largeBtn`/`gridBtn` sizes do not depend on compact (grid cells are already icon-only;
  large buttons already carry their label below a small icon and are left unchanged).

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] Yes → escalated as ADR-038 (spec/architecture.md), approved by the user 2026-08-25
          before this task's implementation began. Proceeding now under that recorded decision.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Approve ADR-038 design (measure-then-decide breakpoints + shared overflow popup) before any code? | 2026-08-25 | Approved as proposed |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: "Available width" for the breakpoint decision is RibbonStrip's own content width
  (ImGui::GetContentRegionAvail().x measured immediately before RibbonToolsLeft's BeginChild)
  minus the fixed 500px layer strip (kLayerPanelW) and the ItemSpacing gap between them.
- Because:       That is the exact width RibbonToolsLeft + RibbonLayerStrip already compete for
  today (RibbonLayerStrip is a fixed-width sibling placed via SameLine right after
  RibbonToolsLeft's EndChild, `CadUi.cpp:3172-3174`); no other width is meaningful here.
- Risk if wrong: Breakpoint fires too early/late relative to when clipping would actually occur.
- Validate by:   Manual GUI pass at multiple widths (user).

ASSUMPTION-2: In Narrow mode, at least the FIRST section for a tab always renders inline even if
  its own Medium width already exceeds available width, rather than pushing everything into the
  popup and leaving the tab looking empty.
- Because:       An empty-looking tab with only a "More" button reads as broken; issue #83 asks
  the ribbon to "occupy the amount of space necessary," and always showing at least one real
  section keeps the tab recognizable. This is a UI judgment call, not a spec-mandated number.
- Risk if wrong: At an extremely narrow window, that first section can still clip — a disclosed,
  pre-existing-class edge case (true row-wrap, deferred per ADR-038 Alternative 3, would be the
  real fix). Not expected to occur at any realistically supported window width.
- Validate by:   Manual GUI pass; revisit if the user's own pass hits this at a supported width.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: Add `RibbonBreakpoint` enum and a single mutable `curCompact` bool inside
  `DrawRibbonBar`. `colW()`/`smallBtn()` branch on `curCompact` (icon-only, narrower, when true).
  Wrap each tab's section body (currently `RibbonSectionBegin(id,title,W,panelH){...}
  RibbonSectionEnd();`) in a `std::function<void()>` lambda, keeping every line of button/command
  code inside untouched — only the width argument passed to `RibbonSectionBegin` changes (picks
  the Wide- or Medium-computed width depending on `curCompact` at call time, since `colW()`
  itself already resolves that). Compute each tab's `wideTotal`/`mediumTotal` once per frame by
  evaluating the same width formulas twice (`curCompact=false` then `true`). Decide breakpoint
  from available width vs. those totals. A small `PlaceRibbonSections` driver renders the tab's
  section lambdas in order: Wide/Medium → all inline, `curCompact` set once for the whole tab;
  Narrow → greedily place at Medium metrics until the next section wouldn't fit (Assumption-2:
  first section always placed), then a single "More" button opens a popup that renders whatever
  didn't fit with `curCompact=false` (full Wide metrics/labels). `RibbonToolsLeft`'s width becomes
  `min(what was actually placed inline, available)`.
- Files/functions to touch: `src/ui/CadUi.cpp` — `DrawRibbonBar` only (~line 2377-2946). No other
  file.
- Test approach: this is UI-only rendering code with no headless entry point (same as increment
  1 — `verification/skills/` precedent for ImGui rendering: build-clean + full existing
  regression suite (Catch2 + headless transcripts) proves no non-UI regression, manual GUI pass
  proves the rendering itself, since this project has no UI automation (`GUI hover is not
  automatable` precedent, ADR-031's own headless design deliberately excludes ImGui rendering).
  Failure mode covered: build must still succeed with 0 warnings-as-errors if that's configured;
  full suite must stay exactly as green as before this change (any red is this task's own
  regression, since nothing else is touching the codebase concurrently).
- Steps:
  - [ ] Add `RibbonBreakpoint` enum near `RibbonLabel` (~line 2295).
  - [ ] Add `bool curCompact = false;` and rewrite `colW()`/`smallBtn()` to branch on it, inside
        `DrawRibbonBar` before the per-tab width computations.
  - [ ] Compute each `wXxx` width twice (curCompact=false → `W.wXxx`; curCompact=true →
        `M.wXxx`), via a small local struct, replacing the current single-pass block
        (~line 2450-2480).
  - [ ] Wrap each tab's section body in a lambda (Edit, Draw+Modify, Layout, Text, Dimensions,
        Inquiry, Survey, View) — no change to any line inside a body.
  - [ ] Write `PlaceRibbonSections` driver + breakpoint decision; replace the flat per-tab
        `ribbonToolsW` switch and the unconditional inline section calls with the driver call
        per tab.
  - [ ] Rebuild; run the full regression suite; fix anything red.
  - [ ] Self-verify (build-project, architecture-review, code-review, testing); hand off for the
        user's manual GUI pass at multiple widths.

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1, ADR-038 approved). Tests-first not applicable — no headless
  path for ImGui rendering (see Test approach); automated regression suite is the safety net for
  everything this change could accidentally break outside rendering.

## 8. Implementation log  (append as you work)
- 2026-08-25 — REQ-302 increment 2 opened (D-2026-08-25-e), ADR-038 recorded and approved, task
  opened, plan written.
- 2026-08-25 — Implemented per plan, `src/ui/CadUi.cpp` only (`DrawRibbonBar` and three new small
  static helpers just above it). No line inside any section's button/command code changed — each
  section's existing body was wrapped in a `RibbonSectionSpec` closure verbatim; only what decides
  its width and whether it renders inline or inside the "More" popup is new. `colW()`/`smallBtn()`
  now branch on a `curCompact` bool that `DecideRibbonFit`/`RenderRibbonFit` set per section at
  render time (Medium: every section compact; Narrow: inline sections compact, popup sections at
  full Wide metrics, per ADR-038 (b)). Added `#include <functional>` for `std::function`.
- 2026-08-25 — Rebuilt clean (GoSurvey.exe, GoSurveyTests.exe, gosurvey_headless.exe — vcvars64 x64,
  MSVC/VS "18" toolset). No new warnings in `CadUi.cpp` (diffed against pre-change warning list —
  every warning present is at a line this task did not touch). `GoSurveyTests.exe`: 541/541 test
  cases green (unchanged from the pre-task baseline). `ctest -C RelWithDebInfo`: 591/591 headless
  transcripts passed, 0 failed (1 pre-existing disabled test, `headless.dxf-export-stable`,
  unrelated) — exact match to the documented baseline. No non-UI regression from this change.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS (GoSurvey, GoSurveyTests, gosurvey_headless all clean, 0 new
      warnings)
- [x] architecture-review  — PASS (no Workshop architectural decision beyond ADR-038, which was
      escalated and approved before implementation; no new dependency/layer/ownership change)
- [x] code-review          — PASS (self-reviewed: every section body is byte-identical to
      increment 1's shipped code, just wrapped in a closure; `DecideRibbonFit` is pure and has no
      ImGui call, satisfying the "must run before BeginChild needs a size" constraint; `curCompact`
      is reset to `false` at the end of both `computeTabWidths` calls and at the end of
      `RenderRibbonFit`, so no stale compact state can leak into the unrelated contextual
      PDF/Hatch sections that render right after)
- [x] dependency-audit     — PASS (n/a — no dependency change; `<functional>` is a standard-library
      header)
- [x] performance-review   — PASS (n/a — `DecideRibbonFit` is O(sections) per tab, at most 3
      sections; same order of magnitude as increment 1's own per-frame width arithmetic)
- [x] testing              — PASS (541/541 Catch2, 591/591 headless — see log above; this is
      UI-rendering code with no headless entry point, same as increment 1, so these two suites are
      the full extent of what can be automated — see Test approach in §6)

## 10. Verification result
- Submitted:  2026-08-25
- Verdict:    PASS — automated (build-clean, 541/541 Catch2, 591/591 headless) and manual (user
  confirmed the GUI pass: "looks good", 2026-08-25) both green. No findings raised.

## 11. Outcome
- Requirements satisfied: REQ-302 increment 2 (Acceptance met: yes — all conditions, including the
  manual GUI pass at multiple window widths, confirmed by the user)
- Tests added:            none (UI-rendering code with no headless entry point — see Test approach,
  §6; consistent with increment 1's own precedent)
- Refactors:              `DrawRibbonBar`'s per-tab section rendering changed from unconditional
  inline calls to deferred `RibbonSectionSpec` closures placed by `DecideRibbonFit`/
  `RenderRibbonFit` (no behavior change to Wide — it renders byte-for-byte as before)
- Docs updated:           spec/requirements.md (REQ-302 Acceptance — Increment 2), spec/project.md
  (D-2026-08-25-e, D-2026-08-25-g), spec/architecture.md (ADR-038)
- Done:                   2026-08-25
