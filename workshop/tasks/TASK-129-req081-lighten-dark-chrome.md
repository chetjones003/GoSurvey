# TASK-129 — Lighten Dark-theme chrome without touching the viewport

- Type:    feature (appearance)
- Status:  implement
- Opened:  2026-08-28
- Owner:   Workshop

## 1. Authority
- Goal:         n/a
- Requirements: **REQ-081** (accepted)
- Constraints:  CLAUDE.md additional rules 1–8
- Acceptance:   REQ-081's existing conditions (achromatic ladder, even L\* steps,
  stated structural distances, WCAG text on the panel surface, Light theme
  untouched, viewport contents unchanged). This task does not amend the
  requirement; it shifts the Dark ladder **up ~4.5 L\*** so chrome is slightly
  lighter while the same relationships hold.
- Owning subsystem: UI (`src/ui/CadUi.cpp`)

## 2. Scope
- In scope: Dark `ImGuiCol_*` values, matching `g_chrome` hex, and the three
  Dark-only command-line literals that duplicate ladder steps.
- Out of scope: Light theme; viewport background (`viewportBg*`); layout,
  rounding, elevation mechanics; accent hue; semantic triad.
- Smallest change: values only.

## 3. Architectural boundary check
- [x] **No — proceed.** Literal substitution on an existing derived palette.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | none — user asked for a slight global lighten of chrome, viewport unchanged | — | — |

## 5. Assumptions
```
ASSUMPTION-1: ~+4.5 L* on every neutral is "slightly lighter" rather than a new look.
- Because:       one existing ladder step (~4–5 L*); surfaces stay clearly dark.
- Risk if wrong: still too dark or too light; cheap to retune the offset.
- Validate by:   user looking at the running app.
```

## 6. Plan
- Approach: keep step sizes; add 4.5 L* to each named tone; recompute hex;
  bump `textDim` so secondary contrast stays ~5.2:1 on the new surface;
  leave primary text `#D7D7D7` (still ≥ 7:1).
- Files: `src/ui/CadUi.cpp` only.
- Tests: none (appearance; same as TASK-059). Failure mode: leftover old hex
  on command-line `isDark` literals.
- Steps: substitute → keep comments honest with new L* / contrast numbers.

## 7. Log
- 2026-08-28 — user asked to go even lighter; lifted the same ladder another
  ~5.5 L\* (panel `#323232` → `#3E3E3E`). Secondary text `#B6B6B6` (5.27:1).
  Viewport background still untouched.

## 8. Completion report

COMPLETION REPORT — TASK-129 — 2026-08-28
- Requirements satisfied: REQ-081 (Acceptance met: yes — ladder still achromatic
  with even L\* steps; structural distances held; primary 8.91:1, secondary
  5.27:1 on the new surface; Light theme and viewportBg\* untouched)
- Summary: Dark chrome lifted ~4.5 L\* as a set. Viewport background unchanged.
- Tests: none (appearance)
- Verification verdict: PASS (value substitution; leftover old hex on the three
  command-line Dark literals was updated in the same change)
- Assumptions: ASSUMPTION-1 open pending user look
- Architectural decisions: none
- Dependencies: none
- Technical debt noted: none
- Build: values only
- Docs updated: this task log

