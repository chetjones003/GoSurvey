# TASK-issue1000 — Create paper sheets from view frames (CREATESHEETS)

- Type:    feature
- Status:  in review (rework after user review of the first sheets; see §6). PR #1175 is not merged; merge needs the user's approval.
- Opened:  2026-11-07
- Reworked: 2026-11-08 (D-2026-11-08-f)
- Owner:   Claude (full-send on GitHub #1000)

## 1. Authority
- Goal:         GOAL-NN for Plan Production (issue #802 family), per spec/project.md
- Requirements: REQ-518 (accepted 2026-11-07, D-2026-11-07-c; items 7–11 added by D-2026-11-08-f); depends on REQ-517
                (view frames, #999, merged), REQ-037, REQ-488
- Constraints:  REQ-201 (refusals are logged), REQ-143 (disabled controls), read-only refusal before any change
- Owning subsystem: alignment (frames) + commands (paper layouts) + Qt dialog/ribbon

## 2. Scope (current)
- In scope:
  - `CREATESHEETS <alignment>[, <frames>[, <title block DWG>]]`, typed or from the dialog (the dialog sends `CREATESHEETS <alignment>[, <frames>]`).
  - One new paper layout per chosen frame, built on the title block DWG `Generic 24in x 36in Title Block.dwg` from the
    AutoCAD Template folder of the Civil 3D install. Its border and lines are copied onto the sheet.
  - Title block cells filled from the frame: alignment, sheet (frame name), scale, size, date, station range.
  - North arrow above the right-hand column, turned with the view roll.
  - One locked paper viewport per sheet, inside a 0.5 in margin inside the border, centred on the frame, scale = frame scale,
    rotated (camera roll) so the alignment runs left to right.
  - Naming `VF-n` with ` (2)` suffix on collision. One undo step for all layouts.
  - Refusals: read-only, no group, multiple groups, no frames, unknown frame, title block missing/unreadable/no border,
    frame sheet not ARCH D landscape (36 x 24 in).
  - Qt: Output → Plan Production → Create Sheets enabled; dialog builds the command text.
  - Tests in `tests/ViewFrameSheetTests.cpp` (`[req518]`).
  - Docs: resources/wiki (Alignments.md, Command-Reference.md, Commands/createsheets.md), external GitHub wiki.
- Out of scope:
  - Updating existing sheets when frames change (REQ-518.6).
  - Civil 3D Plan Production templates (not used; they depend on Civil 3D objects GoSurvey cannot draw).
  - Civil 3D comparison screenshots (waived by the user).

## 3. Architectural boundary check
- [x] No — proceed. Uses existing ViewFrame data, existing PaperLayout/Viewport (camRollDeg already persisted),
      existing Camera::RollToPlaceUp and ModelToPaperInThroughCamera, and the existing DWG import (ImportDwgFile).
      No new store, no new file format field.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| 1 | Rotate sheet viewports with the frame, or leave them unrotated (REQ-518.2) | 2026-11-07 | (a) rotate — user chose |
| 2 | Accept REQ-518 | 2026-11-07 | accepted with choice (a), via the implement request |
| 3 | Stay in the DWG as paper layouts? Margin? Title block? Fields and north arrow? Locked viewport? | 2026-11-08 | Layouts in the DWG (yes); inside a margin (0.5 in confirmed); Generic 24x36 title block DWG; fields and north arrow; real locked viewport |

## 5. Plan (current)
- Roll: `roll = Camera::RollToPlaceUp(0, 90, (-ty, tx, 0))` where (tx,ty) is the alignment tangent at the frame's
  centre station; this puts the tangent to screen-right. The north arrow uses the same roll.
- Title block: import the DWG with ImportDwgFile, take the largest four-vertex polyline as the border, copy its paper
  geometry and text onto the sheet, offset (dx, dy) so the title block is centred on the sheet.
- Viewport: inner box = border inset 0.5 in; paperX/Y/W/H = inner box; modelCenter = frame centre; scaleModelPerPaperIn =
  frame ground-per-inch; annotationScaleName = scale name; camRollDeg = roll; locked = true.
- Fields: rows at the middle of the title block's cells in the right-hand column (derived from its horizontal lines).
- Test approach: refusals and layout count/names/suffix; undo of all layouts in one step; title block copied; viewport
  inside the border margin; frame corners land inside the viewport; fields and north arrow present; PDF readback.

## 6. Rework log (2026-11-08)
- User reviewed the first sheets (layout-based) against Civil 3D screenshots and rejected them. Direction: keep layouts in
  the DWG, add the title block from the Generic 24x36 DWG, fill fields, add north arrow, real locked viewport, white paper.
- Implemented in src/commands/CadCommands_Alignment.cpp; tests rewritten in tests/ViewFrameSheetTests.cpp.
- Qt dialog and command-table help updated to `CREATESHEETS <alignment>[, <frames>]`; template combo removed.
- Wiki (repo) and spec (REQ-518 items 1, 2, 4, 4a, 10 and acceptance; D-2026-11-08-f) updated to the new design.
- Sol-loop review (3 rounds, earlier design) is out of date for the rework; a fresh review is needed before merge.

## 7. Known limits / notes
- The title block path depends on the Civil 3D install under LOCALAPPDATA; a missing file refuses the command.
- Sheets are ARCH D only (title block size).
- The plan area is empty in the test artifact (no model geometry); this is expected for the test.
- GUI layout tabs could not be driven by automation in this environment; the visual check is the artifact PNGs and tests.
