# TASK — Projects P9: warnings before destructive or cross-project actions (issue #696)

- Branch: `feat/projects-p9-warnings`
- Authority: **REQ-383** (+ REQ-376 clause 4, REQ-377, REQ-382); **D-2026-10-05-k** (the user chose option A for
  paste / import); GitHub #696 (P9)
- Boundary check: the rules are in `src/commands/ProjectWarnings.{hpp,cpp}` (domain: `AppCommandState`, no window)
  and in the sync that already owns the point diff (`ProjectPoints.cpp`). The dialogs are in
  `ui/CadUi_Projects.cpp` / `CadUi_Modals.cpp` and only show what the domain raised and store the answer. One
  pure helper (`projpts::ShowsEntry`) is added to the existing `io/ProjectPointRules`. No new dependency.

## Delivered

1. **Delete (clause 4).** `SyncProjectPoints` sees a drawing's deleted points as before, but if another drawing of
   the project could lose them it raises `pointEditPrompt` and waits. The dialog says how many **open** drawings
   show the points (and names them) and how many **closed** drawings of the project might. Answers: **Delete from
   project**, **Hide in this drawing only** (the database is not touched; the numbers go in the drawing's
   `hidden` rules), **Cancel** (the points come back). A project with no other drawing deletes without asking.
2. **Number conflict (clause 2).** Replaces P4's silent refusal: **Overwrite**, **Renumber** (next free number
   above the database, the drawing and the drawing's next-number counter; label MTEXT re-linked) or **Cancel**.
   When one edit has both a conflict and a delete, the conflict is asked first.
3. **Paste (clauses 1 and 3).** Copy records the source drawing's project, coordinate system and unit on the
   clipboard. `PASTE` / `PASTEORIG` check them first: a coordinate-system mismatch where either drawing is in a
   project is **blocked** (Cancel only); a different project, or different units, warns (**Paste anyway** /
   Cancel). Two standalone drawings are never checked.
4. **Unsaved close (clause 6).** Quitting and closing a project's last drawing tab first try to write the project
   database; if that still fails, the project and its open drawings are listed. The quit prompt also tags each
   unsaved drawing with its project, and "Save All" retries the database write.
5. **Live edit (clause 5)** was delivered in P3 (`[req376]` tests) and is unchanged.

## Decisions / assumptions (recorded in REQ-383 clause 7 and D-2026-10-05-k)

- ASSUMPTION: "other drawings affected" counts open drawings by their saved visibility rules and closed project
  drawings (every `.dwg` under the Drawings folder that is not open) as "might"; their rules live in their own DWG
  and are not read for a warning. A project with no other drawing asks nothing.
- ASSUMPTION: one answer covers every point of the same kind in one edit.
- ASSUMPTION: "unsaved database changes" = changes that could not be written, because the database saves itself
  0.75 s after each edit. Closing a tab that is not the project's last cannot lose them, so it does not ask.
- ASSUMPTION: a units mismatch compares metres per unit with a 1e-12 tolerance, so international and US survey feet
  count as different (as in Add Drawing, REQ-378).

## Technical debt

- Pasting survey points and a **Convert** option for pasted content do not exist (D-2026-10-05-k); a follow-up
  issue is to be filed.
- Closing a single tab of a standalone drawing still has no unsaved-changes prompt (existing behaviour, not this
  requirement).
- The dialogs are GUI paths (cannot be driven headless); the rules underneath are tested.

## Tests

- `GoSurveySnapTests "[req383]"` (`tests/ProjectWarningsTests.cpp`): delete asks with the right counts and names,
  waits without changing anything, proceeds; hide-only leaves the database revision untouched and other drawings
  showing the point; cancel restores; no other drawing = no question and one closed drawing = a question; a
  drawing that does not show the point is not counted; conflict overwrite / renumber / cancel; conflict then
  delete in one edit; read-only never asks; leaving the tab drops the question; paste: other project warns,
  coordinate-system blocks (both paste routes), units warn, standalone / same project unchecked, Copy tags the
  clipboard; unsaved close lists the project and its drawings and clears once the write works.
- `[req376]` / `[req377]` sync tests updated: their deletes and the P4 refusal now answer the question.
