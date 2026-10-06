# TASK — Projects P8: Turnovers (issue #696)

- Branch: `feat/projects-p8-turnovers`
- Authority: **REQ-381** (+ REQ-379 Project Health gate, REQ-373 tracked items); D-2026-10-05-d,
  **D-2026-10-05-j** (a turnover is a record, not a bundle — the user chose "record only"); GitHub #696 (P8)
- Boundary check: the record rules are pure in `src/io/ProjectTurnover.{hpp,cpp}` (`<filesystem>`, json,
  miniz's CRC — no window, no `AppCommandState`); the session glue (`CreateProjectTurnover`) is in the
  existing `src/commands/ProjectFiles`; the window, menu entry and Project Files button are in
  `CadUi_Projects.cpp` / `CadUi.cpp` / `CadUi_Toolspace.cpp`; the `TURNOVER` command is in `CadCommands.cpp`.
  No new dependency (miniz is already vendored for P7).

## Delivered

1. **Create Turnover.** File > Create Turnover..., the Project Files section button, or `TURNOVER`. The
   window shows Project Health first (with **Copy links into the project**; problems must be accepted),
   then a Recipient box and a tick-list of the project's tracked files (all ticked). **Create turnover**
   writes `Turnovers/<date>_<recipient>.gsturnover`: project ID/name, recipient, UTC date and, per chosen
   file, path, kind, size and CRC-32 (a file not on disk is recorded as missing). The record is tracked
   in the `.gsproj` (so Project Files lists it; hovering shows recipient, date, file count) and is never
   offered as content of a later turnover. Refused (nothing written): blank recipient, nothing chosen,
   unknown file, unaccepted Health problems, read-only project.

## Decisions / assumptions (recorded)

- **D-2026-10-05-j**: record only; no bundle.
- ASSUMPTION: the fingerprint is CRC-32 (already in the vendored miniz); it tells versions apart but is not
  tamper-proof. The SPEC says only "contents".
- ASSUMPTION: the date is UTC, taken when the record is created.

## Technical debt

- Fingerprinting a very large point cloud blocks the UI thread while the record is written (like Pack).
- The window itself is a GUI path (checked by building the app); the rules underneath are unit-tested.

## Tests

- `GoSurveyTests "[req381]"` (`tests/ProjectTurnoverTests.cpp`): exact contents/date/recipient + read back +
  tracked; fingerprint changes with content; refused until Health accepted; blank recipient / empty choice /
  unknown or escaping path refused; missing file recorded; duplicates once; same-day names distinct, newest
  first, records not candidates; damaged record skipped; UTC date text.
