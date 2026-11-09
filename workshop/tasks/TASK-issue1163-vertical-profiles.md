# TASK-issue1163 — Vertical profiles along an alignment

**Requirement authority:** REQ-521 (accepted 2026-11-08, D-2026-11-08-k). Builds on REQ-480, REQ-483, REQ-484, REQ-500.
Does not change REQ-145 (`QUICKPROFILE`) or REQ-513 (`CREATEPROFILEFROMSURFACE`).

**Constraints:** CON-NN not applicable beyond the project rules; no new dependencies; geometry stays XY-only in alignments.

## Files and subsystems

| Area | Files |
| --- | --- |
| Domain (pure, no GUI) | `src/alignment/AlignmentProfile.hpp`, `src/alignment/AlignmentProfile.cpp` (new) |
| Drawing state | `src/commands/CadCommands.hpp` (`DrawingGeometrySnapshot`, `DrawingDocument`, `AppCommandState`, command declarations, `PushUndoSnapshot` users) |
| Copy points | `src/commands/CadCommands.cpp` (cmd↔doc copy, undo snapshot and restore, new-drawing clear, command dispatch) |
| Commands | `src/commands/CadCommands_Alignment.cpp` (six runners) |
| Persistence | `src/io/GsIo.cpp` (save and load next to `sampleLines`) |
| Tests | `tests/AlignmentProfileTests.cpp` (new), registered in `CMakeLists.txt` |
| Docs | `resources/wiki/Alignments.md`, `resources/wiki/Command-Reference.md`, `resources/wiki/Commands/*.md` (new pages) |

## Implementation approach

- A `VerticalProfile` holds PVIs (station, elevation, curve length). The domain module computes elevation, grade and
  profile point at a station, validates geometry, and derives a 3D design point from the alignment and profile.
- Commands follow `RunCreateSampleLines` (read-only guard, `Resolve`, `ParseSta`, `PushUndoSnapshot`, `BumpCadGpuCache`).
- Save and load follow the `sampleLines` block. Alignment deletion keeps profiles (flagged, not dropped).

## Architectural-boundary check

- Domain code has no dependency on Qt, GsIo or commands (testable headless), like `AlignmentSampleLines`.
- Profiles hold alignment ids only; alignment geometry is unchanged (no Z in alignment entities).

## Test approach

- Domain: curve and tangent elevations and grades; curve mid-point offset; design path; each validity refusal; outside
  range; one-PVI and empty profiles.
- Commands: each verb's happy path and refusal; one-step undo; read-only refusal; alignment deletion keeps the
  profile and PROFILELIST reports it; duplicate names.
- Persistence: serialize and reload, then query results equal.

## Verification

Build (Release, `build.bat`), run `ctest --preset ninja-release -R "Profile|Alignment|SectionSheet"`, run the full suite,
then the council review (three Claude Haiku reviewers, iterated).

## Documentation review (REQ-453, before the PR opens)

| Source | Result |
| --- | --- |
| `README.md` | Reviewed. No update needed: it describes alignments in general ("define road and utility alignments with stationing"), which stays true. Profiles add no new top-level capability to that summary. |
| `PRIVACY.md` | Reviewed. No update needed: profiles are drawing geometry saved in the drawing. Nothing is collected or sent. |
| In-app help wiki (`resources/wiki/`) | Updated. `Alignments.md` gains a "Design profiles (vertical)" section; `Command-Reference.md` gains twelve rows (six in each of its two tables); six new `Commands/*.md` pages. |
| Public user manual (GitHub wiki, `https://github.com/chetjones003/GoSurvey/wiki`) | Updated with the same six pages, index rows and section, and pushed before the PR was opened. Pre-existing gap, not changed here: the public `Alignments.md` and `Command-Reference.md` still show the `CREATEVIEWFRAMES` `<scale>` text without the `AUTO` option added under D-2026-11-08-h. |

## Council review (three Claude Haiku reviewers, read-only; geometry, commands/persistence, spec/tests/docs)

- Round 1: scores 9, 8, 8. Fixed: template save strips `designProfiles`; BEDIT clears profiles; six `kRegistry` rows added;
  exact profile range (no tolerance); overlap tolerance 1e-9; tangent too steep to represent is refused; non-finite results
  become "no value"; garbled wiki sentence and alignment-missing scope; tests for read-only, undo, overlap via command, no key
  when empty.
- Round 2: scores 8, 8, 8. Fixed: remaining `%.3f` station formats (now `%.6f`); message buffers widened; "no geometry" checked
  before the NaN station; load path type-guarded so a wrong-typed field cannot throw and leave a half-loaded drawing; a profile
  with a malformed stored point is not loaded and the load log names it (REQ-521.8, decision D-2026-11-08-k); tests for
  exact undo and the exact-touch boundary; the vacuous `alignment missing` assertion replaced.
- Round 3: scores 9, 9, 8. Fixed: touch-point test asserts 107.5 (continuity); load now rejects a profile with no name or a
  repeated name (capitals ignored) and names a missing points array correctly; SPEC wording of PROFILEQUERY, PROFILELIST and the
  K sentence aligned with the code.
- Round 4: scores 8, 8, 7. Fixed: a query past the profile's end reports "outside the profile" (the profile answers first);
  a stored name is trimmed, and a blank name is not loaded; a point must be exactly three finite numbers; an entry that is not
  an object is logged. New tests cover each.
- Round 5: scores 9, 9, 8. Fixed: the exact elevation at a tangent's far PVI; the SPEC REQ-521.3 finite-grade rule now matches
  the code; tests for PROFILEDELETE with a missing alignment and a typed `nan`.
- Round 6: scores 8, 8, 8. Fixed: a station span that overflows is refused (the grade would have read as zero); PROFILEDELPVI
  removes a PVI left beyond a shortened alignment; a skipped entry is named by its position; the SPEC records the
  station-equation gap; message buffers widened to 1024 bytes (the 1e308 case truncated the reason).
- Round 7: scores 9, 8, 8. No blocking defect. Fixed after scoring: the station-equation gap is stated in SPEC item 6 for
  PROFILEQUERY, PROFILEPVI and PROFILEDELPVI; the profilelist page describes "geometry not valid" precisely; the task file row
  count is corrected.

## Verification

- `build.bat` (Release): succeeds.
- `GoSurveyTests "[req521]"`: 41 test cases, 185 assertions, all pass.
- Full `ctest --preset ninja-release`: 100% passed, 0 failed out of 194 (log: `build/artifacts/ctest-final5-req521.log`).
  The one comment-only edit after that build (a note in `CadCommands_Alignment.cpp`) changes no behaviour.
- A full single-process `GoSurveyTests.exe` run was not used as evidence: it is sharded by ctest, and its single-process
  output included four failures that came from running the binary outside the repository root (fitting-library paths).
  Those four pass from the repository root and pass in the ctest run.

## Public wiki

- Pushed to `chetjones003/GoSurvey.wiki` as commit `c738dce` (before the PR was opened, per REQ-453).

## PR #1179 review (PR-Agent, one review at the first push)

1. A curve between very large grades overflowed in the grade difference (`g2 - g1`), so a valid midpoint returned no value. Fixed: the curve is written without any difference of grades (`E = Ep + (L/2)(g2 t^2 - g1 (1-t)^2)`, `G = g1(1-t) + g2 t`), and the position `t` is clamped to [0, 1]. Test: `a curve between grades so large that their difference overflows still gives its value`.
2. The curve-zone checks used a 1e-6 tolerance, so a station just before a tiny curve was evaluated on the parabola. Fixed: the zone checks are exact (the curve meets its tangent at both ends, so no tolerance is needed). Test: `a station just before a tiny curve follows the tangent, not the parabola`.
