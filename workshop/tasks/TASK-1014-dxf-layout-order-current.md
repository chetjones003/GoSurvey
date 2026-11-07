# TASK-1014 — DXF layout tab order and current layout (issue #1014, audit #751 I9)

- Type: feature
- Status: in progress
- Opened: 2026-11-07
- GitHub issue: #1014 (umbrella: #751)

## 1. Authority

- Requirement: REQ-113 (accepted), new acceptance (15).
- Decision: D-2026-11-02-d (recorded in `spec/project.md`; the issue carried the decision text, the earlier
  branch that held the spec edit never reached beta).
- Constraints: no new abstraction, no new dependency; DXF only.

## 2. Plan

- Export (`ExportDxfFile_Impl`): write HEADER `$CTAB` = active layout name, or `Model`. Tab order was already
  written as LAYOUT group 71 = layout index + 1.
- Export (LibreDWG path, used when a viewport needs native state): LibreDWG has no `$CTAB`, so
  `SetExportedDxfCurrentTab` inserts it into the written file before `$CLAYER`.
- Import (`ImportDxfFile_Impl`): layouts were already sorted by group 71; `ActivateDxfCurrentTab` reads `$CTAB` and
  sets `activeSpaceIndex`/`lastPaperLayoutIndex`, with Model space when missing, `Model` or unknown. It always
  assigns, so a stale index from the previous drawing cannot survive.

## 3. Files

`src/io/DxfIo.cpp`, `src/io/LibreDwgCad.cpp`, `tests/LibreDwgCadTests.cpp`, `spec/requirements.md`,
`spec/project.md`, `resources/wiki/Paper-Space-and-Layouts.md`.

## 4. Architectural-boundary check

IO layer only; reads/writes existing `AppCommandState` fields. No UI, no new state.

## 5. Tests

`[issue1014]` in `LibreDwgCadTests.cpp`: three-layout round trip with layout 1 active, Model active; LibreDWG path
with a viewport; hand-written DXF with out-of-order tab values and an unknown `$CTAB`.

## 6. Documentation review (REQ-453)

- `README.md`, `PRIVACY.md`: reviewed, no update needed.
- In-app wiki `resources/wiki/Paper-Space-and-Layouts.md`: updated.
- Public wiki (GitHub Wiki Paper Space and Layouts): see PR description.
