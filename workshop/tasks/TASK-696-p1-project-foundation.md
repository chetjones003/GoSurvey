# TASK — Projects P1: project foundation (issue #696)

- Branch: `feat/projects-p1-foundation`
- Authority: **REQ-373**, **REQ-374**, **REQ-382**; D-2026-10-05-d; GitHub #696 (P1)
- Boundary check: pure format / join / lock code in `src/io/Project.*` (no UI, no app state); glue
  in `src/ui/CadUi_Projects.cpp`; state on `AppCommandState` (`openProjects`, `projectPrompt`,
  `DrawingTab::projectUid`). No new dependency (REQ-300). No new abstraction: the recent-projects
  list **reuses** `recent::*` against a second JSON file (second concrete use).

## Delivered

1. `.gsproj` format (id, name, layout, settings, tracked items, unknown fields kept), atomic save,
   relative-path safety — `src/io/Project.{hpp,cpp}`.
2. New Project dialog (name + location) and Open Project (File menu + Start screen); both land on an
   empty drawing tab in the project.
3. Auto-detect and join when a DWG is opened (first `.gsproj` walking up; damaged → "Open standalone /
   Cancel"; none → standalone, unchanged). "Opened in project X" notice in the command log.
4. Several projects open at once; each tab belongs to one (`DrawingTab::projectUid`); project name on the
   tab label and the Toolspace header; a project with no tabs left is closed and its lock released.
5. Lock file `<Name>.gsproj.lock`: first opener edits; others get Open read-only / Take over / Cancel.
   Read-only refuses Save / Save As for its drawings.
6. Start screen: New Project / Open Project buttons, Recent Projects above Recent Drawings, each
   recent drawing tagged with its project or "Standalone".

## Decisions made here (recorded in REQ-382 revisions)

- **Stale lock** = the holder is on this machine and its process no longer exists, or the lock file is
  unreadable. A lock from another machine can never be judged stale; the user may still take it over
  after the warning.
- **Open Project** with no drawing lands on a new empty drawing tab in that project (a session lives
  only while it has a tab).

## Deferred to later phases (not guessed)

- New Project dialog's coordinate system / units fields → **P2** (REQ-375 owns where they are stored).
- "Start from an existing drawing" → **P5**.
- Recent Projects thumbnail: a folder glyph is shown; there is no defined picture to capture for a
  project. Raise as a question if a real thumbnail is wanted.
- File > New inside a project does not yet inherit the project; Save for a project drawing does not yet
  default to `Drawings/` → P2.
- Read-only mode also guards the database / project files in P3+.

## Tests

- `GoSurveyTests "[req373],[req374],[req382]"` (`tests/ProjectTests.cpp`): create/marker/folders,
  round trip with unknown field + unknown kind, path safety, atomic write, join (nested, outside,
  damaged, doubled marker), lock (first wins, release only own, stale rule, takeover), recent reuse.
- GUI paths (dialogs, Start screen) are not unit-testable; checked by running the app.
