# TASK-145 — BEDIT: in-place isolated block-definition editing

- Type:    feature
- Status:  done
- Opened:  2026-08-29
- Owner:   workshop

## 1. Authority
- Goal:         Block support (issue #124)
- Requirements: REQ-107 (accepted for the block-editor slice, D-2026-08-29-h)
- Constraints:  CON-07 (build reproducibility); additional rules 1 (simple), 2 (no
                speculative abstraction), 5 (architectural consistency)
- Acceptance:   (from REQ-107, block-editor slice — restated)
  - BEDIT <name> from model space enters an edit session; refused in a paper layout;
    a second BEDIT for the block already open is a no-op.
  - While a session is open the viewport shows only that block's geometry in local
    coords; model + paper entities are not drawn / not pickable / not snappable; the
    block's own INSERT overlays are not drawn.
  - LINE/PLINE/CIRCLE/ARC/ELLIPSE/TEXT and MOVE/COPY/ROTATE/SCALE/DELETE/TRIM/OFFSET/
    MIRROR operate on the block's content; survey-point + CSV tools unavailable.
  - Any content change marks the session dirty.
  - BCLOSE on a dirty session raises Save / Don't Save / Cancel; Save writes the
    geometry into the definition and every INSERT re-renders; Don't Save restores the
    definition as at BEDIT; Cancel keeps the session open. Clean session: no prompt.
  - On close the ribbon tab and camera active at BEDIT are restored.
  - Nested blocks, meshes, attribute defs, parameters, actions preserved unchanged.
- Owning subsystem: Commands (block editor) + a one-line guard in the app shell + a
  close modal in the UI layer.

## 2. Scope
- In scope:     the seven acceptance conditions above via the ADR-043 model-store swap.
- Out of scope: dynamic-block authoring changes (BPARAM/BACTION keep their own commands);
                a paper-layout block editor; simultaneous multi-block editing; nested-block
                "edit in place" drill-down; disabling every survey ribbon button (command
                paths are guarded; ribbon polish is a follow-up).
- Smallest change: swap the model arrays for the definition's primitives on enter, harvest
                them back on save/close; no per-command routing branch.

## 3. Architectural boundary check
- New session state + a new editing surface + hiding the other spaces + a close gate →
  **architectural**. Escalated as a SPEC GAP and resolved by **D-2026-08-29-h / ADR-043**
  before any code. Workshop made no architectural decision of its own.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | New MDI drawing tab, or edit in place with an isolated viewport view? | 2026-08-29 | In place with an isolated viewport view. |
| Q2 | Mechanism: fourth `activeSpaceIndex` with per-command routing, or model-store swap? | 2026-08-29 (in the ADR proposal) | Store swap (user signed off on the ADR). |

## 5. Assumptions
```
ASSUMPTION-1: restoring an empty selection on close is acceptable.
- Because:       CadCaptureGeometrySnapshot does not carry `selection`.
- Risk if wrong: minor UX (the pre-BEDIT selection is not re-highlighted).
- Validate by:   user review of the running app.
ASSUMPTION-2: a survey point created mid-session (rare; UI-driven) is acceptable to
              refuse rather than silently drop.
- Because:       the store swap discards `surveyPoints` edits on close.
- Risk if wrong: a workflow that expects to add points inside BEDIT.
- Validate by:   user review; CSV import is guarded in SurveyCsvImportFile.
```

## 6. Plan (as built)
- `AppCommandState`: `blockEditActive`, `blockEditModelStash` (DrawingGeometrySnapshot),
  `blockEditCleanRevision`, `blockEditUndoMark`, `blockEditCloseAsked`, saved camera.
- `CadCommands.cpp`: expose `CadCaptureGeometrySnapshot` / `CadRestoreGeometrySnapshot` /
  `CadActiveUndoStackSize` / `CadTruncateActiveUndoStack`; guard `SetActiveSpace` while a
  session is active.
- `CadBlocks.cpp`: `LoadBlockPrimitivesIntoDrawing` / `HarvestDrawingPrimitivesIntoContent`
  (file-local); `CadBlocksEnterNamedEditor` refuses paper space / a second block, pushes an
  undo boundary, stashes the drawing + camera, loads the definition's primitives; `BSAVE`
  and `BCLOSE` harvest and (on close) restore the stash, re-apply the edited definitions,
  restore camera, drop session undo frames; `BEDITADD` writes to the model arrays while a
  session is active so it survives the harvest.
- `SurveyCsv.cpp`: `SurveyCsvImportFile` refuses while a session is active.
- `CadUi_BlockAuthoring.cpp`: Save / Don't Save / Cancel modal driven by
  `blockEditCloseAsked`.
- Test: `tests/headless/transcripts/issue124-bedit-inplace.txt` (happy path + isolation +
  dirty-close prompt + Save + Don't Save + clean-close-no-prompt + BEDIT-in-paper refusal).

## 8. Implementation log
- 2026-08-29 recorded D-2026-08-29-h / ADR-043; REQ-107 → accepted (slice).
- 2026-08-29 implemented the swap; found and fixed two defects during testing:
  (a) the geometry stash carries `blockDefs`, so restoring it clobbered the just-saved
      definition — fixed by re-applying `blockDefs` after the restore;
  (b) session draw commands push undo frames of the block's geometry — fixed by marking
      the undo depth on enter and truncating to it on close.
- 2026-08-29 `BEDITADD` re-pointed at the model arrays during a session (else the harvest
  on Save dropped its segment).
- 2026-08-29 app shell: tab switch refused while a session is active (snapshotting the
  block geometry as the tab's document would corrupt it).

## 9. Self-verification
- [x] build-project        — PASS (ninja-release; ninja-debug incl. DevShell links clean)
- [x] architecture-review  — PASS (no Workshop architectural decision; ADR-043 governs)
- [x] code-review          — PASS
- [x] dependency-audit     — n-a (no dependency added)
- [x] performance-review   — n-a (enter/close copy the geometry set once, same cost as one
      undo push; no per-frame or per-command cost)
- [x] testing              — PASS (772/772 ctest; new transcript green)

## 10. Verification result
- Submitted:  2026-08-29
- Verdict:    PASS
- Findings:   two defects found and fixed during self-verification (stash clobbering the
              saved definition; session undo frames) — see the implementation log. No open
              findings. Visual checks (isolated view renders only the block; modal wording;
              camera + ribbon restore) handed to the user in the running app.

## 11. Outcome
- Requirements satisfied: REQ-107 block-editor slice (Acceptance met: yes)
- Tests added:            headless.issue124-bedit-inplace
- Refactors:              CadCaptureGeometrySnapshot/RestoreGeometrySnapshot given
                          external-linkage wrappers (were file-static)
- Docs updated:           spec/project.md, spec/requirements.md, spec/architecture.md
- Done:                   2026-08-29 (pending verification verdict)
