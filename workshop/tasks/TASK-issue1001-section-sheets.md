# TASK-issue1001 — Sample lines and section sheets (CREATESAMPLELINES, CREATESECTIONSHEETS)

- Type:    feature
- Status:  in review (PR not yet opened at time of writing; see §6)
- Opened:  2026-11-08
- Owner:   Claude (full-send on GitHub #1001)

## 1. Authority
- Goal:         Plan Production for issue #802 family (Civil 3D design objects), per spec/project.md
- Requirements: REQ-519 (accepted 2026-11-08, D-2026-11-08-h; items 7–14 added by that decision)
- Depends on:   REQ-480..503 alignments (#121, closed 2026-11-06), REQ-497 ground elevation, REQ-068 TIN surfaces,
                REQ-037 native paper geometry, REQ-517/518 (#999/#1000, merged), REQ-201 (refusals are logged)
- Constraints:  read-only project refuses before any change; one undo step per command; REQ-143 (no greyed controls
                left on the ribbon for these two buttons)
- Owning subsystem: alignment (sample lines, section sampling, scales) + commands (paper layouts, typed commands) +
                io (persistence) + render/Qt (plan drawing, dialogs, ribbon)

## 2. Scope (as built)
- `CREATESAMPLELINES <alignment>, <start>, <end>, <interval>, <left>, <right>[, <surface>]` and the Home → Profile &
  Section Views → Sample Lines button (dialog). One line per interval station, named by its displayed station.
- Section sampling against one TIN surface at creation (optional surface argument; dialog: None by default). Sample points
  are the origin, the swath ends inside coverage, and every crossing of a triangle edge or vertex (exact for a TIN).
- `CREATESECTIONSHEETS <alignment>, <sample lines>, <sections per sheet>, <axis elevation>` and the Output → Plan
  Production → Create Section Sheets button (dialog). ARCH D layouts `SEC-n` on the Generic 24in x 36in title block
  (the same loader as CREATESHEETS, refactored into `LoadSheetTitleBlock`). Section drawn as paper geometry.
- Persistence: sample lines (plan geometry + section points) saved in the drawing's JSON; cleared with the other
  alignment stores on template save; dropped when their alignment is deleted (`RefreshViewFrameGroups` calls
  `SyncSampleLines`).
- Plan drawing: line swath in GL view and station label in the Qt overlay, layer 0 visibility as view frames.
- Out of scope (recorded as not built): title-block fields on section sheets; Civil 3D section-view styles.

## 3. Decisions (see D-2026-11-08-h in spec/project.md)
- Acceptance: the user's request to implement #1001 (same precedent as REQ-518, D-2026-11-07-c).
- Open choices decided (reversible): typed commands take every value; dialogs prefill 50 ft interval, 50/50 ft swaths,
  4 sections per sheet, axis elevation = lowest ground in the chosen lines rounded down to 10 ft; surface chosen at line
  creation (spec names no separate surface-sampling command); sheet scales are the finest 1-2-5 step at which each
  sheet's sections fit; sheets named `SEC-n`.
- Civil 3D comparison panel waived by the user ("skip civil 3d parody screenshots"), recorded as waived.

## 4. Files
- New: src/alignment/AlignmentSampleLines.{hpp,cpp}; src/qt/EditorSampleLinesDialog.{hpp,cpp};
  src/qt/EditorSectionSheetsDialog.{hpp,cpp}; tests/AlignmentSampleLineTests.cpp; tests/SectionSheetTests.cpp;
  resources/wiki/Commands/createsamplelines.md, createsectionsheets.md (and the same two pages in the public wiki).
- Changed: CadCommands.{hpp,cpp} (state members, dispatch, help text, declarations); CadCommands_Alignment.cpp
  (commands, shared title-block helper, refresh sync); GsIo.cpp (save/load, template clear list); ViewportRenderer.cpp,
  QtAnnotations.cpp, EditorViewport.cpp (plan drawing); EditorWindow.cpp (verbs); RibbonCatalog.hpp (two buttons);
  CMakeLists.txt, cmake/GoSurveyQt.cmake; resources/wiki/Alignments.md, Command-Reference.md; spec/requirements.md,
  spec/project.md.

## 5. Tests and verification
- Domain (tests/AlignmentSampleLineTests.cpp): REQ-519 acceptance (11 lines, names 0+00..5+00); refusals; flat surface
  gives 100; rising surface 2.0 ft above the origin at 20 ft via a triangle edge at 20 ft; wholly-inside-triangle section;
  partial coverage; no coverage; no-triangle TIN; nice scale series; sync drops orphan lines.
- Commands (tests/SectionSheetTests.cpp): 11-line run with surface; no-surface lines; refusals; one undo; alignment
  delete; read-only; non-TIN refusal; save/reopen; sheets (3 lines, 2 per sheet → SEC-1, SEC-2 in station order; one undo);
  sheet refusals. Title-block tests skip when the Generic title block is not installed.
- Ribbon: both buttons wired (RibbonCatalog placeholder flags cleared).
- Build: `.\build.bat` (Release) — see §6 for the result.

## 6. Status log
- 2026-11-08: spec accepted (REQ-519, D-2026-11-08-h); design and implementation written; baseline build before the
  feature passed (543 steps); feature build and test run pending at the time of writing.

## 7. Council review and technical debt
- Round 1 (three Claude Haiku reviewers, read-only): scores 5, 6, 7. Blocking items fixed: boundary crossing ground,
  DXF/DWG import leaving stale sample lines, names repeated by a fine interval, dialog defaults, empty sample-lines field,
  spec item 4 wording. One claimed blocker (Delete key leaving lines) was checked and was false: the alignment erase path
  already syncs lines through RefreshViewFrameGroups; a regression test covers it.
- Round 2 (fresh reviewers): scores 8, 7, 6. Fixed: the plan kept drawing a deleted alignment until the next redraw
  (GPU cache bump on the erase paths); bench scenes now sync sample lines; the dialog no longer turns an empty field into
  ALL; the end station prefill rounds down so it stays inside the alignment; the scale fit is a pure function with tests
  that run without the title block; the scale text is printed with a fixed double; spec item 4 keeps the two real surface
  refusals.
- Round 3 (fresh reviewers, verifying round-2 fixes): scores 7, 7, 8; no blocking findings from reviewer 3. Reviewers 1 and 2
  found one blocking defect: a BENCH run dropped the user's sample lines. Fixed by saving and restoring them with the
  alignments (CadCommands_Bench.cpp). That fix and two spec and dialog wording corrections were made after round 3 and were
  checked by the build and the full suite, not by a fourth council round.
- Known edge, not fixed: a TIN literally named "None" cannot be chosen in the Sample Lines dialog, because the command
  reads NONE as no surface.
- Technical debt, not fixed in this change:
  - SampleSection tests every triangle edge for every line on the UI thread. A large TIN times 2000 lines is slow. Needs a
    spatial index (the TIN index is private to TinSurfaceQuery).
  - A hole in the TIN draws a straight ground line across the void. The spec is silent on gaps.
  - The sheet-layout test needs the installed Generic 24in x 36in title block and skips otherwise (the repo's convention
    for ViewFrameSheetTests too).
  - Pre-existing, REQ-517: ClearCadGeometry and the block load do not clear view frame groups (only sample lines were
    fixed here).
  - Dialogs (Qt) have no automated tests beyond the ribbon wiring; the Civil 3D panel was waived by the user.
