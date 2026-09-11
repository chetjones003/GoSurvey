# TASK-211 — Ribbon layout: wire Home tab onto RibbonLayout (issue #326)

- Type:    feature
- Status:  done
- Opened:  2026-09-06
- Owner:   Workshop (Claude)

## 1. Authority
- Goal:         REQ-302/ADR-053 — data-driven ribbon layout engine (issue #321), Home-tab wiring
- Requirements: issue #326 ("Ribbon layout: wire Home tab")
- Acceptance:   Home tab renders through `RibbonLayout::DrawSection`; no section computes its own
  hardcoded width formula; no other tab touched.
- Owning subsystem: UI (`src/ui/CadUi.cpp`, `src/ui/RibbonLayout*`)

## 2. What changed

**Engine extensions** (issue #326 found the #322-#325 foundation only supported a single flat row
of buttons — not enough for Home's real content):
- `RibbonGroupLayout` (`Row`/`Column`/`Grid`) + per-group `gapX`/`gapY`/`gridColumns` on
  `RibbonGroupSpec` (`RibbonLayoutTypes.hpp`).
- `RibbonButtonSpec` gained `disabled`, `tooltip`, `labelBelow`, `iconKind` (opaque int passthrough
  for CadUi.cpp's private `RibbonIconKind`, so real command buttons keep their actual glyphs, not
  just the generic NYI icon), and `fixedHeight` (non-square Fixed buttons).
- Measure/Place passes made layout-aware and fully recursive/2D (previously a single flattened row).
- `RibbonDrawButtonForLayout`/`RibbonLayout::DrawSection` draw disabled state + tooltips.
- All additions are backward-compatible defaults (gapX/gapY=0, layout=Row) — every pre-existing
  `RibbonLayoutMeasureTests`/`RibbonLayoutPlaceTests` case is unchanged and still passes.

**Home tab**: all 9 sections (Palettes, Explore, Optimize, Create Ground Data, Create Design,
Profile & Section Views, Draw, Modify, Clipboard) now build a `ribbonlayout::RibbonSectionSpec` and
render via `RibbonLayout::DrawSection`. Section width comes from `ribbonlayout::MeasureRibbonSection`
— no section hardcodes a width formula. Per explicit user direction, this is NOT a pixel-for-pixel
port of the old hand-tuned metrics (several of the old tabs were already visually inconsistent) —
buttons AutoFit to their real icon/label content, and multi-button groups use `Grid` layout.

## 3. Known hazard — DO NOT use nested `RibbonGroupSpec::groups` in this engine yet

While implementing this issue, wiring a group via **nested sub-groups** (a `Column`-layout
`RibbonGroupSpec` whose content is `.groups` — i.e. a "column of row sub-groups" — rather than
buttons placed directly in `.buttons`) reproduced a **real, repeatable Release-build crash**
(`STATUS_STACK_BUFFER_OVERRUN` / heap corruption reported by ucrtbase, ~9s after launch, every run).

Findings from an extensive bisection (see PR discussion for the full log):
- Reproduced with minimal content (a single nested group, a single button) — not tied to any
  specific button content, icon, id, or string.
- **Not reproducible in a Debug build** (60-90s stable) — Release-optimizer-dependent UB.
- Not reproducible on `beta` before this change (confirmed on unmodified `beta`, 60s stable).
- Every configuration using ONLY flat `Row`/`Column` groups (buttons placed directly, no nested
  `.groups`) or `Grid` layout (also direct `.buttons`, wraps into rows of `gridColumns`) has been
  soak-tested crash-free (120s+, all 9 Home sections active, real command dispatch).
- Root cause was **not** isolated to a specific line despite careful review of
  `MeasureRibbonGroup`/`PlaceGroupItems`'s recursive `.groups` traversal (no index-out-of-bounds,
  no dangling references found by inspection) — it may be a compiler-optimization/UB issue
  (aliasing, a subtle lifetime issue exposed only under inlining) rather than a straightforward
  logic bug.

**Consequence for #327-#339 (the remaining ribbon-tab wiring issues):** do not use nested
`RibbonGroupSpec::groups` (a group whose `.groups` vector is populated) when wiring Insert,
Annotate, View, Manage, Output, Survey, or any contextual tab. Any "stack of rows" shape must go
through `Grid` layout with `gridColumns=1` (or the appropriate N) and buttons placed directly in
`.buttons`, exactly as Home's sections do now. If nested groups are needed for some future tab, this
should be revisited with a real debugger attached (this session could not get a Debug-build repro,
and this box's tooling for attaching to/analyzing a Release-build crash dump was unavailable)
before depending on that code path in another shipped tab. The nested-groups code itself is left
in place (not deleted) since it is unit-tested and may be legitimately safe in other build
configurations — it is just unverified for real UI use and must not be relied on yet.

## 4. Tests / verification
- `RibbonLayoutMeasureTests`/`RibbonLayoutPlaceTests` extended with Column/Grid cases; all pass
  (`GoSurveySnapTests.exe [ribbonlayout]` — 15 cases, 66 assertions).
- Full `GoSurveySnapTests` suite green (114 cases, 1354 assertions) — no regressions.
- Manual: launched the built app, verified all 9 Home sections render with correct icons/labels/
  NYI tooltips/disabled state at the default window width; soak-tested 120s+ with no crash.
- Build: clean Release build, no new warnings.
