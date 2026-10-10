# TASK-023 — REQ-051: AutoCAD-style MTEXT "Text Formatting" in-place editor

- Type:    feature
- Status:  done — user verified in app 2026-08-18 (against the reference screenshot)
- Opened:  2026-07-30
- Owner:   chetjones003

## 1. Authority
- Goal:         AutoCAD-parity text editing — a surveyor coming from AutoCAD/nanoCAD finds the
                "Text Formatting" toolbar where they reach for it, with style/font/height/color
                on the panel instead of hand-typed rich-text wire tags.
- Requirements: REQ-051 (new — the MTEXT in-place editor presents an AutoCAD-style Text Formatting
                panel). Builds on and does not alter: REQ-039 (in-place text editor for model +
                paper text, ADR-014c), REQ-044/ADR-020 (named text styles, bake-on-write),
                REQ-040 (precedent: floating draggable panel with position persisted in UserPrefs).
- Constraints:  no new dependency, abstraction, layer, or global; no change to the MTEXT rich-text
                wire format or to `CadAnnotation`'s stored fields; single-line TEXT keeps its
                existing bare in-place box; existing `.gs`/DXF/PDF round-trips unchanged.
- Decision:     to be recorded in `spec/project.md` on approval — accept REQ-051 with the scope the
                user chose: full panel chrome, wire up only what the existing format already
                supports, present-but-disabled for the rest.
- Owning subsystem: UI (`src/ui/CadUi.cpp` — the existing `DrawMtextRichEditorOverlay`), plus
                IO (`src/io/UserPrefs.cpp`) for panel-position persistence, following REQ-040.

## 2. Scope
- In scope:     the two-row "Text Formatting" panel (floating, draggable, position persisted); a
                visual column ruler over the in-place box with a show/hide toggle; wiring the
                controls the current data model already supports; disabled-with-tooltip for the
                rest; model MTEXT (incl. the MTEXT placement command's text entry) and paper-space
                MTEXT.
- Out of scope: single-line TEXT (keeps today's bare box — AutoCAD-faithful); any new rich-text
                wire tag; per-run height or per-run color; paragraph properties (alignment, line
                spacing, lists, indents); columns; fields; stacking; super/subscript; tracking;
                width factor; annotative scaling; background mask; a functional (drag-to-resize)
                ruler. Each disabled control becomes its own follow-up requirement.
- Smallest change: restructure the existing rich-editor overlay's chrome and add the pickers; no
                new file except a small pure-logic header for the testable parts.

## 3. Architectural boundary check  (workflow.md §4)
- [x] **No — proceed.**
  - New abstraction? No. The work extends one existing function, `DrawMtextRichEditorOverlay`;
    helpers are file-local `static` functions in the same TU, not interfaces/templates (§11.4).
  - New dependency? No — ImGui + the existing `FontReg`/`Shx`/`TextStyles` facilities.
  - New global mutable state? No. Panel anchor/ruler-visibility live in `AppCommandState` and
    persist via `UserPrefs`, exactly the `cmdBar*` pattern REQ-040 established
    (`src/io/UserPrefs.cpp:153-162`, `:326-334`). §11.3 satisfied.
  - Data-format change? **No document-format change.** Per-selection font uses the existing
    `[[font:…]]…[[/font]]` run tag (`src/ui/MtextRichFormat.cpp:115,156,174`). Height, oblique,
    justification, and color write existing `CadAnnotation` fields (`plottedHeightInches`,
    `obliqueDeg`+`ovOblique`, `mtextAttach`, entity attrs). UserPrefs gains four scalar keys —
    a user-prefs addition on the REQ-040 precedent, not a document-format change.
  - Ownership/layering? UI reads and writes the annotation it already owns the edit of; IO only
    serializes prefs. Dependencies flow downward.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | How much of the reference toolbar must function in this pass? | 2026-07-30 | Full chrome + ruler; wire only what the existing format supports; lay out the rest disabled with tooltips, each a follow-up requirement. |
| Q2 | Do font/height/color apply to the selection or the whole object? | 2026-07-30 | Font per selection (existing `[[font:…]]` run tag); height and color whole-object (matches what `CadAnnotation` stores). Chosen explicitly to avoid a data-format change. |
| Q3 | Where does the panel sit? | 2026-07-30 | Floating, draggable by its title bar, position persisted across edits and sessions. |
| Q4 | Which text edits show the panel? | 2026-07-30 | Model MTEXT and paper-space MTEXT. Single-line TEXT keeps its bare in-place box. |
| Q5 | The wire format already has a `[[color:…]]` run tag, so per-selection color is free. Enable it too? | 2026-07-30 | Yes — the swatch colours the selected characters via `[[color:…]]`, while the ByLayer combo still sets the whole entity's colour (AutoCAD's own split). Amends Q2: colour is *both* per-selection and whole-object, by two distinct controls. |

## 5. Assumptions
```
ASSUMPTION-1: The panel's control inventory should mirror the reference screenshot's layout and
              order even where a control is disabled.
- Because:       the user's goal is recognizability; a familiar layout with grey buttons reads as
                 "not yet" while a re-ordered subset reads as "different program".
- Risk if wrong: a panel with visible dead controls is judged cluttered.
- Validate by:   user looks at the built panel; if cluttered, drop the disabled controls to a
                 collapsed "more" row (the expand arrow already toggles row 2).

ASSUMPTION-2: Caching the text selection each frame (the existing CallbackAlways mechanism at
              CadUi.cpp:142-148) survives clicking a toolbar combo, so a font pick applies to the
              selection the user made before reaching for the toolbar.
- Because:       ImGui deactivates InputTextMultiline when another widget takes focus, discarding
                 its live selection; today's B/I/U buttons already rely on this cache.
- Risk if wrong: picking a font from a combo applies to a collapsed/stale selection.
- Validate by:   Step-1 spike. RESOLVED by inspection 2026-07-30: the cache is written only *while*
                 the box is active (CallbackAlways) and never cleared on deactivation, so the last
                 selection persists after focus moves — which is exactly why the shipped B/I/U
                 buttons work. A combo popup only holds that state longer. Confirmed for real in
                 manual verification steps 3–4.

ASSUMPTION-4: "grow to accommodate more rows" is satisfied by growing per line of text, leaving the
              wrap-driven growth the user described as a recorded limitation rather than faking it.
- Because:       ImGui's InputTextMultiline has no word wrap (a long-standing upstream gap). Sizing the
                 box to the *wrapped* height would grow the box at the right moment but the text inside
                 would still run off to the right — a box that grows while its text does not wrap.
- Risk if wrong: the editor does not match AutoCAD's wrap behavior, which is what the user asked for.
- Validate by:   raised to the user with options (accept; hard-wrap on commit; custom wrapping editor).
                 A custom editor is an architectural decision → SPEC GAP, not a Workshop choice.

ASSUMPTION-3: The ruler's tick spacing should read in the MTEXT's own plotted inches.
- Because:       AutoCAD's MTEXT ruler is in drawing/paper units; the reference screenshot's ticks
                 are unlabeled so the unit is not determinable from it.
- Risk if wrong: cosmetic only this pass (the ruler is non-functional), but a functional ruler
                 follow-up would inherit the wrong unit.
- Validate by:   user comparison against AutoCAD; cheap to change while the ruler is decorative.
```

## 6. Plan
- **Approach:** keep `DrawMtextRichEditorOverlay`'s existing target resolution (model annotation /
  paper text / placement box) and its plain-TEXT early return **untouched**. Replace only the rich
  branch's chrome: an ImGui child window titled "Text Formatting" with a drag-handle title bar
  positioned from a persisted anchor (defaulting near the MTEXT box), holding two toolbar rows;
  below it, the in-place edit box with a decorative ruler above and a resize-handle glyph. Pure
  geometry/string decisions move to a new header-only `src/ui/MtextToolbar.hpp` so they are unit
  testable (the `PlotFont.hpp` / `OrthoConstrain` / `ColorContrast` precedent).

- **Control inventory** (reference order; ✅ = wired, ⬜ = present but disabled + tooltip):

  Row 1 — ✅ text style combo (`cmd.textStyles`, applying = `TextStyles` bake per ADR-020) ·
  ✅ font combo (per selection via `[[font:…]]`, listing `kTextStyleFonts` with `DrawTextStyleSample`
  previews) · ⬜ annotative · ✅ height (whole object, `plottedHeightInches`) · ✅ B · ✅ I ·
  ⬜ strikethrough · ✅ U · ⬜ overline · ⬜ background mask · ⬜ undo · ⬜ redo (tooltip: Ctrl+Z/Ctrl+Y
  in the box) · ⬜ stack · ✅ color combo (whole object, entity attrs, ByLayer) · ✅ ruler toggle ·
  ✅ OK · ✅ expand arrow (collapses row 2).

  Row 2 — ⬜ columns · ✅ MTEXT justification (9-way, existing `mtextAttach`, already honored by the
  renderer) · ⬜ paragraph · ⬜ align left/center/right/justify/distributed · ⬜ line spacing ·
  ⬜ bullets/numbering · ⬜ insert field · ✅ uppercase (existing `[[caps]]` wrap) · ⬜ lowercase ·
  ⬜ superscript · ⬜ subscript · ✅ symbol dropdown (the existing math-symbol `Insert…` list) ·
  ✅ oblique (whole object, `obliqueDeg` + `ovOblique`) · ⬜ tracking · ⬜ width factor.

  Also kept: the existing "Abc" type-in-all-caps toggle, and Cancel alongside OK.

- **Files/functions to touch:**
  - `src/ui/MtextToolbar.hpp` (new, header-only, ImGui-free): `ClampPanelAnchor` (keep the panel
    fully inside the viewport rect), `FontRunTags` (compose `[[font:X]]` / `[[/font]]`),
    `RulerTickPositions` (tick offsets for a given width + unit scale), `AttachLabel` (1–9 →
    "Top Left"…"Bottom Right").
  - `src/ui/CadUi.cpp`: rewrite the rich branch of `DrawMtextRichEditorOverlay` (lines ~6277–6388);
    add the two toolbar rows and ruler as file-local `static` draw helpers.
  - `src/commands/CadCommands.hpp`: four `AppCommandState` fields — `mtextPanelAnchorValid`,
    `mtextPanelAnchorX/Y`, `mtextPanelRulerVisible`, `mtextPanelRow2Visible`.
  - `src/io/UserPrefs.cpp`: load + save those, mirroring the `cmdBar*` block.
  - `CMakeLists.txt`: add `tests/MtextToolbarTests.cpp` to `GoSurveyTests`.
  - `spec/requirements.md` (REQ-051 + traceability row), `spec/project.md` (decision-log row).

- **Test approach.** The panel itself is ImGui UI, which this project verifies manually (the
  REQ-039/040 precedent; `MtextRichFormat.cpp` cannot link into the pure test target). The pure
  logic extracted to `MtextToolbar.hpp` is unit tested:
  - happy path — `ClampPanelAnchor` leaves an in-bounds anchor untouched; `FontRunTags("Arial")`
    yields `[[font:Arial]]` / `[[/font]]`; `RulerTickPositions` spaces ticks at the expected
    interval and count for a known width; `AttachLabel(1)=="Top Left"`, `(5)=="Middle Center"`.
  - failure mode — `ClampPanelAnchor` pulls an off-screen anchor (negative, and past the right/
    bottom edge) fully back inside, and degrades to the viewport origin when the panel is larger
    than the viewport; `FontRunTags("")` yields empty tags (no-op, never a malformed `[[font:]]`);
    `RulerTickPositions` on a zero/negative width yields no ticks (no division by zero);
    `AttachLabel(0)` and `(10)` return a safe fallback rather than reading out of bounds.
  - regression, manual: single-line TEXT still opens the bare box; MTEXT placement still commits;
    a `.gs` save/reload and a DXF export of edited MTEXT are unchanged.

- **Steps:**
  - [ ] 1. Spike ASSUMPTION-2 — confirm the cached selection survives a combo interaction.
  - [ ] 2. `src/ui/MtextToolbar.hpp` + `tests/MtextToolbarTests.cpp` + CMake; run green.
  - [ ] 3. `AppCommandState` fields + `UserPrefs` load/save (REQ-040 pattern).
  - [ ] 4. Panel chrome: title bar, drag, clamp, persisted anchor, two rows, expand arrow.
  - [ ] 5. Wire the ✅ controls; add the ⬜ controls disabled with tooltips.
  - [ ] 6. Ruler + resize-handle affordance + ruler toggle.
  - [ ] 7. nanoCAD gray/steel-blue styling pass.
  - [ ] 8. Self-verify (§9), then user manual verification against the reference screenshot.

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1–Q4; Q5 open, has a safe default). Tests-first for the pure
  header (step 2 before step 4). The riskiest unknown (selection survival) is spiked first, so a
  FAIL there surfaces before the chrome is built.

## 8. Implementation log
- 2026-07-30 — opened; Authority + boundary check + plan written. No code yet.
- 2026-07-30 — Q5 answered (per-selection colour in scope). REQ-051 + traceability row added to
  `spec/requirements.md`; decision recorded in `spec/project.md`. Authority now complete.
- 2026-07-30 — `src/ui/MtextToolbar.hpp` + `tests/MtextToolbarTests.cpp` (8 cases) + CMake entry.
  74/74 ctest green.
- 2026-07-30 — `AppCommandState` gains six panel-chrome fields; `UserPrefs` load/save mirrors the
  `cmdBar*` block. `CloseMtextRichEditorUi` deliberately left untouched so the panel position survives
  closing the editor — that is the difference between panel chrome and per-edit state.
- 2026-07-30 — `MtextRichEditorTargetAttrs` added beside `MtextRichEditorTargetAnnotation`
  (`CadCommands.hpp`), so the colour control reaches the model/paper attribute row by the same rule.
- 2026-07-30 — `kTextStyleFonts` moved above the editor (pure move; the STYLE dialog and the panel's
  font picker both use it now). `DrawTextStyleSample` forward-declared for the font-preview rows.
- 2026-07-30 — rich branch of `DrawMtextRichEditorOverlay` rewritten: in-place box + ruler over the
  MTEXT, floating draggable "Text Formatting" panel with two rows. Plain-TEXT branch and target
  resolution untouched. Build clean; 74/74 green; app launches.
- 2026-07-30 — **deviation from plan**: the planned resize-handle affordance was dropped and REQ-051
  amended. Q1/Q4 put drag-to-resize out of scope, so the handle would have invited a drag the editor
  cannot perform. Recorded as a follow-up instead of shipping a dead affordance.
- 2026-07-30 — ASSUMPTION-2 (step-1 spike) resolved by inspection rather than a live run, see §5.
- 2026-07-30 — **first user review; three findings, REQ-051 amended by recorded decision, then fixed:**
  - F1 the panel clipped its second row and its right-hand controls → the fixed 846px estimate is gone;
    the child now uses `AutoResizeX|AutoResizeY|AlwaysAutoResize` and reports its measured size back into
    two session-only fields, which place it and span its caption on the next frame. Spanning the caption
    from the *measured* width (not a guess) is what keeps caption and content from fighting over the width.
  - F2 the ruler was inert → its right marker is now a drag handle writing `boxMaxX` (or the rubber-band
    box while placing), with one `PushUndoSnapshot("MTEXT width")` per drag, guarded by
    `mtextRulerDragActive`. This un-defers part of what Q1 put out of scope, at the user's request.
  - F3 the box opened at the MTEXT's drawn box height → it is now one line tall and grows a line per line
    of text. Its width still tracks the MTEXT column, so F2's drag visibly re-columns it.
  - Build clean, 74/74 green, app launches. **Open limitation** raised to the user: ImGui's
    `InputTextMultiline` has no word wrap, so the box grows on explicit line breaks, not when text reaches
    the column width — see ASSUMPTION-4.

## 9. Self-verification
- [x] build-project        — PASS (clean; the only warnings are pre-existing `strncpy` ones in
                             `PdfAttachDialog.cpp`, untouched by this task. App launches and stays up.)
- [x] architecture-review  — PASS (decision recorded before code; UI-owned change + IO prefs only; no new
                             abstraction — helpers are file-local statics in the same TU; no new
                             dependency; no new global — panel state lives in `AppCommandState`+UserPrefs
                             per REQ-040; no wire-format or stored-field change; dependencies flow down)
- [x] code-review          — PASS (plain-TEXT and placement paths preserved; every whole-object control
                             null-guards its target and disables itself while placing; `MtextRichEditorTargetAttrs`
                             guards a parallel-vector length mismatch; height/oblique clamped before store;
                             `ovHeight`/`ovOblique`/`ovFont` set so a later style edit cannot silently
                             undo a user's explicit choice)
- [x] dependency-audit     — n-a (no dependency added; ImGui + existing FontReg/Shx/TextStyles)
- [x] performance-review   — n-a (per-frame UI, not a measured hot path; `RulerTicks` is capped at 4096
                             marks and allocates one small vector per frame only while the editor is open)
- [x] testing              — PASS (74/74 ctest green, +8 new `MtextToolbarTests` covering happy path and
                             the failure modes: off-screen/oversized anchor, empty font family, degenerate
                             ruler, out-of-range attachment). Panel interaction is ImGui UI → **manual**,
                             per the REQ-039/040 convention.

## 10. Verification result
- Submitted:  2026-07-30
- Verdict:    PASS — user verified against the reference screenshot in app 2026-08-18.
- Findings:   one self-raised and resolved — a decorative resize handle would misrepresent the editor;
              dropped and REQ-051 amended (see §8).

## 11. Outcome
- Requirements satisfied: REQ-051 (Acceptance met: pending the manual pass below).
- Tests added:            `tests/MtextToolbarTests.cpp` (8 cases).
- Docs updated:           `spec/requirements.md` (REQ-051 + traceability), `spec/project.md` (decision log).
- Technical debt:         the disabled controls (paragraph properties, columns, fields, stacking,
                          super/subscript, tracking, width factor, annotative, background mask,
                          strikethrough, overline, in-panel undo/redo), the ruler being visual only, and
                          the absent drag-to-resize — all recorded on REQ-051 as follow-ups.
                          Removal condition: a requirement per control, several needing a wire-format or
                          stored-field decision first (i.e. a spec decision, not a Workshop choice).
- Done:                   2026-08-18 (user verified in app)

### Manual verification (user)
1. Double-click a model MTEXT → a "Text Formatting" panel with two rows appears, ruler above the box.
2. Drag the panel by its blue title bar; close and reopen the editor → it returns to that spot. Restart
   the app and reopen → still there.
3. Select a few characters, pick a font → only those change. With nothing selected, pick a font → the
   whole MTEXT changes.
4. Select characters, click the colour swatch, pick a colour → only those characters recolour. With
   nothing selected the swatch is greyed.
5. Height and the oblique field change the whole MTEXT; the ByLayer combo changes the object's colour.
6. The style dropdown lists the drawing's styles; applying one re-bakes the text (REQ-044).
7. Justification dropdown re-lays the text out in its box.
8. Every greyed button shows a tooltip naming it and does nothing. Ruler button and the expand arrow toggle.
9. Paper-space MTEXT gets the same panel; double-click a single-line TEXT → still the bare box.
10. OK commits, Esc cancels; save/reload `.gs` and export DXF → text unchanged.
