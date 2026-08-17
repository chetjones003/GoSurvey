# TASK-063 — Keyboard navigation inside the data grids

- Type:    feature
- Status:  open   ← not started; opened as a deliberate follow-up, see §8
- Opened:  2026-08-16
- Owner:   unassigned

## 1. Authority
- Requirements: **REQ-082** — needs an amendment before this is buildable. The
  requirement as accepted covers sorting, column state, frozen headers, uniform
  rows and cell-filling editors; it says nothing about moving between cells.
  Adding keyboard navigation is therefore a **spec change first**, not a coding
  task: see §3.
- Constraints:  no `CON-NN` defined; CLAUDE.md "Additional rules" 1–8 apply
- Acceptance:   to be written into REQ-082 as part of step 1 below. Proposed:
  - `Tab` / `Shift+Tab` commit the cell and move to the next / previous cell in
    the row, wrapping to the next / previous row at the ends;
  - `Enter` commits and moves **down** one row in the same column;
    `Shift+Enter` moves up;
  - `Esc` abandons the edit in progress and restores the cell's prior value;
  - arrow keys move the focused cell when no edit is in progress;
  - the focused cell is visibly marked, and scrolls into view when focus reaches
    a row outside the viewport;
  - navigation follows the **displayed** order, so it agrees with the active
    sort rather than with storage order.
- Owning subsystem: **UI** — `src/ui/CadUi.cpp` (the two grids and the shared
  `PushGridCellStyle` helpers)

## 2. Scope
- In scope: the two windows REQ-082 already governs — the survey points grid
  (VIEWPOINTS) and the Layer Manager.
- Out of scope, unless separately requested: range selection, fill-down /
  fill-right, copy/paste of a cell range, undo of a grid edit as one step,
  formulas. These are the parts of "like Google Sheets" that are a spreadsheet
  *engine* rather than a grid, and each is its own requirement.
- Smallest change: a per-grid "focused cell" (row, column) held in
  `AppCommandState` alongside the other UI state, consulted by the row loop to
  decide which widget receives `ImGui::SetKeyboardFocusHere()`.

## 3. Architectural boundary check  (fill before planning)
- Does this need a NEW abstraction / layer / dependency / ownership change /
  global state / public-API or data-format change / algorithm the spec didn't
  specify?
    - [ ] No — proceed.
    - [x] **Probably yes, and it must be answered before any code.** Two items:
          1. **REQ-082 does not cover this** — building it as written would be
             implementing to an unwritten rule, which is the thing the SPEC GAP
             mechanism exists to stop. Amend REQ-082 (or open REQ-083) first.
          2. Focused-cell state has to live somewhere. `AppCommandState` is the
             established home for UI state and needs no new owner, so this is
             likely a plain field rather than an architectural change — but it is
             **two** grids, so whoever picks this up must decide whether they get
             one shared focus record or one each, and say which in the task.

## 4. Questions  (ask before guessing)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Does `Enter` commit-and-move-down (Sheets/Excel) or commit-and-stay (today's ImGui behaviour)? Changing it alters muscle memory for anyone already using the grids. | — | — |
| Q2 | Should navigation wrap past the last row — creating a new record, as Sheets does — or stop? For survey points a new row means a new point ID, which is a data decision, not a UI one. | — | — |
| Q3 | Do the arrow keys navigate cells, or keep their current meaning inside a focused text field (moving the caret)? Both cannot be true at once. | — | — |

## 5. Assumptions
```
(none yet — the questions above must be answered rather than assumed. Each one
changes what gets built, and Q2 in particular has a data consequence.)
```

## 6. Plan  (write BEFORE any code)
- Steps:
  - [ ] 1. Amend REQ-082 with the acceptance conditions in §1, having answered
           Q1–Q3. **Nothing below starts until this is recorded.**
  - [ ] 2. Add the focused-cell state and mark the focused cell visibly.
  - [ ] 3. `Tab` / `Shift+Tab` traversal within a row, wrapping between rows.
  - [ ] 4. `Enter` / `Shift+Enter` vertical movement; `Esc` abandons the edit.
  - [ ] 5. Arrow-key movement outside an active edit (subject to Q3).
  - [ ] 6. Scroll the focused cell into view.
  - [ ] 7. Apply to both grids; confirm navigation follows the sorted view.
- Test approach: the project's anti-requirements exclude rendered-GUI automation,
  so this is manual — but one part is worth extracting and unit-testing: the pure
  "given (row, col) and a key, what is the next (row, col)" function, including
  its wrap behaviour at both ends. That is where an off-by-one would live, and it
  needs no window to test. Follows the `PlotFont.hpp` / OrthoConstrain precedent.

## 7. Workflow-specific notes
- Feature: the pre-flight is Q1–Q3 plus the REQ-082 amendment. Do not start at
  step 2.

## 8. Implementation log
- 2026-08-16 — **opened, not started**, at the user's request while shipping
  0.5.1. Recorded as TASK-062's ASSUMPTION-1: "behave like a spreadsheet" was
  delivered as sorting, column state, frozen headers, uniform rows and
  cell-filling editors, and deliberately **not** as in-grid keyboard navigation.
  Nothing was lost by that split — every cell edits exactly as it did before —
  and the helpers TASK-062 introduced (`PushGridCellStyle`, `GridCheckbox`,
  `GridRadio`, `kGridTableFlags`) are what this builds on.
- Useful context for whoever picks this up: the grids already iterate a **view**
  (`order` / `layOrder`) while addressing records by **storage** index, and
  `PushID` uses the storage index so a row's edit state stays with its record
  across a re-sort. Keyboard navigation must move through the **view**, or the
  cursor will jump around whenever a sort is active. That distinction is the
  single thing most likely to be got wrong here.

## 9. Self-verification
- [ ] build-project / architecture-review / code-review / dependency-audit /
      performance-review / testing — none run; not started.

## 10. Verification result
- Not submitted.

## 11. Outcome
- Not delivered.
