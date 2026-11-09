# TASK-805 — Home ▸ Clipboard: Cut (and Paste Special decision)

- Type:    feature
- Status:  Cut in review; Paste Special open — see §8
- Opened:  2026-11-08
- Owner:   Claude (full-send, issue #805)

## 1. Authority
- Goal:         GOAL-001 (Qt UI usability and consistency, D-2026-11-03-a in spec/project.md)
- Requirements: REQ-084 (c) — the shortcut menu's Clipboard submenu lists **Cut | Copy | Paste**, with Cut/Copy disabled on an
                empty selection (accepted 2026-08-18). REQ-038 — clipboard is in-process, Ctrl+V paste
                (accepted). REQ-382 — a read-only project refuses writes (accepted).
- Constraints:  REQ-453 pre-PR documentation review (README, PRIVACY.md, resources/wiki, public wiki). This is
                the documentation review at spec/requirements.md "REQ-453 — Review product documentation before
                opening a pull request" (a second REQ-453 heading in that file is about Store installs).
- Acceptance (restated from the REQ text and the issue; the issue itself had none):
  - REQ-084 (c): "Clipboard (submenu) — Cut | Copy | Paste, with Cut/Copy disabled on an empty selection."
    The ribbon Cut button is enabled only when something is selected.
  - Cut copies the selection to the clipboard and then erases it. One Undo restores it (the undo rule that
    applies to every erase; neither REQ-038 nor REQ-084 mentions undo).
  - Cut with nothing selected does nothing and does not start an interactive delete.
  - Cut is refused in a read-only drawing (REQ-382).
  - Cut refuses, and erases nothing, when any selected object is one the clipboard cannot carry.
  - Ctrl+X performs Cut, as the context menu already advertises.
- Owning subsystem: Qt editor (src/qt) + command layer (src/commands/CadCommands.cpp).

## 2. Scope
- In scope:        Cut in the ribbon, context menu, Ctrl+X and the CUT verb; wiki docs for Cut and Ctrl+X.
- Out of scope:    **Paste Special** (issue #805 second half) — SPEC GAP, awaiting user decision.
                   External-format paste (PASTESPEC OLE/text/image) — not requested.
- Smallest change: `CutSelectionToClipboard` (commands) + `EditorWindow::CutSelection` (Qt read-only guard).

## 3. Architectural boundary check
- [x] No — proceed. Reuses CopySelectionToClipboard and StartDeleteCommand. One new command-layer function,
  with its allow-lists kept local to the file. The Qt side adds one guarded method used by the ribbon
  verb, the context menu and Ctrl+X.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Paste Special: what should the ribbon button do? (see §8) | 2026-11-08 | pending |

## 5. Assumptions
ASSUMPTION-1: Ctrl+X should perform Cut.
- Because:       the context menu already labels Cut as `Ctrl+X`; the key was never bound in the drawing.
- Risk if wrong: low; a key that does nothing is a defect.
- Validate by:   test `cutRibbonButtonAndCtrlXRunCut` (Ctrl+X via keyClick).

ASSUMPTION-2: Cut's erase is one undo step for the user.
- Because:       a survey point and its label, deleted by two steps that each push an undo frame, make
                 two frames. Cut drops the second frame (matched by its label "Delete", not by counting, so it
                 holds when the history is full) and renames the first to "Cut".
- Risk if wrong: Undo would need two steps for survey-point Cuts.
- Validate by:   `cutOfSurveyPointAndLineUndoesInOneStep` and `cutOfSurveyPointAndLineUndoesInOneStepWhenHistoryIsFull`.

ASSUMPTION-3: The allow-lists match the types the copy loops handle.
- Because:       the copy loops are the only place the clipboard receives objects.
- Risk if wrong: an object could be erased without a copy. Any type not on the list is refused, so this
                 fails safe.
- Validate by:   `cutRefusesEveryTypeTheClipboardCannotCarry`, `cutRefusesPaperTable`.

## 6. Plan
- Approach: check first (allow-lists), then CopySelectionToClipboard + StartDeleteCommand. The check
  refuses before anything changes, so a Cut never erases an object the clipboard lacks.
- Files/functions: CadCommands.{cpp,hpp} (CutSelectionToClipboard); EditorWindow.{hpp,cpp}
  (CutSelection + CUT verb); EditorContextMenu.cpp; EditorInput.cpp (Ctrl+X); RibbonCatalog.hpp;
  EditorRibbon.cpp (enablement).
- Test approach: happy path = cutCopiesDeletesAndUndoRestores, cutOfSurveyPointAndLineUndoesInOneStep;
  failure = cutRefusesEveryTypeTheClipboardCannotCarry, cutRefusesPaperTable,
  cutWithNothingSelectedDoesNotStartAnInteractiveDelete, cutRefusedInReadOnlyDrawing;
  wiring = cutRibbonButtonAndCtrlXRunCut, cutRibbonButtonFollowsSelection, RibbonWiringGapTests "Issue #805".
- Steps:
  - [x] spec read, evidence pass
  - [x] core function + allow-list refusal rules
  - [x] Qt wiring (ribbon, menu, Ctrl+X, verb), read-only guard, ribbon enablement
  - [x] tests (Qt editor and unit Catch shards pass; screenshots in build/artifacts)
  - [x] full ctest (194 of 194) passed on the final build (run after the last code edit)
  - [x] council review, four rounds of three Claude Haiku reviewers; scores recorded in the PR body
  - [ ] PR, merge

## 7. Workflow-specific notes
- The copy loops carry only the types in the allow-lists. COPYCLIP refuses surfaces with a message and skips
  feature lines with a message. Cut refuses every non-carried type before the copy runs.
- Not changed here: DELETE (the Delete key and the Erase menu item) has no read-only guard in the Qt layer
  (pre-existing REQ-382 drift). Cut is stricter than DELETE and is covered by a test.
- Not changed here: Cut is not on the Edit menu; the context menu and the ribbon carry it.
- Open question, not decided here: Cut does not cancel an active command (for example an unfinished MOVE).
  The Delete key behaves the same way. Parity with DELETE is kept until a decision is recorded.
- Visual review (CLAUDE.md §3.6): the user waived the Civil 3D comparison for this issue. No Civil 3D capture
  was made and no three-reviewer score panel was run, so the paired comparison is INCONCLUSIVE (the Civil 3D
  blocker is recorded here, not claimed as a pass). GoSurvey-only captures: build/artifacts/qt-editor/manual/shard-4/artifacts/
  (issue805-cut-disabled-empty-selection.png, issue805-cut-enabled-with-selection.png).
- Fixed after PR-Agent review (PR #1182): the undo history cap is switched off while a survey-point + line Cut
  runs, so the minimum limit of 1 also undoes in one step (test cutOfSurveyPointAndLineUndoesInOneStepAtHistoryLimitOne).
- Fixed after PR-Agent review: a grading-group rebuild that stops the erase now reports "only the survey points
  were erased" (or "nothing was erased") instead of success. Undo restores the survey points. No regression test:
  building a grading group in the test harness was not attempted; verified by reading the early return.
- Known limit: a single-step Cut records its undo frame as "Delete", so Undo's log says "UNDO: Delete".

## 8. SPEC GAP — Paste Special (evidence pass done)
- **What the issue says:** "Paste Special (paste-as-block / paste-with-options) also has no backing command."
- **What the spec says:** nothing about Paste Special. REQ-038 covers Ctrl+V paste only.
- **Evidence:**
  - Repo: PASTEORIG (paste at original coordinates) is on the right-click Clipboard menu. PASTEBLOCK
    (paste as block reference) is a typed command only, not on any menu. No command is named Paste Special.
    The ribbon has a disabled placeholder labelled "Paste Special" (RibbonCatalog.hpp).
  - Autodesk help: in AutoCAD, Paste Special (PASTESPEC) opens a dialog that pastes data copied from
    OTHER applications (text, pictures, OLE objects). Paste as Block and Paste to Original Coordinates
    are separate items on the Paste drop-down, not part of Paste Special.
- **Why it needs a human decision:** a new user-facing feature surface is a product choice, and the
  spec has no REQ for it.
- **Choices:** (A) Paste Special drop-down offering the two existing commands (PASTEBLOCK, PASTEORIG) —
  no new behaviour; (B) real PASTESPEC for external formats — a new feature, large; (C) keep it
  not-implemented and track separately.
- **Recommendation:** (A), with (B) as a separate issue.
- **Council round 1 (3 Haiku reviewers, read-only; scores 7, 5, 6) and fixes (fix-all):**
  1. Blocker: PASTEBLOCK skipped the REQ-383 coordinate-system check that PASTE and PASTEORIG run. Fixed:
     `CheckClipboardPaste` runs first; any Block or Warn verdict refuses before any change and points to
     Paste (Ctrl+V) for the full warning. Test `pasteBlockRefusesCoordinateMismatch`.
  2. Should-fix: the paper refusal I first added rested on a false premise. PASTE and PASTEORIG already
     place block references on the sheet (`CommitPasteIntoPaper`). Replaced: PASTEBLOCK now places them in
     `paperBlockRefs` in a paper layout. Test `pasteBlockPlacesReferencesInActiveSpace`.
  3. Should-fix: a stale layout index (activeSpaceIndex past the layout list) wrote into the model store.
     Fixed with Cut's guard (`activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace`). Test
     `pasteBlockRefusesStaleLayoutIndex`.
  4. Should-fix: tooltip overclaimed ("paste the clipboard as block references"). Reworded to
     "the clipboard's block references".
  5. Recorded in D-2026-11-09-a and REQ-038: the typed PASTEBLOCK changes are the paper routing and the
     guards above. Wiki row (in-repo and public, commits adfa384 then f56fbf6) keeps the plain Paste Special row.
- **Council round 2 (3 fresh Haiku reviewers; scores 6, 6, 7) and fixes:**
  1. Blocker (REQ-076): the paper branch kept the copied source's entity id. Fixed: both branches clear the id
     (`a.id = 0`). Test `pasteBlockPlacesReferencesInActiveSpace` sets source id 42 and asserts the sheet copy
     has id 0.
  2. Should-fix: the Warn refusal's hint pointed at Ctrl+V, which pastes the whole clipboard. The message now
     says "not pasted (needs a check)", names that limit, and points at Ctrl+V for the warning. Still a known
     gap: no block-only override (recorded in D-2026-11-09-a).
  3. Should-fix: the spec said PASTE applies the stale-index guard. It does not; only Cut does. Wording
     corrected in D-2026-11-09-a and REQ-038. REQ-038 now also names the paper routing.
  4. Should-fix: the coordinate-mismatch test asserted only the count. It now asserts the `blocked` line and
     the zone name in `m_Log`.
  5. Nit, kept: the menu item stays enabled on a line-only clipboard; the typed `PASTEBLOCK dx, dy` still
     takes offsets (the menu uses zero).
- **Known limits, kept on purpose:** the Paste Special menu stays enabled on any non-empty clipboard. On a
  line-only clipboard "Paste Block References" logs "clipboard has no block references" rather than being
  greyed out, because menu items are built once and not refreshed with the clipboard. With no offset,
  PASTEBLOCK places references where they were copied (zero offset, no cursor preview). Neither PASTE nor
  PASTEBLOCK has a read-only-drawing guard (pre-existing; Ctrl+V has the same gap).

## 9. REQ-453 documentation review (product sources)
- `README.md` — reviewed; no mention of Cut, Paste Special or Ctrl+X. No update needed.
- `PRIVACY.md` — reviewed; not affected by this change. No update needed.
- In-app wiki `resources/wiki/` — updated: Modify-Tools.md (Cut row shows Ctrl+X and CUT; note lists what
  Cut refuses; Paste at Original Coordinates menu path corrected), Known-Limitations.md (Ctrl+X row removed;
  the key now works), Keyboard-Shortcuts.md (Ctrl+X row added; added to the suppressed-while-typing list),
  Command-Reference.md (CUT row added).
- Public GitHub wiki (chetjones003/GoSurvey.wiki) — updated to match: Modify-Tools.md, Known-Limitations.md,
  Keyboard-Shortcuts.md, Command-Reference.md. Pushed as commits b8b85e33 and 33ee34e (master), checked with
  `git ls-remote` (HEAD 33ee34e).
- Paste Special (this PR): in-repo `resources/wiki/Modify-Tools.md` gained a Paste Special row (the note
  names PASTEBLOCK and PASTEORIG). Public wiki: same row pushed as 181006a, then the paper-sheet row
  confirmed as f56fbf6 (after adfa384 briefly said "model space only"; that was reverted before the PR).
  Known-Limitations, Keyboard-Shortcuts and Command-Reference need no change: no new key or command.
- `README.md` and `PRIVACY.md` — reviewed for Paste Special; no update needed.
