# TASK — Projects P7: Pack Project / Open Packed Project (issue #696)

- Branch: `feat/projects-p7-pack`
- Authority: **REQ-380** (+ REQ-379 Health gate, REQ-382 lock excluded, REQ-300 dependency rule);
  D-2026-10-05-d, **D-2026-10-05-i** (container = standard ZIP via vendored miniz — the user chose this
  from three options), **ADR-066**; GitHub #696 (P7)
- Boundary check: the pack/unpack rules are pure in `src/io/ProjectPack.{hpp,cpp}` (`<filesystem>`, json,
  miniz — no window, no `AppCommandState`); the left-out-file helpers + the Health `omitted` list are in the
  existing pure `src/io/ProjectFiles`; the open-time wording is in `src/commands/ProjectFiles.cpp`; the
  Pack window, Open Packed Project and menu/Start-screen entries are in `CadUi_Projects.cpp` / `CadUi.cpp` /
  `CadUi_StartScreen.cpp`; two Windows file dialogs in `WinFileDialogs`. New dependency: miniz, vendored
  under `third_party/miniz/` with `VENDORED.md`.

## Delivered

1. **Pack Project (clauses 1-2).** File > Pack Project… / `PACKPROJECT`. The window shows Project Health
   first (linked / missing / unavailable / unsaved) with **Copy links into the project**; any problem needs
   "Pack anyway". It then shows the file count and size before compression, the point clouds' share, a
   "leave point clouds out" box, and a warning line at 25 MB or more. **Pack…** asks where to save, then
   writes a `.gspack` (a ZIP): every file in the project folder except the lock file and `*.tmp`, plus a
   `gspack.json` manifest (format version, project ID, name, date, left-out files, each file's exact
   modified time). Written to `<name>.tmp` and renamed. The project's point database is flushed first.
2. **Open Packed Project (clauses 3-4).** File menu, Start screen button, `OPENPACK`. Pick a `.gspack`, pick a
   folder (must be empty or new). Every entry is checked **before** anything is written (safe relative name,
   no backslash / drive / `..` / `.` / empty / trailing-dot segment, no duplicate even by case, one manifest,
   exactly one `.gsproj`, marker ID = manifest ID, format version not newer). Files stream out with their
   checksums verified; exact modified times are restored (a `.gscloud` cache is matched to its cloud by exact
   time, ADR-060). Any failure removes everything written and (if we made it) the folder. Then the project
   opens through the normal Open Project path.
3. **Left-out point clouds.** Recorded in the opened `.gsproj` as `packOmitted`. Project Health lists them as
   "Unavailable (left out…)" (information; `Clean()` ignores them) and opening a drawing says "unavailable"
   instead of "missing". Once the file is on disk again it is no longer unavailable.

## Decisions / assumptions (recorded)

- Container: **D-2026-10-05-i / ADR-066** (user chose "vendor miniz, standard .zip").
- ASSUMPTION: the extra size warning is at **25 MB** (a common email limit); the SPEC says only "shows the
  total size". The shown total is before compression.
- ASSUMPTION: "Pack anyway" is allowed with Health problems (a linked or missing file is simply not in the
  pack and is reported when the pack is opened); the SPEC says Health runs first but not that it must be clean.
- ASSUMPTION: the destination folder for Open Packed Project must be empty or new; the SPEC says "a folder the
  user picks" and a non-empty one would risk overwriting.
- A `.gscloud` cache IS packed (so a cloud opens at full detail with no rebuild); it is left out with its cloud
  when point clouds are excluded.

## Technical debt

- Packing / unpacking a very large project blocks the UI thread while it runs (no progress bar), like the
  P6 copy.
- The destination free-space check uses the declared uncompressed sizes of the pack's entries.
- A pack has no signature: the checks stop unsafe paths and damage, not a deliberately re-built pack.
- The Pack / Open dialogs are GUI paths (file dialogs cannot be driven headless), checked by building the
  app, not by unit tests; the rules underneath are tested.

## Tests

- `GoSurveyTests "[req380]"` (`tests/ProjectPackTests.cpp`, 9 cases): round trip (same bytes, project ID,
  exact times, PDF placements, Health clean); lock / temp / the pack itself never packed; size plan; exclusion
  → unavailable not missing, and back once the file returns; eight unsafe entry names rejected; not-a-zip, no
  manifest, no marker, ID mismatch, duplicate name, newer format rejected; non-empty destination refused and
  untouched; failed checksum leaves nothing (new and pre-existing empty folder); unwritable target leaves no
  half file.
- `GoSurveySnapTests "[req380]"` (`tests/ProjectFilesFlowTests.cpp`): a left-out cloud opens as unavailable.
