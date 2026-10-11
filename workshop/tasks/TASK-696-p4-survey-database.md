# TASK — Projects P4: per-drawing visibility rules + Survey Database toolspace section (issue #696)

- Branch: `feat/projects-p4-survey-database`
- Authority: **REQ-377** (accepted); D-2026-10-05-d (decision 4); builds on REQ-376 / ADR-065 (P3); GitHub #696 (P4)
- Boundary check: the rules and the filter are pure in `src/io/ProjectPointRules.{hpp,cpp}` (no window, no
  drawing). The per-tab glue stays in `src/commands/ProjectPoints.cpp`. The toolspace section is
  `DrawSurveyDatabaseFolder` in `CadUi_Toolspace.cpp` and only edits `AppCommandState::pointVisibility`.
  The trailer carries the rules in `GsIo.cpp` (ADR-044, additive key `pointVisibility`). No new dependency.

## How it works

The database still holds EVERY point; a drawing's `st.surveyPoints` is now the subset its rules show.
`SyncProjectPoints` already diffs `st.surveyPoints` against what the tab last agreed with the database
(`pointsBaseWorld`); that base is now the VISIBLE subset, so a hidden point is never seen as "deleted".
A change to the rules (or to the database) makes the next frame rebuild the view (`Pull`).

Rule model (`projpts::Rules`): filters (number ranges, description wildcard, elevation range, point group,
source drawing) are ANDed; no filter = show everything (identical to P3). On top: `shown` (points created in
this drawing — clause 2) and `hidden` (hide here only — clause 4). Hidden beats shown beats the filters.

## Delivered

1. Rules type + filter + compact id-list text (pure, `[req377]` tests).
2. Rules per tab: `AppCommandState::pointVisibility`, saved/restored with the tab (`DrawingDocument`), written
   to / read from the DWG trailer (`pointVisibility`, absent when default).
3. Sync: view = visible subset; edits fold into the database by number; every number that newly appears in a
   drawing is pinned in `shown`; `createPointsNextId` skips numbers hidden here.
4. Toolspace **Survey Database** folder (project drawings only, beside Surfaces / Feature Lines): shown/total
   count, the five filters, Reset filters, **Hide selected here**, "Show them again".
5. Hidden points are simply absent from `st.surveyPoints`, so they are not drawn, snapped, selected, or used
   by surfaces (clause 5) with no per-feature code.

## Decisions / assumptions (recorded)

- ASSUMPTION: an empty filter set shows every point (not none). It keeps every P3 project opening unchanged
  and a brand-new drawing useful at once; "starts empty" for a NEW project drawing is already true because
  the database is empty. Users narrow a drawing with filters.
- ASSUMPTION: the source-drawing filter is a single drawing, chosen from the drawings that created points.
- ASSUMPTION: a number the drawing adds that the database already holds HIDDEN here is refused (the whole
  edit is undone in the view and reported, REQ-201) rather than overwriting another drawing's point. The
  overwrite / renumber / cancel prompt is the REQ-383 pass (P9); this is its safe "cancel" outcome.
- ASSUMPTION: rules are not part of the undo snapshot; changing them marks the drawing unsaved
  (`BumpCadGpuCache`) but is not an undo step.
- A same-data overwrite no longer logs "was overwritten" (it overwrote nothing) — needed because undo can
  legitimately re-show a point.

## Technical debt

- Rule edits are not undoable.
- A point group that is deleted leaves the group filter matching nothing (reported by the empty count).
- Per-keystroke rule edits rebuild the view; fine at a few thousand points, revisit at 100k.

## Verification

- `GoSurveyTests "[req377]"` (`tests/ProjectPointRulesTests.cpp`): each filter, combined filters, source
  filter, point group (and missing group = nothing), pinned/hidden precedence, bad number range, id text.
- `GoSurveySnapTests "[req377]"` (`tests/ProjectPointsSyncTests.cpp`): two drawings show different subsets;
  a created point is visible in its drawing whatever the filters; hide-here keeps the database and other
  drawings; editing/deleting shown points never touches hidden ones; hidden-number collision refused; next
  number skips hidden numbers; rules round-trip through the trailer; standalone drawing unaffected.
- Full ctest: 9 failures (offset / surfaces / solids / feature-line headless transcripts) — identical to the
  P3 baseline, none involve projects.
- GUI check of the toolspace section: see the PR (hover/clicks are not automatable here).
