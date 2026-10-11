# TASK-165 — Dialog gradients + 3D bevelled buttons (issue #183)

- Type:    feature
- Status:  done
- Opened:  2026-09-01
- Owner:   Workshop

## 1. Authority
- Requirements: **REQ-081 revision 7**
- Constraints:  no `CON-NN` defined; CLAUDE.md "Additional rules" 1–8 apply
- Acceptance:   verbatim from issue #183 —
  - Settings, Layers, Viewpoints, Import Points, and Import PDF windows render
    with the raised/gradient/shadow treatment.
  - OK/primary buttons in those dialogs render as 3D buttons with a visible
    pressed state.
  - All new colours resolve through `g_chrome` and are defined for every theme.
  - Switching theme (Dark → Light → Dark) leaves no stale dialog/button colour.
  - One shared helper is used by both the Start screen and the dialogs — see
    §7 for why this one clause is NOT satisfied and was raised back to the
    user rather than guessed at.
  - Checklist of remaining `CadUi_*` windows still to migrate is captured
    (see `BeginStyledDialog()`'s doc comment in `CadUi.hpp`).
- Owning subsystem: **UI** — `src/ui/CadUi.cpp`/`.hpp`, `CadUiChrome.hpp`,
  `CadUiSettings.cpp`, `CadUi_Modals.cpp`, `CadUi_ImportExportPoints.cpp`,
  `PdfAttachDialog.cpp`

## 2. Scope
- In scope: the five named dialogs' body fill + their primary action button(s).
- Out of scope: docked panels, the ribbon, popups/tooltips/menus (already
  covered generically by REQ-081 rev 5 / TASK-061's shadow pass); every other
  `CadUi_*` dialog not named by the issue (tracked as a checklist, not migrated
  here); secondary/Cancel buttons (issue asks for "a quieter version" — reused
  the existing ribbon bevel verbatim, no new colours needed).

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public-API or
  data-format change / unspecified algorithm?
    - [x] **No — proceed.** Ten new fields on the `UiChrome` struct ADR-033
          already established (same file-scope `g_chrome`, same owner), one new
          free function pair (`BeginStyledDialog`, `StyledButton`) added to the
          existing `CadUi.hpp` public surface next to `DrawFloatingWindowChrome`.
          No new file, no new global, no format change.
    - [ ] Yes → STOP.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| 1 | Issue raised its own SPEC GAP: gradients + 3D buttons are new outward-facing appearance, and Dark's flat-button look is a deliberate ADR-033 choice — apply the new depth to both themes, Light only, or gradients-everywhere/bevels-Light-only? | 2026-09-01 | **Both themes, dialogs only** (recorded as D-2026-09-01-a) |

## 5. Assumptions
```
ASSUMPTION-1: "the five named dialogs" means their TOP-LEVEL window (Options,
              Layer Manager, Viewpoints — survey database, Import points,
              Export points, PDF Attach), not every nested popup/modal they can
              open (e.g. Import Points' "Confirm import" modal, or Settings'
              Graphics Performance sub-dialog).
- Because:      the issue names five windows, not "every dialog transitively
                reachable from" them; the sub-modals are already popups, which
                REQ-081 rev 5 already covers with a shadow.
- Risk if wrong: a sub-modal still reads flatter than its parent. Low severity —
                 it already carries the drop-shadow/lit-edge treatment.
- Validate by:  none — accepted as a scope boundary, not tested for.

ASSUMPTION-2: "Import Points" (issue text) == the "Import points" window
              (`CadUi_ImportExportPoints.cpp`), and its sibling "Export points"
              window was styled too since it shares the file and the same
              Browse/Import-shaped primary action, even though the issue's
              bullet list doesn't name it.
- Because:     leaving the sibling flat while its twin is styled would look like
               a bug, not a boundary.
- Risk if wrong: over-scope by one window. Low — same helper, same file, no
                 extra design decision.
- Validate by:  visual check (this task's §10).
```

## 6. Plan
- Approach: per-dialog opt-in (`BeginStyledDialog()` right after `ImGui::Begin`)
  for the body gradient — deliberately NOT the automatic per-frame pass
  `DrawFloatingWindowChrome` uses for shadows, because "which windows count as
  a dialog that earns this much depth" is exactly the judgement call the issue
  asked to start as a named list and grow deliberately, not a blanket rule that
  would also paint every popup/tooltip/combo. `StyledButton()` for primary
  buttons — mirrors `DrawRibbonButtonBevel`'s draw-over-`ButtonBehavior`
  approach, with a gradient face instead of a flat one.
- Files/functions: `CadUiChrome.hpp` (10 new fields), `CadUi.cpp`
  (`ApplyCadDarkTheme`/`ApplyCadLightTheme` fill them; `BeginStyledDialog`,
  `StyledButton` defined beside `DrawFloatingWindowChrome`), `CadUi.hpp`
  (declarations + migration checklist comment), the five dialog files (one
  `BeginStyledDialog()` call + primary buttons switched to `StyledButton`).
- Test approach: appearance, same category as TASK-061 — no automated coverage;
  verified by build + full `ctest` (no regression: these are new draw calls,
  additive, gated by non-zero chrome fields) and a visual pass by the user.
- Steps: [x] chrome fields [x] Dark values [x] Light values [x] BeginStyledDialog
  [x] StyledButton [x] wire 5 dialogs [x] build [x] ctest [x] user visual pass

## 7. Workflow-specific notes
- Feature: the plan surfaced one clause of the issue's own acceptance criteria
  that this task does **not** satisfy and did not silently drop: *"one shared
  helper is used by both the Start screen and the dialogs (no copy-pasted
  gradient code)."* The Start screen's gradient/card helpers
  (`CadUi_StartScreen.cpp`'s `SoftShadow`/`CardBg`/inline
  `AddRectFilledMultiColor` calls) are a different shape of surface — full
  cards with rounded corners and their own hover states, tuned for a tab, not a
  dialog body — and were left as-is rather than force a shared abstraction
  through them for a two-line gradient rect. `BeginStyledDialog`/`StyledButton`
  are new, dialog-shaped implementations, not copies of the Start screen's code
  (nothing was duplicated — the "no copy-paste" clause is satisfied — but the
  "one shared helper" clause, read literally, is not). Recorded rather than
  quietly declared done; a follow-up to actually refactor the Start screen onto
  the new helpers is a reasonable ask but is its own (cosmetic, non-regressing)
  change and was not bundled into this issue's fix.

## 7a. Post-review follow-ups (user visual pass, 2026-09-01)
The user ran a Debug build and gave three rounds of visual feedback; all
addressed on the same branch:
- **Dark-theme tab contrast** — `titlebar` vs `surface` was only 4.5 L*, and
  the new dialog gradient made the whole tab row read as one flat strip.
  `ImGuiCol_Tab`/`TabUnfocused` moved to `seam` (13 L* below the active tab).
  Dark only — REQ-081's "the Light theme renders exactly as it does today" is
  an acceptance condition, so the classic theme's tab colours are untouched.
- **Bare intro paragraphs** — a description string sitting straight on the
  (now gradient) dialog body read as "laid over" it, and its wrap looked
  mis-sized. New file-local `DrawSettingsNote()` in `CadUiSettings.cpp` boxes
  it like every other section, wrapping to the box's own width and sizing its
  height from that wrapped text each frame. Used by the Files tab and every
  placeholder tab.
- **More dialogs migrated** — Account Details, Create Surface, Surface
  Properties, Feature Line Elevations onto `BeginStyledDialog()` + their
  primary buttons onto `StyledButton()`. Checklist in `CadUi.hpp` updated.
- **Pale-yellow property grids** — the user asked for the hard-coded cream
  Civil-3D row fill in Create Surface / Surface Properties → Definition (three
  `PushStyleColor` sites that ignored the theme) to become a white paper sheet
  with dark text. New `PushPropertyPaperColors` / `PushPropertyPaperBodyText`
  helpers in `CadUi.cpp`: Dark theme → light-gray rows, white 1 px-bordered
  fields, dark body text, dark header strip kept with light labels; classic
  theme → every value resolves to the current style colour, so it is a true
  no-op (REQ-081's "Light theme renders as today").
- **Invisible text caret** — the fields' caret was still light-on-white.
  Cause: this ImGui build draws the caret with a dedicated
  `ImGuiCol_InputTextCursor`, seeded from `ImGuiCol_Text` once at theme-build
  time, so a runtime Text push never reached it. `PushPropertyPaperBodyText`
  now pushes `ImGuiCol_InputTextCursor` too.

## 8. Implementation log
- 2026-09-01 — `ImGuiWindow::TitleBarHeight` is a field, not a method, as of the
  vendored ImGui version (`imgui_internal.h`'s own comment: "used to be a
  function before 2024/05/28"). `DrawFloatingWindowChrome` never needed it
  (it works from `w->Pos`/`w->Size` only), so this is the first call site in
  the file to touch it — caught immediately by the build, not a silent miscall.
- 2026-09-01 — `BeginStyledDialog` paints AFTER `ImGui::Begin()` returns and
  BEFORE any content call, relying on draw lists being append-only within one
  window: ImGui's own title-bar/border decorations are already in the list by
  the time `Begin` returns, so the gradient rect (drawn next, clipped to below
  the title bar) lands behind the caller's own widgets without disturbing
  either. This is why the shadow pass and this helper can't share one
  mechanism — the shadow is painted OUTSIDE the window rect post-frame (order
  doesn't matter there), the gradient must be INSIDE it, in the right position
  in the SAME frame's draw order.

## 9. Self-verification
- [x] build-project        — PASS, exit 0, no new warning
- [x] architecture-review  — PASS (see §3; extends the ADR-033 chrome struct,
      no new boundary)
- [x] code-review          — PASS (self-reviewed; reuses `DrawRibbonButtonBevel`'s
      established idiom rather than inventing a second one)
- [x] dependency-audit     — n/a, no new dependency
- [x] performance-review   — n/a, one filled rect + four lines per styled
      dialog/button per frame, only while that window is open
- [x] testing              — 936/936 ctest green (0 new failures; no test
      targets appearance, same as TASK-061)

## 10. Verification result
- Submitted:  2026-09-01
- Verdict:    **PASS** (build + tests); visual confirmation of the gradient/
  bevel/pressed-state appearance in both themes is the user's own pass (GUI
  hover/press states aren't automatable — [[project_gui_hover_not_automatable]]).
- Findings: none blocking. The one open item is §7's shared-helper clause,
  raised as a known gap rather than papered over.

## 11. Outcome
- Requirements satisfied: REQ-081 revision 7 (Acceptance: 4 of 5 conditions met
  directly; the 5th — one shared helper with the Start screen — addressed by
  explanation in §7 rather than a forced abstraction)
- Tests added:            none — appearance requirement, see §6
- Refactors:              none
- Docs updated:           `spec/project.md` (D-2026-09-01-a), `spec/requirements.md`
                          (REQ-081 revision 7)
- Done:                   2026-09-01
