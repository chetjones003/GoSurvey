# TASK-058 — Restyle the Dark theme to the Hazel reference and make the hand-painted chrome theme-aware

- Type:    feature
- Status:  done
- Opened:  2026-08-16
- Owner:   Workshop

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         n/a — `spec/project.md` §1–4 are still template placeholders; no `GOAL-NN` exists
- Requirements: **REQ-081** (accepted 2026-08-16), REQ-301 (minimal abstraction)
- Constraints:  no `CON-NN` are defined in the spec; CLAUDE.md "Additional rules" 1–8 apply
- Acceptance:   restated verbatim from REQ-081 —
  - with Dark active, no chrome element renders in the classic theme's palette
    (the `#464646` / `#3A3A3A` grays or the steel-blue `#3C5575` family);
  - two adjacent docked panels are separated by a visible border line, and the
    panel surface differs from the dockspace ground by a visible value step;
  - switching Options → Display → *Color theme* Dark → Light → Dark leaves each
    theme rendering its own palette, with no colour left over from the other on
    the frame after the switch;
  - the **Light** (nanoCAD classic) theme renders exactly as it does today —
    this work does not change it;
  - viewport contents — crosshair, grips, entity/layer colours, selection
    highlight, snap markers, paper-space sheet — are unchanged;
  - a Properties geometry row whose label ends in X, Y or Z shows the axis badge
    in red, green or blue respectively; a non-axis row (e.g. Radius) shows none.
- Owning subsystem: **UI** — `src/ui/CadUi.cpp` only

## 2. Scope
- In scope:
  - rewrite `ApplyCadDarkTheme`'s ~45 `ImGuiCol_*` values and style metrics to the
    Hazel palette, and add the table/tab colours it never set;
  - add the `UiChrome` file-local palette (ADR-033) and have **both** theme
    functions fill it;
  - route the hand-painted chrome through it: the toolbar/ribbon band constants,
    `DrawRibbonButtonBevel`, the status-bar strip, the autocomplete popup, and the
    property grid (`PropSectionHeader`, `PropValueCellBg`, `FillPropPanelEmpty`,
    `kPropTableFlags` borders);
  - add the X/Y/Z axis badge to `PropGeomRow`.
- Out of scope:
  - the classic (Light) theme's appearance — it must not change;
  - the drawing viewport: crosshair, grips, entity colours, snap markers, the
    paper-space sheet, `viewportBg*` (a user setting, not a theme value);
  - dialogs and panels outside `CadUi.cpp` that already use `ImGuiCol_*` only
    (`CadUiSettings.cpp`, `CadUi_*.cpp`) — they inherit the new palette for free;
  - `SplashScreen.cpp`, `ViewCube.cpp`, `RichTextEdit.cpp` — their `IM_COL32`
    values are content, not shell chrome;
  - adding a third theme, or persisting per-theme user overrides.
- Smallest change: one struct + one file-local instance in the translation unit
  that already owns every affected draw site. No new file, no header change, no
  public API, no persisted field.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change /
  global state / public-API or data-format change / algorithm the spec didn't
  specify?
    - [ ] No — proceed.
    - [x] **Yes — file-scope mutable state (`UiChrome`).** Escalated as a SPEC GAP
          before any code; resolved by the 2026-08-16 decision-log entry
          (REQ-081 + ADR-033). Also escalated: no requirement governed the themes
          at all. Proceeding under that recorded decision.
- REQ-301 check: the abstraction has **two** present-day concrete uses — the Dark
  theme and the classic theme both fill and are read through it.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Which theme carries the Hazel look — replace the classic one, restyle Dark, or add a third? | 2026-08-16 | **Restyle the Dark theme**; leave the classic nanoCAD theme intact |
| Q2 | How far past colours — palette only, palette + panel chrome, or full parity incl. property-grid widgets? | 2026-08-16 | **Full parity including the property-grid widgets** (X/Y/Z badges, grid row style) |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: The axis of a Properties geometry row can be read from its label's
              trailing token ("Start X" → X, "Center Z" → Z, "Radius" → none).
- Because:       REQ-081 names the badge but not how a row declares its axis, and
                 the 12 call sites all label their axis rows this way.
- Risk if wrong: a future row labelled e.g. "Max X extent" would get no badge, or
                 a non-coordinate row ending in a bare X would get a wrong one.
                 Cosmetic either way — no value or edit path is affected.
- Validate by:   the acceptance check "Radius shows no badge"; revisit if a row
                 ever needs a badge its label cannot express.
```
```
ASSUMPTION-2: Hazel's editor palette is reproduced from the supplied screenshots
              and the known Hazelnut theme constants, not from its source.
- Because:       the reference is four screenshots; exact source values were not
                 supplied.
- Risk if wrong: colours are near-matches rather than exact. REQ-081's acceptance
                 is stated as relationships (surface lighter than ground, border
                 darker than both), so a near-match still satisfies it.
- Validate by:   user's side-by-side look at the running app.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: introduce `UiChrome` in `CadUi.cpp`'s existing file scope, next to the
  band constants it replaces. Each `Apply*Theme` ends by filling it. Every site
  that today reads a `constexpr ImU32` or an inline literal reads the struct
  instead. The classic branch is filled with the **exact literals being replaced**,
  so its rendering is unchanged by construction rather than by inspection.
- Files/functions to touch (all in `src/ui/CadUi.cpp`):
  - `UiChrome` + `g_chrome` (new, file scope)
  - `ApplyCadDarkTheme` — full palette rewrite + fills `g_chrome`
  - `ApplyCadLightTheme` — fills `g_chrome` with today's constants; colours untouched
  - `kBandFace`/`kBandHilite`/`kBandShadow`/`kBandSunken` → `g_chrome` fields
  - `RibbonSectionBegin`, `RibbonSectionEnd`, `DrawRibbonButtonBevel`
  - `PropValueCellBg`, `FillPropPanelEmpty`, `PropSectionHeader`, `PropGeomRow`
  - `DrawCadStatusBarStrip` (window + child bg)
  - the command autocomplete popup (`WindowBg` / `Border`)
- Test approach: no automated test — this is appearance, and `requirements.md`'s
  anti-requirements rule out screenshot-diff/UI-automation testing. Verified
  manually against REQ-081's acceptance list, which is written to be checkable by
  eye. Happy path = Dark renders the Hazel palette with visible panel separation;
  failure mode = **the theme-switch round trip** (Dark → Light → Dark), which is
  the one way this change can break: a draw site that reads a stale `g_chrome`.
- Steps:
  - [x] step 1 — add `UiChrome` + `g_chrome`, fill from both themes
  - [x] step 2 — rewrite the Dark palette + style metrics
  - [x] step 3 — route band / ribbon / status bar / popup through `g_chrome`
  - [x] step 4 — restyle the property grid; add the axis badge
  - [x] step 5 — build; walk the acceptance list

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1, Q2 above, both binding user decisions).
  Tests-first does not apply — the deliverable is appearance, and the project's
  anti-requirements exclude rendered-GUI automation. The classic theme's
  no-change condition is met structurally (its branch is filled with the literals
  it replaces), which is stronger than a test would be.

## 8. Implementation log  (append as you work)
- 2026-08-16 — opened. Boundary check answered **Yes** (file-scope state) →
  SPEC GAP filed and resolved the same day as REQ-081 + ADR-033 before any code.
- 2026-08-16 — found while surveying: `ApplyCadDarkTheme` never set
  `ImGuiCol_Table*`, so the Properties grid drew its borders in ImGui's stock
  colours under the Dark theme. Added, within the requirement.
- 2026-08-16 — steps 1–5 implemented.
- 2026-08-16 — REQ-081's badge condition collided with its "Light theme
  unchanged" condition, since `PropGeomRow` is shared by both themes. Resolved
  inside the requirement rather than by amending it: `UiChrome::axisBadges` gates
  the badge, the classic theme sets it false. Defensible on its own terms — that
  theme is a reproduction of nanoCAD 5, which has no axis badges.
- 2026-08-16 — after the first look, raised `ImGuiCol_TableBorderLight` from
  `#1E1E1E` to `#2E2E2E`: at `#1E1E1E` the property grid's row rules were
  invisible against the `#242424` surface, which reads as flat — the exact
  complaint REQ-081 exists to answer.
- 2026-08-16 — verified in the running app (see §10). Note for whoever picks this
  up: the user's saved `displayColorThemeIdx` is **1** (classic), so none of this
  is visible until the theme is switched to Dark in Options → Display.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS
- [x] architecture-review  — PASS with escalation: the one architectural item
      (file-scope state) was escalated and is covered by ADR-033. Change stays
      inside the UI subsystem; no layer, dependency, ownership or format change.
- [x] code-review          — PASS: no literal chrome colour survives at a draw
      site; both theme entry points write every field.
- [x] dependency-audit     — n/a (no dependency added)
- [x] performance-review   — n/a: per-frame cost is unchanged (struct field reads
      replace `constexpr` reads); no allocation, no new draw call.
- [x] testing              — manual, per §6 (results in §10)

Build: `cmake --build build`, MSVC 14.50 / Ninja Release, exit 0 across all three
targets. `CadUi.cpp` recompiled from scratch and its warning list compared: 76
warnings, all at lines ≥ 3282 and none inside a touched range — no new warning.

## 10. Verification result
- Submitted:  2026-08-16
- Verdict:    **PASS** on every condition checkable without the user; one
  condition (the classic theme's pixel-identity) argued rather than diffed.
- Method:     the app was launched under both themes and screenshotted; a LINE
  was drawn and picked so the Properties geometry rows were populated.
- Per acceptance condition:
  - no classic-palette chrome under Dark — **PASS** (observed: neutral
    `#1A1A1A`/`#242424` throughout; no `#464646`, no steel-blue anywhere);
  - adjacent panels separated, surface ≠ ground — **PASS** (observed: a dark
    seam between the Properties and Viewports docks);
  - Dark → Light → Dark leaves no colour behind — **PASS by construction**:
    both entry points write every `UiChrome` field, and the two themes were
    observed rendering their own palettes in the same session;
  - Light unchanged — **PASS, by construction rather than by pixel diff.** Every
    classic value is the literal it replaced, checked one at a time including
    the float→`ImU32` rounding (`0.117 → 30`, `0.275 → 70`, `0.45 → 115`), and
    the only shared code paths that changed are gated (`axisBadges`,
    `headerBoxGlyph`) or colour-neutral (the extra `ImGuiCol_Text` push in
    `PushModeToggleButtonColors`, which pushes that theme's own text colour so
    both branches pop the same count). A before/after pixel diff was **not**
    performed — it would need a second build of the parent commit;
  - viewport contents unchanged — **PASS** (no viewport draw site was touched;
    the drawn line, crosshair and ViewCube render as before);
  - X/Y/Z badges present, non-axis rows bare — **PASS** (observed on a selected
    line: Start/End X red, Y green, Z blue; `Length` and `Radius` bare).
- Findings:   none outstanding.

## 11. Outcome
- Requirements satisfied: REQ-081 (Acceptance met: yes, subject to the user's own
  side-by-side judgement of the palette — ASSUMPTION-2)
- Tests added:            none — appearance requirement, see §6
- Refactors:              chrome colours centralised in `UiChrome`
- Docs updated:           `spec/requirements.md` (REQ-081 + matrix), `spec/project.md` (ADR-033)
- Done:                   2026-08-16
