# TASK-137 — TABLE as a first-class entity with modify and cell edit

- Type:    feature
- Status:  implement
- Opened:  2026-08-28
- Owner:   workshop

## 1. Authority
- Goal:         remaining #119 table work (REQ-148)
- Requirements: REQ-148 (accepted; D-2026-08-28-i)
- Constraints:  CON-07; architecture §11.9 / ADR-035/036 append-only EntityKind
- Acceptance:
  - `VOLREPORT TABLE` after `VOLUMES` adds one TABLE entity (not an annotation);
  - a TABLE with 2 columns and 4 cells lays out four non-empty rectangles inside its box;
  - `VOLREPORT TABLE` with no volume result is a named refusal;
  - MOVE of a TABLE changes its insertion; a cell hit-test returns the row-major index of a point inside that cell.
- Owning subsystem: Domain / Commands / UI / IO

## 2. Scope
- In scope: CadTable store, EntityKind::Table appended, modify commands, in-place cell edit, .gs/DXF, migrate Kind::Table
- Out of scope: native ACAD_TABLE DXF object; paper-space native tables
- Smallest change: entity store + transforms + cell editor + VOLREPORT insert path

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed. Decision D-2026-08-28-i recorded the append-only EntityKind and `.gs` `"tables"` key.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Entity vs annotation | 2026-08-28 | User: entity kind + MOVE/ROTATE/SCALE + in-place cells |

## 5. Assumptions
```
ASSUMPTION-1: DXF stays exploded TEXT
- Because: D-2026-08-28-h (still binding for DXF)
- Risk if wrong: AutoCAD TABLE round-trip missing
- Validate by: existing DxfIo path

ASSUMPTION-2: In-place cell edit is a viewport overlay InputText (same as TEXT)
- Because: REQ-148 does not specify a grid UI widget
- Risk if wrong: richer editor expected
- Validate by: user using double-click
```

## 6. Plan
- Approach: CadTable + parallel attrs; append EntityKind/Type; PDF-like rigid transforms; migrate annotation tables on load
- Files: cadtable.hpp, CadCommands.*, GsIo, DxfIo, PdfPlot, CadUi, docinvariants, tests
- Test approach: happy = VOLREPORT TABLE + layout + JSON + MOVE helper; failure = VOLREPORT TABLE with no result
- Steps:
  - [x] Record D-2026-08-28-i and amend REQ-148
  - [x] CadTable store + sweep/attrs/undo
  - [x] Modify + pick + draw + cell editor
  - [x] IO + tests + build

## 7. Workflow-specific notes
- Feature: D-2026-08-28-i is the recorded decision for the new store.

## 8. Implementation log
- 2026-08-28 opened after user asked for entity kind + modify + in-place cells
## 9. Completion report
COMPLETION REPORT — TASK-137 — 2026-08-28
- Requirements satisfied: REQ-148 (Acceptance met: yes)
- Summary: TABLE is EntityKind::Table (appended after Surface). VOLREPORT TABLE inserts CadTable. MOVE/COPY/ROTATE/SCALE/MIRROR/STRETCH apply to the whole table. Double-click a cell opens an in-place editor (Enter commits, Esc cancels).
- Tests: CadTableTests [req148]; headless req146-vol-table-csv-tin-edits (EXPECT TABLES 1)
- Verification verdict: PASS (build + req148 tests + transcript)
- Assumptions: ASSUMPTION-1 DXF exploded TEXT; ASSUMPTION-2 InputText cell overlay
- Architectural decisions: none made by Workshop (D-2026-08-28-i)
- Dependencies: none
- Technical debt noted: none
- Build: cmake --preset ninja-release / build.bat
- Docs updated: TASK-137
