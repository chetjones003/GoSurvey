# TASK-736 — Traverse Editor: save and reload traverse data

GitHub issue: #834
Requirement: REQ-403 (spec/requirements.md)
Decision: D-2026-10-12-a (spec/project.md)

## Requirement authority

REQ-403 — standalone `.gstrav` JSON file, auto-tracked by a GoSurvey Project
(#696 P6 mechanism, `projfiles::TrackFile`) when the active drawing belongs to
one; untracked for a standalone drawing. Computed/derived fields are not
serialized — they are recomputed from the saved inputs on load.

## Files / subsystems affected

- `src/traverse/TraverseIo.hpp` / `.cpp` (new) — pure JSON serialize/deserialize
  of `TraverseData` + `LsaWeights`. No UI, no filesystem dialog — just a path in,
  a path out (ADR-002 precedent: domain stays headless-testable).
- `src/platform/WinFileDialogs.hpp` / `.cpp` — `BrowseOpenFileGstravUtf8` /
  `BrowseSaveFileGstravUtf8`, mirroring the existing `*Gspack*` pair exactly
  (Win32 `OPENFILENAMEW` + non-Windows stub).
- `src/ui/CadUi_TraverseEditor.cpp` — Save / Save As / Load buttons next to
  "Import .fbk…"; project auto-tracking after a successful save inside a
  project folder.
- `src/commands/CadCommands.hpp` — one new field (`traverseFilePath`) to
  remember the last-used path for plain "Save".
- `CMakeLists.txt` — register `TraverseIo.cpp` in both the app target and the
  headless test target (beside `TraverseCalc.cpp`/`FbkImport.cpp`).
- `tests/TraverseTests.cpp` — round-trip tests.

## Existing code to reuse

- `projfiles::TrackFile` / `AppCommandState::openProjects` / `drawingTabs[].projectUid`
  (`src/commands/ProjectFiles.hpp`, `src/io/ProjectFiles.hpp`) — the exact
  mechanism PDFs/point clouds use to become a tracked project file. No new
  `Role` needed: a `.gstrav` file falls under the existing `Role::PointFile`
  default branch of `RoleForFile`.
- `nlohmann::json` — already the project's persistence library
  (`ProjectFiles.cpp`, `ProjectSettings.cpp`, `Project.cpp`).
- `BrowseOpenFileGspackUtf8`/`BrowseSaveFileGspackUtf8` as the structural
  template for the new `.gstrav` dialog pair.
- `FbkImport`'s call-site shape in `CadUi_TraverseEditor.cpp` (replace
  `cmd.traverseData`, set `traverseDataDirty`, reset `traverseExpandedLeg`,
  push a log line) as the template for Load.

## Implementation approach

1. `TraverseIo::Save(path, td, weights, err)` writes one JSON object:
   `{"format":"gstrav","version":1, start fields, legs:[...], options,
   weights:{...}}`. `TraverseIo::Load(path, td, weights, err)` reads it back,
   validates `format`/`version`, and populates both out-params. Unknown/missing
   fields fall back to the struct's existing defaults (forward-compatible).
2. Wire Save/Save As/Load buttons in the editor. After a successful save, if
   the active tab's `projectUid != 0` and the saved path resolves inside that
   project's folder, call `TrackProjectFile` (same call `AddPdfToProject`
   makes) so the file joins the project's tracked list and travels with
   Pack/Turnover. Log every outcome (REQ-201).
3. Load calls `TraverseIo::Load`, and on success replaces `cmd.traverseData`
   and `cmd.traverseLsaWeights`, marks `traverseDataDirty = true` (so
   `ComputeTraverse` reruns and the closure shown matches what was saved),
   resets `traverseLsaComputed`/`traverseExpandedLeg`.

## Test approach

- Pure round-trip tests in `tests/TraverseTests.cpp` (no UI/GL, ADR-002): build
  a `TraverseData` with legs, raw observation sets, non-default `LsaWeights`,
  both open and closed-loop variants; save to a temp path; load back; assert
  every input field matches and `ComputeTraverse` on the reloaded data
  reproduces the original closure.
- A malformed-file test (truncated/garbage JSON) asserts `Load` fails with a
  message and leaves the passed-in `TraverseData` unchanged.
- Project-tracking is manual (GUI + a real `.gsproj`), same category as the
  existing PDF/point-cloud tracking tests — no headless project-session
  harness exists for this path yet.

## Architectural-boundary check

No new `gsproj` schema field, no new `Role` enumerator, no change to
`.gsproj`'s tracked-item shape — `.gstrav` is just another file `RoleForFile`
already classifies as `Role::PointFile`. Domain (`TraverseIo`) stays free of
`AppCommandState`/ImGui; only the UI layer knows about projects and dialogs,
matching the existing PDF/point-cloud split between `io/ProjectFiles` (pure)
and `commands/ProjectFiles` (glue).
