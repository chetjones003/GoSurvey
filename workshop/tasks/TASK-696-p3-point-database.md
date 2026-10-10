# TASK — Projects P3: the project point database (issue #696)

- Branch: `feat/projects-p3-point-database`
- Authority: **REQ-376** (+ REQ-382 for read-only); D-2026-10-05-d, **ADR-065 (accepted by the user as
  written, 2026-10-05, before P3 started — recorded in `spec/architecture.md`, `spec/project.md` D-2026-10-05-e,
  `spec/roadmap.md`)**; GitHub #696 (P3)
- Boundary check: file format + diff are pure in `src/io/ProjectPointDb.{hpp,cpp}` (no window, no drawing);
  the per-tab glue is in `src/commands/ProjectPoints.{hpp,cpp}` (domain, next to `ProjectSettings.cpp`);
  `CadUi_Projects.cpp` only calls it once a frame and on open/close. No new dependency (REQ-300).
  The "fourth isolation boundary" of ADR-065 (b) is `ProjectSession::points`, one `shared_ptr<projpts::Db>`
  per open project.

## How it works

Every command already edits `st.surveyPoints`, so none of them change. Once a frame, for the ACTIVE project
tab, `SyncProjectPoints` compares `st.surveyPoints` with what the tab last agreed with the database
(`DrawingTab::pointsBaseWorld`):

- a difference is the user's edit (add / edit / delete by point number) and is folded into the database;
- a database that has moved on (another tab edited it) is pulled into the tab when it becomes active.

Only one tab is on screen at a time, so "live in other tabs" is exactly this: the other tab shows the change
the moment it is shown. Undo needs no code: an undo restores the old list and the next frame's diff pushes it.

## Delivered

1. `Points/survey-points.gspdb` (JSON, `formatVersion`, project ID, each point's `sourceDrawing`), atomic
   write (temp + rename), load refuses damaged / newer / other-project / duplicate-number files and the file is
   then never written over (REQ-201).
2. One shared `projpts::Db` per open project; opened with the project, flushed on last-tab close and on exit.
3. Coordinates stored as WORLD (state-plane), converted at the tab boundary — drawings of one job have
   different document origins (local-storage invariant). A document-origin rebase is not read as an edit.
4. Source drawing recorded on add (project-relative DWG path; the tab name while unsaved); kept on edit.
5. Debounced save (0.75 s after the first unsaved change, retry every 5 s after a failure).
6. Read-only opener: points are visible; any change is undone in the view and reported.
7. Project drawings are saved WITHOUT their points (DWG point blocks + the ADR-044 trailer) — ADR-065 (c).
8. Standalone drawings are untouched: no project → no sync, DWG keeps its points.

## Decisions / assumptions (recorded)

- ASSUMPTION: a drawing that arrives in a project already carrying points (a pre-project DWG) keeps them,
  is NOT shared (`PointsMode::Detached`) and the user is told. Merging them is **Add Drawing to Project**
  (REQ-378, P5), which owns conflict choices. Silently importing them would bypass number-conflict prompts;
  silently dropping them would lose data.
- ASSUMPTION: all project drawings show every database point in P3. Per-drawing visibility rules are P4
  (REQ-377); "a new point appears in the drawing it was created in by default" is P4's.
- ASSUMPTION: REQ-376 clause 4's overwrite / renumber / cancel prompt on a duplicate number is satisfied in
  P3 by the drawing already showing every database point, so Create Points / paste use their existing
  duplicate policies against the full list. The cross-project / unsaved-close warnings are P9 (REQ-383).
- Survey-point label MTEXT is derived data (never written to DWG, rebuilt on load), so labels are not stored
  in the database; a pull rebuilds labels for new / changed points and removes those of deleted points.

## Technical debt

- Undo frames of an INACTIVE tab hold an older point list; undoing in that tab after another tab edited the
  database pushes the old points back (it behaves like a fresh edit).
- A pull replaces `st.surveyPoints` wholesale and clears the point selection.
- Save is synchronous on the UI thread. 100k points: see "Measured" — move to a one-shot worker (§8) if a
  real project makes it visible.
- A project DWG opened outside its folder shows no points (ADR-065 consequence).

## Measured

- 100,000 points: save + load together ≈ 0.8 s in the unit test (release build) — a visible hitch if a
  project that large saves on the UI thread. A typical job (a few thousand points) is a few tens of ms.
  Left synchronous deliberately; the one-shot-worker move is the first follow-up if a real project needs it.
- Full ctest: 9 failures, all headless transcripts for OFFSET / surfaces / solids / feature lines (no
  project involved; the repo already carries ~8 such failures, `req069` segfaults intermittently and passed
  once on rerun). Not re-run against `beta` in this task.

## Tests

- `GoSurveyTests "[req376]"` (`tests/ProjectPointDbTests.cpp`): missing file = empty; full round trip incl.
  source drawing and no label link; damaged / foreign / newer / duplicate rejected and left alone; interrupted
  write keeps the old file; add / edit / delete fold; duplicate number overwritten + reported; origin shift is
  not an edit; 100k save/load timing.
- `GoSurveySnapTests "[req376]"` (`tests/ProjectPointsSyncTests.cpp`): two drawings in sync on add / edit /
  delete with source tagged; different document origins; debounced atomic save + reopen; read-only refuses;
  a drawing with its own points is detached; standalone untouched; damaged database never written over.
- GUI: nothing new is drawn; checked by building the app.
