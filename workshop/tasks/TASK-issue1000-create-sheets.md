# TASK-issue1000 — Create paper sheets from view frames (CREATESHEETS)

- Type:    feature
- Status:  submitted (sol-loop: 3 rounds, Claude Haiku 5.5 as the single reviewer; round 3 findings S9/S10 fixed in tests and verified, not re-reviewed; see PR)
- Opened:  2026-11-07
- Owner:   Claude (full-send on GitHub #1000)

## 1. Authority
- Goal:         GOAL-NN for Plan Production (issue #802 family), per spec/project.md
- Requirements: REQ-518 (accepted 2026-11-07, D-2026-11-07-c); depends on REQ-517 (view frames, #999, merged),
                REQ-489 (layout copy, #1015 closed), REQ-490 (MVIEW, #1016 closed), REQ-037, REQ-488
- Constraints:  REQ-201 (refusals are logged), REQ-143 (disabled controls), read-only refusal before any change
- Acceptance:   restated from REQ-518 Acceptance (see spec/requirements.md)
- Owning subsystem: alignment (frames) + commands (paper layouts) + Qt ribbon/dialog

## 2. Scope
- In scope:
  - `CREATESHEETS <alignment>, <template layout>[, <frames>]` command and its registry/plot-token entries.
  - One new paper layout per chosen frame: copy of the template's paper content, one viewport covering the full
    sheet, centred on the frame centre, scale = frame scale, rotated (camera roll) so the alignment is horizontal.
  - Naming `VF-n` with ` (2)` suffix on collision. One undo step for all layouts.
  - Refusals: read-only, no group, multiple groups, missing/model-space template, paper size mismatch, unknown frame.
  - Qt: Output → Plan Production → Create Sheets enabled; dialog builds the command text.
  - Tests in `tests/ViewFrameSheetTests.cpp` (`[req518]`).
  - Docs: resources/wiki (Alignments.md, Command-Reference.md, Commands/createsheets.md).
- Out of scope:
  - Updating existing sheets when frames change (REQ-518.6).
  - Sheet numbers (no field exists).
  - Template viewport rectangle (ignored by REQ-518.2).
  - Civil 3D comparison screenshots (waived by the user).
- Smallest change: a pure helper that computes one viewport from a frame + settings; one command that loops frames.

## 3. Architectural boundary check
- [x] No — proceed. Uses existing ViewFrame data, existing PaperLayout/Viewport (camRollDeg already persisted),
      existing Camera::RollToPlaceUp. No new store, no new file format field.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| 1 | Rotate sheet viewports with the frame, or leave them unrotated (REQ-518.2) | 2026-11-07 | (a) rotate — user chose |
| 2 | Accept REQ-518 | 2026-11-07 | accepted with choice (a), via the implement request |

## 5. Plan
- Roll: `roll = Camera::RollToPlaceUp(0, 90, (-ty, tx, 0))` where (tx,ty) is the alignment tangent at the frame's
  centre station; this puts the tangent to screen-right.
- Viewport: paperX/Y = 0, paperW/H = template sheet size (landscape-normalised, equal to frame sheet size),
  modelCenter = frame centre, scaleModelPerPaperIn = groundPerInch, annotationScaleName = scale name, camRollDeg = roll.
- Test approach: frame corners mapped through `ModelToPaperInThroughCamera` land inside the sheet, the along-alignment
  edge is horizontal on the sheet, and the sheet extent equals the frame length/width; command refusals; layout
  count, names, suffixing, undo of all layouts in one step; the template layout is unchanged.
