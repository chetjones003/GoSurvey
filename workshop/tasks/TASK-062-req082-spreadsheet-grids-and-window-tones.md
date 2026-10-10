# TASK-062 — Inset tones and tab strips for windows; spreadsheet behaviour for the two data grids

- Type:    feature + bug
- Status:  done
- Opened:  2026-08-16
- Owner:   Workshop

## 1. Authority
- Requirements: **REQ-081 revision 6** (window tones, tab strip); **REQ-082**
  (tabular windows behave as data grids); **BUG-023** (VIEWPOINTS → Load crash)
- Constraints:  no `CON-NN` defined; CLAUDE.md "Additional rules" 1–8 apply
- Acceptance:   REQ-081 rev 6 and REQ-082's condition lists, verbatim in the spec.
- Owning subsystem: **UI** (`src/ui/CadUi.cpp`, `src/ui/CadUiSettings.cpp`) plus
  one fix in **survey** (`src/survey/SurveyPoints.cpp`) for BUG-023.

## 2. Scope
- In scope:
  1. `ImGuiCol_ChildBg` becomes an inset tone, so every boxed/scrolling region in
     every window separates from its window — this is what the user asked for on
     Options, Import points, Viewpoints and the Layer Manager at once;
  2. a tab-strip background behind the Options dialog's tab bar;
  3. spreadsheet behaviour for the Viewpoints grid and the Layer Manager;
  4. BUG-023, found while testing (3).
- Out of scope: the classic theme (its `ChildBg` is set explicitly and unchanged);
  the *content* of any dialog beyond the two grids; making other tables (traverse
  editor, reports) into data grids — they were not asked for and `kGridTableFlags`
  is there when they are.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public-API or
  data-format change / unspecified algorithm?
    - [x] **No — proceed.** Four file-local helpers (`PushGridCellStyle`,
          `PopGridCellStyle`, `GridCheckbox`, `GridRadio`) and one `constexpr`
          flag set, each with two present-day call sites (the two grids), which is
          what REQ-301 asks for. The BUG-023 fix adds no field — it maintains an
          invariant on data that already exists.
    - [ ] Yes → STOP.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| — | none — the request named the windows and the two properties wanted | — | — |

## 5. Assumptions
```
ASSUMPTION-1: "Behave like a spreadsheet" means sort / resize / reorder / hide
              columns, a frozen header, uniform rows, and cell-filling editors —
              not in-grid keyboard navigation (arrow keys between cells, Tab to
              commit-and-advance, range selection, fill-down).
- Because:       "like Google Sheets" names a family of behaviours; the ones above
                 are what the screenshots' complaints point at.
- Risk if wrong: the grids look and sort like a sheet but are still navigated one
                 control at a time. Nothing is lost — every cell remains editable
                 exactly as before.
- Validate by:   the user's use of the grids. Keyboard navigation is a contained
                 follow-up on the same helpers if wanted.
```

## 6. Plan
- Approach: fix the *cause* rather than each window. The complaint "scroll
  sections need a different tone" is one style colour (`ChildBg`) that had been
  set equal to `WindowBg`; changing it repairs all four named windows and every
  unnamed one. The grid complaint is likewise not about the tables but about the
  widgets in them, so it is fixed once in a shared style push.
- Files/functions: `CadUi.cpp` (palette `inset`, `PushGridCellStyle`/`Pop`,
  `GridCheckbox`, `GridRadio`, `kGridTableFlags`, `DrawLayerManagerWindow`,
  `DrawViewPointsPanel`); `CadUiSettings.cpp` (tab strip, transparent layout
  child); `SurveyPoints.cpp` (BUG-023).
- Test approach: appearance and interaction, so driven in the running app and
  checked against what is on screen. Failure mode for the sort work is the
  dangerous one — a view order that rewires which record a control edits or
  deletes — so that is an acceptance condition and was checked explicitly.
- Steps: [x] inset ChildBg [x] tab strip [x] grid helpers [x] Viewpoints grid
  [x] Layer Manager grid [x] BUG-023 [x] build [x] verify in the app

## 7. Workflow-specific notes
- Bug (BUG-023): root cause = `LoadSurveyPointsFromJsonFile` clears the parallel
  `surveyPointIdBuffers` and never refills it, while the grid resizes them
  *before* submitting the Load button — so the table reads N rows off an empty
  vector in the same frame. **Pre-existence was proved, not asserted**: the change
  under test was stashed, the pristine tree rebuilt, and the same click
  reproduced the same `0xC0000005`. No regression test — the anti-requirements
  exclude rendered-GUI automation; the debt is recorded in BUG-023 instead.

## 8. Implementation log
- 2026-08-16 — `ChildBg` had been set equal to `WindowBg`, which is why no boxed
  region in any window separated from its window. Set one ladder step below
  (`#1F1F1F`). One consequence had to be handled: nested children then take the
  same tone, so the Options dialog's outer layout child is pushed transparent —
  otherwise its inner boxes are the same tone as their own background and the
  effect cancels out. That is stated in REQ-081 rev 6 so it is not "fixed" later.
- 2026-08-16 — the tab bar has no background in ImGui at all; tabs are drawn onto
  whatever is behind them. Filling one frame-height row before submitting the bar
  is enough, since a tab bar is exactly `GetFrameHeight()` tall.
- 2026-08-16 — **caught in verification**: `PushGridCellStyle` makes frames
  transparent so text cells read as cells, which **erased every unchecked
  checkbox** in the Layer Manager — a checkbox *is* its frame. `GridCheckbox` /
  `GridRadio` put the recess back for toggle cells only, and centre them.
  Visible in the first Layer Manager screenshot as blank Freeze/Lock columns.
- 2026-08-16 — also caught in review: `PushGridCellStyle` read `ImGuiCol_FrameBg`
  *after* pushing over it to derive the active colour, so `GetStyleColorVec4`
  returned the transparent value it had just pushed and an edited cell would have
  shown no recess either. Both originals are now captured before any push.
- 2026-08-16 — sorting is a view permutation (`order` / `layOrder`), never a sort
  of the storage: `surveyPointIdBuffers` is indexed by point index, labels are
  looked up by it, and both grids' delete paths take an index. `PushID` still
  uses the **storage** index, so a row's edit state stays with its record when
  the sort changes. The Layer Manager's inline `delete-then-continue` was
  converted to a deferred delete, since deleting mid-iteration invalidated both
  the row reference and the index view.

## 9. Self-verification
- [x] build-project        — PASS, exit 0, all targets, no new warning
- [x] architecture-review  — PASS (§3)
- [x] code-review          — PASS; two defects found and fixed during it (§8)
- [x] dependency-audit     — n/a
- [x] performance-review   — n/a. The sort is a `stable_sort` over an index
      vector, per frame, on tables of tens of rows; the previous cost was a
      linear walk of the same data.
- [x] testing              — see §10

## 10. Verification result
- Submitted:  2026-08-16
- Verdict:    **PASS**
- Driven in the running app:
  - **Options** — boxed regions ("Window Elements", "Display resolution", …) now
    sit on a visibly darker tone than the dialog; the tab row has a strip with
    "Display" standing proud of it; recessed inputs remain a further step down;
  - **Layer Manager** — compact uniform rows, cells filling their columns, grid
    rules, frozen header **and** frozen Name column; sorted by Name ascending with
    the marker shown; unchecked Freeze/Lock boxes visible and centred after the
    `GridCheckbox` fix;
  - **Viewpoints** — loaded a 7-row fixture written deliberately out of order
    (103, 101, 107, 102, 105, 104, 106) and the grid displayed **101–107 ascending**;
    clicking *Description* re-sorted to ALPHA MARKER / BOLT TOP / EHOUSE CRNR /
    HEATER PAD CRNR ×2 / MID SPAN / ZULU POINT, with the two equal keys holding a
    **stable** order (101 then 102) and the sorted column marked;
  - **BUG-023** — the same Load click that crashed both the pristine build and the
    pre-fix build now loads cleanly and the grid renders all 7 rows.
- **Delete under a non-default sort — exercised before the 0.5.1 release**, since
  it is the one acceptance condition that can destroy data if the view/storage
  mapping is wrong. With the grid sorted by Description *descending*
  (ZULU POINT / MID SPAN / HEATER PAD CRNR ×2 / EHOUSE CRNR / BOLT TOP /
  ALPHA MARKER), the delete on **row 2** removed **id 106 (MID SPAN)** — the
  record displayed there. The count went 7 → 6 and every other id survived. The
  failure this rules out is precise: had the view index been used as the storage
  index, row 2 would have deleted storage slot 1, which in the file's order is
  **id 101** — and 101 is still present.
- Findings: none outstanding. Two follow-ups recorded as debt in BUG-023 (the
  parallel `surveyPointIdBuffers` array is the underlying hazard) and BUG-022.

## 11. Outcome
- Requirements satisfied: REQ-081 rev 6, REQ-082; BUG-023 fixed
- Tests added:            none — appearance + GUI interaction, see §6
- Refactors:              grid cell styling shared by both data grids
- Docs updated:           `spec/requirements.md` (REQ-081 rev 6, REQ-082 + matrix),
                          `TRACKER.md` (BUG-023)
- Technical debt noted:   BUG-023 follow-up — fold the edit buffer into
                          `SurveyPoint` so no parallel array can fall out of sync
- Done:                   2026-08-16
