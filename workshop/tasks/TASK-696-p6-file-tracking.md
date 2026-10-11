# TASK — Projects P6: file tracking (issue #696)

- Branch: `feat/projects-p6-file-tracking`
- Authority: **REQ-379** (+ REQ-373 clauses 4-6 for the tracked-item format); D-2026-10-05-d,
  **D-2026-10-05-h** (PDF placements live in the project file — answered by the user, option 1);
  GitHub #696 (P6)
- Boundary check: the file rules are pure in `src/io/ProjectFiles.{hpp,cpp}` (`<filesystem>` + json, like
  `Project.cpp`); the drawing-level glue that reads/writes `AppCommandState` is
  `src/commands/ProjectFiles.{hpp,cpp}` (domain, no UI); the modal, health window, menu item and Toolspace
  section are in `CadUi_Projects.cpp` / `CadUi_Toolspace.cpp` / `CadUi.cpp`. No new dependency (REQ-300).

## Delivered

1. **Attach (clause 1).** In a writable project drawing, `POINTCLOUDATTACH` and the PDFATTACH dialog's
   Attach button ask Copy / Link / Cancel for a file from outside the project (`RequestProjectAttach` →
   modal → `ResolveProjectAttach`). Copy is the default: the file goes to `PointClouds/` or `PDFs/`
   (an identical earlier copy is reused, a different file of the same name gets `name (2)`), keeping its
   modification time and a point cloud's `.gscloud` cache. Link leaves it and says it will not travel.
   The prompt shows the size and warns at 100 MB or more. A standalone drawing, a read-only project and a
   file already inside the project never see the prompt.
2. **Associations + placements (clause 2).** When a project drawing is saved inside the project
   (all four save paths), `SyncProjectFilesOnSave` writes what it holds into the `.gsproj`: the drawing,
   its point clouds and PDFs as tracked items (project-relative, or `local-link`), the drawing as an
   association, and each PDF's placement (page, world position, scale, rotation, dpi, options). A file the
   drawing no longer holds loses its association. On open (`ApplyProjectFilesOnOpen`) each point cloud is
   pointed at the file the project tracks for it (so another machine's path still works) and each PDF is
   re-placed; anything missing is reported and the drawing still opens.
3. **Project Files section** in the Toolspace: items by folder, `[link]` badge, `(missing)` flag, a tooltip
   with the drawings each is attached to, and a Project Health button.
4. **Project Health**: `PROJECTHEALTH` command + File > Project Health… + the Toolspace button. Lists linked,
   missing, unreachable (unknown kind) files and open drawings with unsaved changes; "Copy links into the
   project" converts links to `in-project` and saves the `.gsproj`. `ProjectHealthFor` is what Pack Project
   (P7) and turnovers (P8) will call as their gate.

## Decisions / assumptions (recorded)

- PDF placements in the project file: **D-2026-10-05-h** (spec + decision row added in this PR).
- ASSUMPTION: the "size prompt" is the Copy/Link modal itself, which always shows the size; 100 MB is where
  an extra warning line appears. The SPEC gives no number.
- ASSUMPTION: the association is written when the drawing is saved (an unsaved drawing has no
  project-relative name). A copy made at attach time but never saved is a file in the project that no
  drawing references yet.
- ASSUMPTION: a drawing saved outside the project folder records nothing.

## Technical debt

- Copying a very large point cloud blocks the UI thread while it runs (no progress bar).
- PDFs are re-rasterized synchronously on open; a heavy PDF delays opening that tab.
- After "Copy links into the project" an already-open tab still points at the old link until reopened; the
  save-time sync recognises the copied file by name and does not bring the link back.
- Add Drawing to Project (P5) copies a DWG whose point clouds keep their old absolute paths; they are
  resolved by the project only once that copy is saved from GoSurvey.
- PDF re-placement and the modal / window layout are GUI paths: headless `PdfAttach_Build` always fails, so
  they are checked by building the app, not by unit tests.

## Tests

- `GoSurveyTests "[req379]"` (`tests/ProjectFilesTests.cpp`, 7 cases): copy default (+ cache + mtime, source
  untouched); in-project / same-name / reuse; size threshold; save records relative paths, flags links, keeps
  or drops associations and placements; placements round-trip the `.gsproj`; a moved project finds its cloud;
  Health lists every problem class and Copy Links In converts a link and reports a missing one.
- `GoSurveySnapTests "[req379]"` (`tests/ProjectFilesFlowTests.cpp`, 7 cases): prompt only when it should be;
  Copy / Link answers; a project that went read-only attaches nothing; save → open round trip on a changed
  local origin; missing files reported; standalone / read-only / outside-folder record nothing; Health unsaved
  count and Copy Links In persistence.
