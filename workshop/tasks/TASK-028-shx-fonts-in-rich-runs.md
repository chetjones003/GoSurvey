# TASK-028 — SHX stroke fonts in rich-text runs and the in-place editor

- Type:    bug
- Status:  self-verify (awaiting user's on-screen confirmation)
- Opened:  2026-07-30
- Owner:   chetj

## 1. Authority
- Goal:         text reads as it does in AutoCAD, whichever way the font was applied
- Requirements: REQ-049 (accepted — "**Stroke (SHX) fonts** … their actual stroke geometry");
  REQ-051 (accepted — the "Text Formatting" panel's font picker); ADR-023 (WYSIWYG in-place editing)
- Constraints:  ADR-022 (glyph geometry in `src/font`, imgui draw adapter in `src/ui`)
- Acceptance:   an SHX font selected from the Text Formatting panel renders as strokes — the same
  whether it was applied to a selection or to the whole object — and the in-place editor shows the
  same glyphs it will commit to.
- Owning subsystem: UI (rich-text render + editor).

## 2. Scope
- In scope:  per-run SHX in `MtextRichFormat`'s layout/draw core, and the matching resolution in
             `RichTextEdit` so measurement, wrapping, caret and drawing all agree.
- Out of scope:
  - `CadUi`'s whole-object SHX branch — see the observation in §11.
  - Bold/italic for SHX runs: stroke fonts carry no bold or oblique face and the existing
    whole-object SHX path ignores both, so the run path matches it rather than inventing a
    faux-bold. `spanFauxBold` is now suppressed for SHX spans so the editor does not pad a cell
    width for a double-strike it will not draw.
- Smallest change: resolve an `Shx::Font*` alongside the existing `ImFont*`, and branch the two
  measure sites and the two draw sites on it.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / public-API change?
    - [x] No — proceed. `src/ui/MtextRichFormat` and `src/ui/RichTextEdit` already sit in the UI layer
          and now include `ui/ShxDraw.hpp`, a sibling in that same layer (ADR-022's intended
          direction). No new module, no header API change.
- One boundary call recorded: the `IsShxFontName` predicate now exists in three places
  (`CadUi.cpp`, `MtextRichFormat.cpp`, `RichTextEdit.cpp`; `PdfPlot.cpp` has a fourth variant).
  The obvious consolidation is `Shx::IsShxFontName` in `font/ShxFont.hpp` — but that is a public-API
  change to a shared module, which CLAUDE.md reserves for a recorded decision. Left duplicated
  deliberately; see DEBT-2.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | The seven sample texts render as TrueType substitutes. The font picker either writes a `[[font:…]]` run (when characters are selected) or sets the object's `fontFamily` (when nothing is). Which did you use? | 2026-07-30 | "Yes — text was selected", i.e. the **run-tag** path. That confirmed the run path as the defect and made this task's scope exact. |

## 5. Assumptions
```
ASSUMPTION-1: an SHX run should stroke-render with the same baseline/cap-height anchoring the
  whole-object SHX path already uses (baseline = top-left + fontPx, capPx = fontPx).
- Because:     nothing specified how a per-run stroke font aligns against TrueType runs on the same line.
- Risk if wrong: SHX runs sit a few pixels off the TrueType runs beside them on a mixed line.
- Validate by: on-screen check of a line mixing an SHX run and a TrueType run.
```

## 6. Plan
- Approach: per run/span, resolve `Shx::Font*` when the effective family ends in `.shx`; measure with
  `Shx::MeasureWidthPx` and draw with `Shx::DrawText` when it is non-null, else the existing TTF path.
- Files/functions to touch: `src/ui/MtextRichFormat.cpp` (`RichWrappedLayoutCore`);
  `src/ui/RichTextEdit.cpp` (`RichTextEditDraw` — both cell-metric passes and the glyph draw).
- Test approach: happy path = an SHX run strokes and measures with SHX advances; failure mode = a
  non-SHX family and the empty (default) family are untouched.
- Steps:
  - [x] confirm the failing path with the user (Q1)
  - [x] add per-run SHX to the render core
  - [x] mirror it in the editor so metrics and glyphs agree
  - [x] build + suite

## 7. Workflow-specific notes (Bug)
- Reported: seven test texts, one per font (romans / romand / romanc / txt / simplex / isocp +
  default), all rendered as proportional TrueType instead of stroke fonts.
- Root cause: **only `a.fontFamily` was ever tested for SHX.** Every render site reads
  `CadIsShxFontName(a.fontFamily) ? Shx::Resolve(...) : nullptr`, so an SHX font applied to a
  *selection* — stored as a `[[font:romans.shx]]` run — never reached `Shx` at all.
  `MtextRichFormat.cpp` resolved runs solely through `FontReg::Resolve`, whose `kShx` table maps
  `romans/simplex/isocp → arial`, `romand/romanc → timesnewroman`, `txt → consolas`. Those five
  substitutes are exactly what the screenshot showed.
- Ruled out first: `Shx::Resolve` itself is healthy on this machine — a probe resolved
  `romans/romand/romanc/txt/simplex/isocp/italic`, with and without the `.shx` extension and in mixed
  case, all from `C:\Program Files\Autodesk\AutoCAD 2026\Fonts`. So this was never a font-location or
  name-normalisation problem.
- Second defect found while fixing the first: `RichTextEdit.cpp` documents that it "mirrors
  MtextRichFormat's resolution so the editor and the drawn MTEXT agree". Fixing only the renderer
  would have broken that mirror — the editor would measure an SHX run with TrueType advances and draw
  TrueType glyphs, while the committed MTEXT stroked, putting the caret and the wrap in the wrong
  place. Both sides were changed together.

## 8. Implementation log
- 2026-07-30 traced the picker's two routes (`CadUi.cpp:6667`); confirmed with the user that the
  selection route was used; verified `Shx::Resolve` healthy; added per-run SHX to the render core and
  the editor; build + suite green.

## 9. Self-verification
- [x] build-project        — PASS (release links clean)
- [x] architecture-review  — PASS (UI-layer siblings only; no header API change — see §3)
- [x] code-review          — PASS (one resolve + two branches per site; the duplicated predicate is
      called out in §3 rather than hidden)
- [x] dependency-audit     — n-a
- [x] performance-review   — PASS by inspection. The per-run/per-span `Shx::Resolve` is cached inside
      the Shx module, and the editor caches per span as it already did for `ImFont`. `Shx` takes
      `std::string`, so measure/draw copy each word (render) or cell (editor) into a reused buffer —
      no per-call allocation after the first growth.
- [ ] testing              — PARTIAL, and this is the honest gap: `GoSurveyTests` is green
      (611 assertions / 98 cases) but covers neither path — both need an ImGui context and an
      `ImFont`, so they cannot be exercised headlessly with the current test target. The underlying
      `Shx::MeasureWidthPx` / `Shx::DrawText` were verified directly in TASK-027; what is unverified
      by machine here is the wiring. **Needs an on-screen check** (§10).

## 10. Verification result
- Submitted:  2026-07-30
- Verdict:    pending user confirmation — re-check the seven samples, and edit one in place to
  confirm the editor shows the same glyphs it commits.
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-049 / REQ-051 (Acceptance: pending the on-screen check above)
- Tests added:            none — see the testing note in §9
- Technical debt:
  - DEBT-2 — `IsShxFontName` now duplicated in three translation units (four counting `PdfPlot`'s
    variant). Removal condition: a recorded decision to expose `Shx::IsShxFontName` from
    `font/ShxFont.hpp`, after which all four collapse to it.
- Observation (NOT actioned — out of scope):
  `CadUi`'s whole-object SHX branch (e.g. `CadUi.cpp:10164`) flattens the wire to plain text via
  `MtextRichFlattenToPlain` and draws it itself, so for an MTEXT whose *base* family is SHX it
  discards per-run colour and bold and only honours a whole-object `[[u]]`. Now that the rich core
  handles SHX per run, that branch is largely redundant and routing those objects through the rich
  core instead would make run formatting work for them too. Left alone here because it changes
  behaviour for already-working DXF-imported text (TASK-026/027), which deserves its own task.
- Docs updated:           none
- Done:                   pending user confirmation
