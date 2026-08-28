# TASK-138 — Auto-fit TABLE grid to cell text

- Type:    bug
- Status:  implement
- Opened:  2026-08-28
- Owner:   workshop

## 1. Authority
- Goal:         remaining #119 table work (REQ-148)
- Requirements: REQ-148 (accepted; D-2026-08-28-j)
- Constraints:  CON-07
- Acceptance:   CadTableFitToContent makes width at least the longest cell at plotted height and height at least one text-height per row; a longer cell after a fit increases width.
- Owning subsystem: Domain / Commands / util

## 2. Scope
- In scope: CadTableFitToContent; VOLREPORT TABLE insert; cell-edit commit
- Out of scope: per-column width array; wrapping; paper-native tables
- Smallest change: fit equal columns from estimated glyph width

## 3. Architectural boundary check
- [x] No — fit writes existing width/height; no new store.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Equal columns vs per-column widths | 2026-08-28 | Equal columns (existing layout); size to the longest cell |

## 5. Assumptions
```
ASSUMPTION-1: 0.70 em × char count bounds ImGui/TTF cell glyphs
- Because: util cannot call ImGui
- Risk if wrong: rare overflow on wide glyphs
- Validate by: volume table screenshot after VOLREPORT TABLE
```

## 6. Plan
- Approach: header-only fit from plotted height; call on insert and cell commit
- Files: cadtable.hpp, CadCommands.cpp, CadTableTests.cpp, spec
- Test: fit vs 48×8 hardcoded box; grow on longer cell
- Steps:
  - [x] D-2026-08-28-j + REQ-148
  - [x] CadTableFitToContent
  - [x] insert + commit + tests

## 7. Workflow-specific notes
- Bug: root cause = VOLREPORT TABLE hardcoded width=48 and height=4×rows while font uses plottedHeight×mup; CommitTableCellEditor did not refit.

## 8. Implementation log
- 2026-08-28 fit on insert and cell commit
