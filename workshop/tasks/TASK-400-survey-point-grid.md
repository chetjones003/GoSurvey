# TASK — Survey Point Database grid panel

- Branch: `feat/req400-survey-point-grid`
- Authority: **REQ-400** (accepted); D-2026-10-09-a; builds on REQ-376/ADR-065 (P3) and REQ-377 (P4,
  issue #696). No GitHub issue filed for REQ-400 itself.
- Boundary check: no new kernel/domain type needed — the panel reads/writes `projpts::Db` directly
  (already pure, `src/io/ProjectPointDb.hpp`) and reuses `src/commands/ProjectPoints.{hpp,cpp}` glue.
  The grid widget itself is UI-only, a new `DrawSurveyPointGridPanel` beside `DrawSurveyDatabaseFolder`
  in `CadUi_Toolspace.cpp` (or a new sibling file `CadUi_SurveyPointGrid.cpp` if it grows past ~150
  lines — matches how other dock panels are split out). No new dependency (REQ-300).

## Files affected

- `src/commands/ProjectPoints.hpp` / `.cpp` — new functions:
  - `EditDatabasePointResult EditDatabasePoint(AppCommandState& st, int pointNumber, const SurveyPoint& newValues, std::vector<std::string>& log)`
    — writes straight into `ActiveProjectDb(st)`, bumps `revision`/`dirty` (mirrors what
    `ApplyChanges` already does for a single point), reports a point-number collision rather than
    resolving it (see below).
  - `int SelectDatabasePoints(AppCommandState& st, const std::vector<int>& pointNumbers)` — sets
    `st.selectedSurveyPointIndices` to the matching entries of `st.surveyPoints` (i.e. only ones
    currently **visible** in the active drawing per REQ-377); returns how many of the requested
    numbers were not selectable (hidden/not found), for the panel to report.
  - A read accessor returning `const std::vector<projpts::Entry>&` (or a snapshot) for the grid to
    render — `ActiveProjectDb(st)` already gives a non-const `Db*`, so the panel can read
    `db->points` directly; no new accessor needed unless `ActiveProjectDb` cannot be called from the
    read path too (it can — same function).
- `src/ui/CadUi_Toolspace.cpp` — `DrawSurveyPointGridPanel(AppCommandState& cmd, std::vector<std::string>* log)`:
  an ImGui dockable window (`ImGui::Begin`/table), opened via a new button/menu item next to the
  existing Survey Database folder's filters ("Open table"), gated the same way
  (`ActiveProjectDb(cmd) == nullptr` → hidden). Columns per REQ-400 clause 2. Uses `ImGuiTableFlags_Sortable`
  and sorts a locally-held index array (never reorders `db->points` itself — order is append order /
  database order per REQ-400 clause 1's "in database order" baseline, sort is a view).
- Window registration: wherever other dock panels (Properties, Point Group Manager) register their
  `show*Window` bool and get drawn each frame — add `showSurveyPointGridWindow` to `AppCommandState`
  next to `showPointGroupManagerWindow`.
- `tests/ProjectPointsSyncTests.cpp` (or a new `tests/SurveyPointGridTests.cpp`) — `[req400]` cases.

## Implementation approach

1. **Edit write-through (clause 4).** `EditDatabasePoint` looks up the entry by `pointNumber` in
   `ActiveProjectDb(st)->points`. If the edit's new `point.id` (number) is unchanged, it overwrites
   the entry's fields in place, bumps `db->revision` and marks `dirty` (same bookkeeping
   `ApplyChanges` does) — this is enough for `SyncProjectPoints`'s existing "pull" path to update
   every open tab next frame, since a tab compares its `pointsBaseWorld` against the current view.
   If the edit also changes the point number into one that collides with a different existing entry,
   do **not** apply it — return a `Collision` result naming the existing entry, and the panel raises
   the same overwrite/renumber/cancel modal `CreatePointsOptions`/REQ-023 already uses (reuse the
   modal, do not build a second one).
2. **Read-only gating (clause 6).** The panel must know whether the active project is the lock holder
   or a read-only opener (REQ-382) — find the existing flag (`ProjectSession` likely has one, check
   before assuming a name) and disable `ImGuiTableColumnFlags_NoResize`-style edit widgets /
   `ImGui::BeginDisabled(readOnly)` around the editable cells.
3. **Visibility column (clause 2).** For each database entry, visible = the entry's point number is
   present in `st.surveyPoints` (the active drawing's already-filtered view) — no separate rule
   evaluation needed, since `SyncProjectPoints` already did it.
4. **Selection (clause 7).** Row checkbox / multi-select → `SelectDatabasePoints`; report the refused
   count via `log` ("N point(s) not selected: hidden in this drawing").
5. **No project / absent panel (clause 3).** Mirror `DrawSurveyDatabaseFolder`'s existing guard.

## Test approach

- `[req400]` unit tests over `ProjectPoints.cpp`'s new functions against a temp project + `projpts::Db`
  fixture (same harness `ProjectPointsSyncTests.cpp` already uses): edit each field, number-collision
  result, read-only refusal, selection against a filtered `st.surveyPoints`, selection refusal for a
  hidden number.
- GUI: build + manual check (hover/hover-driven hooks are not automatable here, per
  `feedback_devshell_driver_gotchas` / existing P3/P4 task notes) — open the panel on a project with
  points from two drawings, edit a cell, confirm the other open tab updates.

## Architectural-boundary check

- No new dependency, no new file-format version (reuses `projpts::kFormatVersion`).
- UI stays UI: no ImGui types leak into `ProjectPoints.hpp`/`ProjectPointDb.hpp`.
- Reuses the existing overwrite/renumber/cancel modal rather than inventing a second conflict UI.

## Open items to confirm during implementation

- Exact name/shape of the read-only flag on `ProjectSession` (REQ-382) — confirm before wiring
  `EndBeginDisabled`.
- Whether `ImGuiTableFlags_Sortable` sort-by-click needs a stable row-id (point number) to avoid
  jumping rows under the cursor while another tab's edit changes the list mid-sort — use point number
  as the ImGui row id either way.
