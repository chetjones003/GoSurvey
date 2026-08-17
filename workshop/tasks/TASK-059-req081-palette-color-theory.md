# TASK-059 — Put the Dark palette on a derived footing (L* ladder, one hue, measured contrast)

- Type:    feature
- Status:  done
- Opened:  2026-08-16
- Owner:   Workshop

## 1. Authority
- Goal:         n/a — no `GOAL-NN` exists (`spec/project.md` §1–4 are placeholders)
- Requirements: **REQ-081 revision 2** (accepted 2026-08-16)
- Constraints:  no `CON-NN` defined; CLAUDE.md "Additional rules" 1–8 apply
- Acceptance:   REQ-081's revision-2 clauses, verbatim —
  - every neutral shares one hue and a low, near-constant saturation;
  - the neutral ladder steps on roughly even CIE L\*, and each structural
    relationship is a stated L\* distance;
  - primary text ≥ 7:1 and secondary text ≥ 4.5:1 on the panel surface;
  - the accent is one hue at several lightnesses/alphas, near the neutrals'
    complement;
  - the semantic triad is equiluminant within ~2 L\* and each member carries its
    label at ≥ 4.5:1.
  (Revision 1's conditions continue to apply and must not regress.)
- Owning subsystem: **UI** — `src/ui/CadUi.cpp` only

## 2. Scope
- In scope: the Dark theme's colour *values* — `ApplyCadDarkTheme`'s `ImGuiCol_*`
  block and its `g_chrome` block, plus the three `isDark` ternaries outside it
  (console background, command-bar background, console field) and the dark branch
  of `PushModeToggleButtonColors`. Two file-local `Hex`/`HexU32` helpers so the
  palette is written in the notation it was designed and validated in.
- Out of scope: the classic theme (untouched); the `UiChrome` mechanism (ADR-033,
  unchanged); style metrics (rounding/padding — revision 1's are working); the
  viewport background (`viewportBg*` is a user setting, not a theme value); any
  layout or behaviour.
- Smallest change: values only. No new field, no new call site, no new file.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public-API or
  data-format change / unspecified algorithm?
    - [x] **No — proceed.** `Hex`/`HexU32` are two file-local 3-line conversion
          functions with ~40 present-day call sites; they are notation, not an
          abstraction (REQ-301 is about interfaces/traits/templates/generics).
          Everything else is a literal substitution.
    - [ ] Yes → STOP.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| — | none — the user's brief ("use good colour theory to make the palette come together") names the method, and the defects were measurable rather than matters of taste | — | — |

## 5. Assumptions
```
ASSUMPTION-1: A slight cool cast on the neutrals (H≈220, S 11-16%) is wanted,
              rather than the reference's pure achromatic gray.
- Because:       "good colour theory" implies a deliberate hue relationship, and
                 a warm accent against cool neutrals is the standard way to make
                 an accent advance. The Hazel reference is achromatic.
- Risk if wrong: the UI reads faintly blue where the user wanted neutral. Cheap
                 to reverse — set R=G=B in the seven ladder constants and nothing
                 else changes.
- Validate by:   the user's look at the running app.
```

## 6. Plan
- Approach: measure the shipped palette, name the defects numerically, design a
  replacement against stated rules, **validate the replacement before writing
  any of it into the source**, then substitute.
- Files/functions: `src/ui/CadUi.cpp` — `Hex`/`HexU32` (new), `ApplyCadDarkTheme`,
  `DrawCommandLinePanel` (3 `isDark` ternaries), `PushModeToggleButtonColors`.
- Test approach: no automated test (appearance; the anti-requirements exclude
  rendered-GUI automation). Instead: arithmetic validation of every value before
  use, then pixel-sampling the running app to confirm what shipped is what was
  designed. Failure mode = a theme switch leaving stale chrome, checked by a
  static field-coverage diff rather than by clicking.
- Steps:
  - [x] measure the shipped ramp (L\* per tone, step sizes, WCAG, triad spread)
  - [x] design + validate the replacement arithmetically
  - [x] amend REQ-081 with the rules the defects broke
  - [x] substitute values; build
  - [x] verify in the running app by sampling rendered pixels

## 7. Workflow-specific notes
- Feature: pre-flight — the brief was method-level, not value-level, so the
  defects were derived by measurement rather than asked about. Tests-first does
  not apply; the equivalent here is that every value was validated numerically
  *before* it entered the source, which is what the "Measured" block below is.

## 8. Implementation log
- 2026-08-16 — measured the shipped palette. Three defects, all real:
  - **border L\* 6.3 vs tab strip L\* 6.8 — 0.5 apart.** The panel outline was
    invisible precisely where panels meet, which is the one place it exists to
    work. This is the defect the eye was registering.
  - **ramp unevenly distributed**: the four darkest tones spanned 5 L\* (steps
    2.0 / 0.5 / 2.5) while the three lightest spanned 16 (steps 4.9 / 5.2 / 5.9)
    — flat at the bottom, abrupt at the top.
  - **`TextDisabled` at 3.93:1**, below WCAG AA, while carrying real secondary
    content (hints, derived readouts, command hints).
  - **axis triad spanned 13.6 L\*** (Y at 56.4 vs X at 42.8), so the green badge
    outranked the other two, and its white letter sat at 3.00:1.
- 2026-08-16 — replaced with a derived palette (values + measurements in the
  source comment block). `accentDeep` was declared in the palette block but its
  only use is in another function; rather than leave a dead constant (clang
  `-Wunused-variable`) the value is written at its use site and the ladder's
  third step is documented in a comment.
- 2026-08-16 — harmonised three values that had been left on the old scheme: the
  console prompt green was `#22C55E` (a saturated pure green unrelated to
  anything else in the palette) and is now the palette's success hue lightened
  for text; the console/command-bar backgrounds now sit on named ladder steps
  rather than on their own one-off grays.

## 9. Self-verification
- [x] build-project        — PASS. `cmake --build build`, MSVC 14.50 / Ninja
      Release, exit 0, all targets. No new warning; the one clang finding
      (`accentDeep` unused) was fixed rather than suppressed.
- [x] architecture-review  — PASS. No architectural item; ADR-033's mechanism is
      untouched and the change stays inside the UI subsystem.
- [x] code-review          — PASS. Values only. Field-coverage checked
      mechanically (§10) rather than by reading.
- [x] dependency-audit     — n/a
- [x] performance-review   — n/a (constant initialisation, once per theme switch)
- [x] testing              — see §10

## 10. Verification result
- Submitted:  2026-08-16
- Verdict:    **PASS**
- Measured — designed values, computed not estimated:

  | | |
  |---|---|
  | neutral ladder L\* | 3.3 / 6.3 / 9.2 / 11.7 / 16.0 / 20.7 / 25.6 |
  | ladder steps | 3.0 / 2.8 / 2.5 / 4.3 / 4.7 / 4.9 (was 2.0 / 0.5 / 2.5 / 4.9 / 5.2 / 5.9) |
  | neutral hue / sat | 218–225° / 11–16% (one hue, as required) |
  | panel over ground | 6.8 L\* (was 4.9) |
  | seam under ground | 5.8 L\* (was 2.5 — and 0.5 against the tab strip) |
  | field under panel | 9.7 L\* |
  | header over panel | 4.7 L\* |
  | panel over tab strip | 4.3 L\* |
  | `Text` #D4D7DD | 10.26:1 — AAA |
  | `TextDisabled` #949AA6 | 5.24:1 — AA (was 3.93:1, FAIL) |
  | accent #F0C67C / #E0AE5E | 9.21:1 / 7.32:1 |
  | accent vs neutral hue | 37° vs 218° — 181° apart, near-complementary |
  | axis triad L\* | 45.6 / 44.8 / 46.5 — spread **1.7** (was 13.6) |
  | axis letter #F2F2F2 | 4.70 / 4.84 / 4.54 :1 — all ≥ AA (was 4.89 / **3.00** / 4.58) |

- Per acceptance condition:
  - one hue, constant saturation — **PASS** (table above);
  - even L\* ladder + stated structural distances — **PASS**;
  - text ≥ 7:1 / ≥ 4.5:1 — **PASS**;
  - accent one hue near the complement — **PASS**;
  - triad equiluminant within ~2 L\* and ≥ 4.5:1 — **PASS** (1.7 L\*).
- Revision-1 conditions re-checked: **no regression.** Panels still separate;
  the Light theme is still untouched (no classic value was edited); viewport
  contents unchanged; badges still present on X/Y/Z and absent on `Length`.
- Independent confirmation that what was designed is what shipped: the running
  app's framebuffer was sampled at six points and returned `#121419` (field),
  `#171A1F` (ground), `#1C1F25` (tab strip), `#24282F` (surface), `#2E323A`
  (section header) — the designed hexes exactly.
- Theme-switch safety checked mechanically rather than by clicking: the
  `UiChrome` field list was diffed against the assignments in each theme —
  **27 fields, 27 written by Dark, 27 written by Light, no typos**. So no switch
  can leave a stale colour behind.
- Findings: none outstanding.

## 11. Outcome
- Requirements satisfied: REQ-081 revision 2 (Acceptance met: yes, subject to
  ASSUMPTION-1 — the user's call on the cool cast)
- Tests added:            none — appearance requirement, see §6
- Refactors:              palette written in hex via `Hex`/`HexU32`
- Docs updated:           `spec/requirements.md` (REQ-081 revision 2)
- Done:                   2026-08-16
