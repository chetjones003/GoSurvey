# TASK-061 — Floating windows lift off the shell (drop shadow, lit edge, live title bar)

- Type:    feature
- Status:  done
- Opened:  2026-08-16
- Owner:   Workshop

## 1. Authority
- Requirements: **REQ-081 revision 5**
- Constraints:  no `CON-NN` defined; CLAUDE.md "Additional rules" 1–8 apply
- Acceptance:   verbatim — *a floating window — dialog, modal or popup — reads as
  lifted off the shell rather than pasted onto it. It carries a soft drop shadow
  on all sides, a lit top edge, and a title bar that is visibly live when the
  window holds focus. This applies to every floating window without each one
  opting in … a theme opts out by setting no window shadow.*
- Owning subsystem: **UI** — `src/ui/CadUi.cpp` (+ one call in `src/app/main.cpp`)

## 2. Scope
- In scope: every floating top-level window and popup — the dialogs the user
  named (settings, import points, attach PDF, edit points, traverse editor,
  save-before-close) and every menu, combo and tooltip, reached generically.
  Plus `WindowRounding`/`PopupRounding` 3 → 5, and `TitleBgActive`.
- Out of scope: the classic theme (opts out); docked panels (they already state
  elevation via TASK-060's `CastShadowInto`); the content *inside* any dialog —
  no dialog's layout, padding or controls were touched.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public-API or
  data-format change / unspecified algorithm?
    - [x] **No — proceed.** One `UiChrome` field on the struct ADR-033 already
          established, one new draw helper, and one new public function in the
          existing `CadUi.hpp` surface. No new file, owner, global or format.
    - [ ] Yes → STOP.
- Worth flagging even though it is not a boundary crossing: the new pass reads
  `GImGui->Windows` (ImGui internals). `CadUi.cpp` already depends on
  `imgui_internal.h` throughout — `ImGuiWindow`, `ImRect`, `ItemAdd`,
  `ButtonBehavior` — so this adds no new coupling, only more of an existing one.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| — | none — the report was specific and reproducible | — | — |

## 5. Assumptions
```
ASSUMPTION-1: "Floating window" == a top-level, undocked window that either has
              a title bar or is a popup/tooltip.
- Because:       the user listed dialogs by name; the rule has to generalise to
                 dialogs not yet written.
- Risk if wrong: a titled window that is really app furniture would get a halo
                 tracing a rect the user cannot otherwise see. The three pieces
                 of furniture in this app (dockspace host, status-bar strip,
                 floating command bar) are all NoTitleBar, so all three are
                 excluded by the same test.
- Validate by:   observed — no stray halo anywhere in the shipped layout.
```

## 6. Plan
- Approach: a single per-frame pass over the submitted window list, rather than
  a `BeginDialog`/`EndDialog` helper each dialog must call. The pass is the whole
  point: a per-dialog opt-in would have to be repeated in every dialog written
  afterwards, and the first one to forget looks exactly like this bug did.
- Files/functions: `CadUi.cpp` — `UiChrome::windowShadow`, both theme functions,
  `DrawWindowDropShadow`, `DrawFloatingWindowChrome`, `ApplyCadDarkTheme` style
  metrics; `CadUi.hpp` — one declaration; `main.cpp` — one call before `Render()`.
- Test approach: appearance, so verified by sampling the running app's
  framebuffer across the dialog edge — a falloff either is in the pixels or is
  not. Failure mode = the halo drawn at the wrong depth (over a window that
  should be in front, or hidden behind the panel it should sit on); checked by
  opening a dialog *over* a docked panel and confirming the halo lands on the
  panel and the dialog itself is unobscured.
- Steps: [x] chrome field [x] shadow helper [x] per-frame pass [x] wire into main
  [x] TitleBgActive [x] rounding [x] build [x] verify by sampling

## 7. Workflow-specific notes
- Feature: pre-flight — no question needed. The one design decision (generic pass
  vs per-dialog helper) is recorded in §6 and in the code comment, because the
  reason for it is the requirement's "without each one opting in" clause.

## 8. Implementation log
- 2026-08-16 — the non-obvious part is **which draw list the halo goes into**.
  It is appended to each window's *own* list: ImGui emits lists in window order,
  so the halo lands over whatever is behind that window and under any window in
  front of it. A shared background or foreground list would have put every
  shadow at one depth and got both of those wrong — the shadow of a dialog would
  either vanish behind the panel it covers or paint over dialogs above it.
- 2026-08-16 — `TitleBgActive` was `== TitleBg`, deliberately: the classic
  theme's note records that ImGui paints a *docked node's tab strip* with this
  colour, so a contrasting value makes every panel flare as focus moves. Moving
  it one ladder step (`titlebar` → `raised`) gives dialogs a live title bar and
  docked panels a gentle focus hint, without the flare. The original note is
  still right about *contrasting* values; it is the step size that matters.
- 2026-08-16 — `WindowRounding` 3 → 5 is safe because ImGui squares a window off
  when it is docked, so it lands on dialogs and popups only.

## 9. Self-verification
- [x] build-project        — PASS, exit 0, no new warning
- [x] architecture-review  — PASS (see §3 note on ImGui internals)
- [x] code-review          — PASS
- [x] dependency-audit     — n/a
- [x] performance-review   — n/a. 12 one-pixel rounded rects per floating window
      per frame, and only for windows already being drawn; nothing when no dialog
      is open, since the loop skips docked and child windows.
- [x] testing              — see §10

## 10. Verification result
- Submitted:  2026-08-16
- Verdict:    **PASS**
- Measured from the running app with the Traverse Editor open over the ribbon and
  the Properties panel:
  - **title bar `#323232` vs body `#282828`** — the focused dialog's title reads
    as live, one ladder step above its own surface;
  - **drop shadow, right edge** (y=500): `#0C0C0C → #0D0D0D → #0F0F0F → #111111 →
    #131313 → #141414 → #151515 → #161616 → #171717 → #181818 → #191919` — a
    clean quadratic falloff to the surface behind it over ~10 px;
  - **drop shadow, bottom edge**: the same ramp;
  - the halo lands **on** the ribbon and the Properties panel behind the dialog,
    and no part of the dialog is dimmed by it — the depth ordering is right.
- Popups: confirmed on a tooltip, which picked up the same treatment with no
  change to its own code — which is the generic pass doing its job.
- No stray halo on the dockspace host, the status-bar strip or the floating
  command bar (all NoTitleBar, all excluded).
- Classic theme: opts out by construction — `windowShadow` is zero alpha and
  `DrawWindowDropShadow` returns before drawing; its `WindowRounding` stays 0.
- Findings: none outstanding.

## 11. Outcome
- Requirements satisfied: REQ-081 revision 5 (Acceptance met: yes)
- Tests added:            none — appearance requirement, see §6
- Refactors:              none
- Docs updated:           `spec/requirements.md` (REQ-081 rev 5)
- Note:                   the user's saved `displayColorThemeIdx` was set to **0
                          (Dark)** at their request and is no longer restored to
                          classic after a test run.
- Done:                   2026-08-16
