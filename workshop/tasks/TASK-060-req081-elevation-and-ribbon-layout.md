# TASK-060 — Elevation cues for the ribbon and panels, plus three ribbon/command-bar layout corrections

- Type:    feature + bug
- Status:  done
- Opened:  2026-08-16
- Owner:   Workshop

## 1. Authority
- Requirements: **REQ-081 revision 4** (items 1); **REQ-040** (item 3, the floating
  command bar); **REQ-067** via **BUG-022** (item 4, point groups unreachable)
- Constraints:  no `CON-NN` defined; CLAUDE.md "Additional rules" 1–8 apply
- Acceptance:
  - REQ-081 rev 4, verbatim: *the ribbon and the docked panels read as plates
    above the drawing canvas … a lit edge along the top of a raised plate, and a
    soft shadow that plate casts onto the surface below it, landing on the
    receiving surface (the drawing canvas), not in the gap between them. Light
    comes from the top-left.*
  - BUG-022: the Survey ribbon panel's Groups button is visible and clickable.
  - Items 2 and 3 are presentation defects with no governing requirement; their
    acceptance is the user's report being satisfied and nothing else moving.
- Owning subsystem: **UI** — `src/ui/CadUi.cpp`, plus one constant in `src/app/main.cpp`

## 2. Scope
- In scope, all four items the user reported:
  1. ribbon + Properties panel read as raised over the viewport;
  2. gutter under the ribbon's panel titles;
  3. the floating command bar lifted off the status bar;
  4. the Survey ribbon panel fits its fourth button.
- Out of scope: the classic theme (opts out of the elevation cue entirely); the
  palette (unchanged by this task); any other ribbon section's layout.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public-API or
  data-format change / unspecified algorithm?
    - [x] **No — proceed.** Two `UiChrome` fields added to the struct ADR-033
          already established, and two file-local draw helpers with two call
          sites each. No new owner, no new file, no persisted field.
    - [ ] Yes → STOP.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| — | none — all four items were specific and reproducible from the report | — | — |

## 5. Assumptions
```
ASSUMPTION-1: The light comes from the top-left, so only the canvas's top and
              left edges receive a cast shadow.
- Because:       the user asked for elevation but not for a light direction.
- Risk if wrong: if a user re-docks the Properties panel to the RIGHT, the canvas
                 still shadows on its left and the cue points the wrong way.
                 Cosmetic; nothing is obscured or unclickable.
- Validate by:   the shipped layout (SetupMainDockLayout) puts panels left and
                 the ribbon above, which is the case the cue is aimed at. Revisit
                 only if re-docking becomes a common workflow.
```

## 6. Plan
- Approach: elevation is one pairing, not one colour — a lit top edge on the
  raised plate plus a cast shadow on the surface receiving it. Both go through
  `UiChrome` so the classic theme can opt out with zero alpha, since its 3D
  bevels already state depth and a second, contradictory cue would read as grime.
- Files/functions:
  - `CadUi.cpp` — `UiChrome` (+`plateHilite`, `plateShadow`), both theme
    functions, new `PlateTopHilite` / `CastShadowInto`, `DrawRibbonBar`,
    `DrawPropertiesPanel`, `DrawCadViewportPanel`, `DrawCommandLinePanel`
  - `main.cpp` — `ribbonH` 130 → 139
- Test approach: appearance, so no automated test. Verified by **sampling the
  running app's framebuffer** along the edges in question — a gradient either is
  there in the pixels or it is not — and by measuring the ribbon gutter in pixels
  before and after.
- Steps: [x] chrome fields [x] helpers [x] ribbon plate + gutter
  [x] properties plate [x] canvas receives shadow [x] command-bar lift
  [x] Survey two columns [x] build [x] verify by pixel sampling

## 7. Workflow-specific notes
- Bug (item 4): root cause = a ribbon panel is three small buttons tall by
  construction (`rowH = floor((colH - 4) / 3)`) and Survey stacked four, so the
  fourth was laid out past the section and clipped. No regression test — the
  project's anti-requirements exclude rendered-GUI automation; the standing risk
  is recorded as debt in BUG-022 instead.

## 8. Implementation log
- 2026-08-16 — items 2, 3, 4 were mechanical. Item 1 took three passes, and both
  failures are worth keeping because they look identical to "the code did not run":
  - **Pass 1 — clipped.** Both marks were drawn into the window draw list, whose
    clip rect is the window's **content** region. That excludes exactly the
    window-padding band the marks live in, so they were submitted and then
    clipped away. Fixed by having each helper push its own clip rect.
  - **Pass 2 — aimed at the wrong rect.** The shadow was cast onto the Viewports
    *window* rect. For a docked window that starts above the dock tab bar and its
    content is inset by `WindowPadding`, so the whole 9px gradient landed on the
    tab strip and the padding band — measurably present, visually nothing. Fixed
    by casting onto the **image** rect (`imgPos`..`imgPos+avail`), which is the
    surface that is actually meant to look recessed.
  - Neither pass produced a warning, an assert, or a wrong colour anywhere. Only
    scanning the framebuffer showed the difference, which is why §10 does that
    rather than trusting the source.
- 2026-08-16 — `ribbonH` raised by exactly the gutter (9), so the gutter is new
  space rather than space taken from the buttons.

## 9. Self-verification
- [x] build-project        — PASS, exit 0, no new warning
- [x] architecture-review  — PASS (no architectural item; ADR-033 mechanism reused)
- [x] code-review          — PASS
- [x] dependency-audit     — n/a
- [x] performance-review   — n/a (two extra rects per frame)
- [x] testing              — see §10

## 10. Verification result
- Submitted:  2026-08-16
- Verdict:    **PASS**
- Measured from the running app's framebuffer:
  - **canvas top edge** (x=1600): `#0E0E0E → #101010 → #111111 → #121212 →
    #131313 → #151515 → #161616 → #171717 → #181818 → #191919` over 9 px — a
    clean fade to the canvas tone, no banding, no residual grey;
  - **canvas left edge** (y=1000): the identical ramp over 9 px;
  - both were **flat** (`#282828` / `#191919`, no gradient) in the two failed
    passes, which is what caught them.
- **Ribbon gutter, measured** as (ribbon bottom edge − last row of title text):
  **4 px before → 13 px after.**
- Command bar: bottom edge lifted 10 px clear of the status-bar strip.
- Survey panel: `Inverse`/`Traverse` and `Surfaces`/`Groups` both render;
  **Groups is visible and clickable for the first time** (BUG-022).
- Classic theme: opts out by construction — both new fields are zero alpha, and
  both helpers early-return on zero alpha before drawing anything.
- Findings: none outstanding. One follow-up recorded as debt in BUG-022 (the
  three-row ribbon limit is implicit in an expression rather than asserted, so
  the next section to add a fourth button fails the same silent way).

## 11. Outcome
- Requirements satisfied: REQ-081 rev 4; BUG-022 fixed
- Tests added:            none — appearance + GUI layout, see §6
- Refactors:              none
- Docs updated:           `spec/requirements.md` (REQ-081 rev 4), `TRACKER.md` (BUG-022)
- Technical debt noted:   BUG-022 follow-up — assert ribbon section content fits
- Done:                   2026-08-16
