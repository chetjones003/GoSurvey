# TASK-1024 — Qt ZOOM EXTENTS never changes the viewport

- Type: bug
- Status: needs review
- Opened: 2026-10-25
- Owner: Workshop

## 1. Authority
- Goal: Qt parity with the retired ImGui viewport. `spec/project.md` defines no numbered `GOAL-NN` entries (its goals section is an unfilled template), so none can be cited.
- Requirements: REQ-120, REQ-122, REQ-123 (all accepted); REQ-201 (state the reason when nothing changes).
- Constraints: no numbered `CON-NN` applies; `spec/project.md` §7 (Windows/MSVC, build output under `build/`) is observed.
- Acceptance:
  - Typed `ZOOMEXTENTS`, the View menu item and the middle-button double-click frame the drawing in the Qt model viewport, leaving 8% free on the binding axis (REQ-122).
  - The middle double-click works mid-command without disturbing it, and over the ViewCube (a middle press already passes through the cube); the typed command is still refused mid-command (REQ-120).
  - Paper space frames the sheet; an activated floating viewport frames the model into its own rectangle; with the zoom lock on, the sheet is framed (REQ-120, REQ-123).
  - An empty drawing reports why nothing moved and leaves the view unchanged (REQ-201).
  - Qt-level tests check the camera after `ZOOMEXTENTS` (the headless transcripts cannot).
- Owning subsystem: Qt viewport (`EditorViewport`); the zoom maths stays in Commands.

## 2. Scope
- In scope: consume the pending zoom from the Qt paint path; a middle-button branch in `mouseDoubleClickEvent` (the Qt viewport had no handler for the REQ-120 gesture at all); Qt regression tests; this log.
- Out of scope: any change to framing maths, the spec, or other frontends.
- Smallest change: `src/qt/EditorViewport.cpp` (paint call + double-click branch), `ClearPendingViewportZoom` declared in `src/commands/CadCommands.hpp` so the Qt code reuses it instead of hand-clearing two flags, and `src/qt/EditorInput.cpp:264` switched to it.

## 3. Architectural boundary check
- No new abstraction, dependency or state. `ProcessPendingViewportZoom` is the existing Commands entry point; the retired ImGui `CadUi.cpp` called it every frame with the viewport size, and that call was lost in the Qt move (#966). `ClearPendingViewportZoom` already existed; only its declaration is new.

## 4. Questions and assumptions
- None. The same call also consumes `pendingZoomWindow` (ZOOM WINDOW) and the post-import fit requested by DXF/DWG import; both were equally stranded and are the same defect (REQ-122 names the post-import fit).
- The pending request is held while a window-resize preview is active, because the preview freezes the widget at its old size and the framing would use the old aspect.

## 5. Plan and verification
- Files: `src/qt/EditorViewport.cpp`, `src/qt/EditorInput.cpp`, `src/commands/CadCommands.hpp`, `tests/QtEditorTests.cpp`.
- Tests (all through the real viewport): `issue1024QtZoomExtentsFramesTheDrawing` (centre, log, and an independent check that the whole drawing fits with the binding axis at 8% free); `issue1024QtMiddleDoubleClickZoomsExtentsMidCommand` (typed command still refused, double-click works off and over the ViewCube, the LINE keeps its phase and first point); `issue1024QtZoomExtentsOnAnEmptyDrawingSaysWhyAndKeepsTheView`; `issue1024QtZoomExtentsInPaperSpaceFramesTheSheet`; `issue1024QtZoomExtentsInAFloatingViewportFramesTheViewport` (hand-worked REQ-123 numbers, then the zoom lock). The #997 grading screenshot test now uses the real `ZOOMEXTENTS`.

## 6. Implementation log
- 2026-10-25 — Confirmed `ProcessPendingViewportZoom` had no caller in `src/`. Added the call in `paintGL` with the viewport pixel size and aspect (width/height, as the ImGui caller did).
- 2026-10-25 — Self-review found the middle double-click had no Qt handler either; added one (mid-command safe, camera only).
- 2026-10-25 — First test drafts failed for test-side reasons (a `LINE` left active refused `ZOOMEXTENTS`; zoom is the inverse of the visible span). Corrected.
- 2026-10-25 — `/code-review` follow-up, all addressed: reused `ClearPendingViewportZoom` instead of hand-clearing flags; moved the middle double-click ahead of the ViewCube check so it zooms there too; hold the request during a resize preview; split the single test into five focused ones with named constants and shared helpers; added the 8% framing check, first-point/phase check, typed-command-refused check, paper-space, floating-viewport and zoom-lock coverage; corrected this log.
- 2026-10-25 — Red run: with the paint-path call disabled, all five new tests fail (`!st.pendingZoomExtents` / pan never moves); with it restored they pass.

## 7. Verification result
- Plan verdict: APPROVE — restores a call the Qt migration dropped; no spec change.
- Implementation verdict: PASS — see section 8 for the build and suite results.
- Review verdict: `/code-review` (Standards + Spec axes) was run on the first revision; every finding was addressed in the follow-up commit.

## 8. Outcome
- Requirements satisfied: REQ-120, REQ-122, REQ-123 in the Qt viewport.
- Tests added: the five `issue1024Qt...` tests above; the #997 grading screenshot test now uses the real `ZOOMEXTENTS`.
- Assumptions / technical debt: none. Not covered by a Qt test: the resize-preview hold (verified by reading; the preview is a transient state that the tests do not drive).
- Product documentation review: `README.md` reviewed; no change needed. `PRIVACY.md` reviewed; no change needed. In-app wiki (`resources/wiki/Commands/zoomextents.md`, `Views-and-Navigation.md`, `Keyboard-Shortcuts.md`) already documents the intended behaviour this fix restores; no change needed. Public GitHub Wiki documents the same behaviour; no change needed.
- Build and suite: recorded in the PR description.
