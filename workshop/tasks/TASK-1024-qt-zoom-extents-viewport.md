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
  - Typed `ZOOMEXTENTS`, the View menu item and the middle-button double-click frame the drawing in the Qt model viewport, leaving 8% free on the binding axis (REQ-122), in wide, tall and ultra-wide windows and when orbited.
  - The middle double-click works mid-command without disturbing it, and over the ViewCube (a middle press already passes through the cube); the typed command is still refused mid-command (REQ-120).
  - Paper space frames the sheet; an activated floating viewport frames the model into its own rectangle; with the zoom lock on, the sheet is framed (REQ-120, REQ-123).
  - An empty drawing reports why nothing moved and leaves the view unchanged (REQ-201).
  - The other requests that were stranded with it — a queued ZOOM WINDOW, the BEDIT frame, and the DXF/DWG import fit — work in the Qt viewport (REQ-122).
  - Qt-level tests check the camera after each of these (the headless transcripts cannot).
- Owning subsystem: Qt viewport (`EditorViewport`); the zoom maths stays in Commands.

## 2. Scope
- In scope: consume the pending zoom from the Qt paint path; publish the viewport's pixel size so imports fit to it; a middle-button branch in `mouseDoubleClickEvent` (the Qt viewport had no handler for the REQ-120 gesture at all); Qt regression tests; this log.
- Out of scope: any change to framing maths, the spec, or other frontends; starting ZOOM WINDOW's first corner from a click (separate defect, #1100).
- Files: `src/qt/EditorViewport.cpp` (paint call, size publish, double-click branch); `src/commands/CadCommands.{hpp,cpp}` (new `RequestZoomExtents`, which `StartZoomExtentsCommand` now also uses; `ClearPendingViewportZoom` is declared here, moved from `CadCommandsInternal.hpp` so it has one declaration); `src/qt/EditorInput.cpp` (reuses `ClearPendingViewportZoom`); `tests/QtEditorTests.cpp`.

## 3. Architectural boundary check
- No new dependency or state. `ProcessPendingViewportZoom` is the existing Commands entry point; the retired ImGui `CadUi.cpp` called it every frame with the viewport size, and that call was lost in the Qt move (#966). `RequestZoomExtents` has two real callers (the typed command and the Qt gesture) and keeps the Qt layer from setting the domain flag by hand. `ClearPendingViewportZoom` was already public in practice (used from `src/qt`), so it now lives in the public header only; the internal header's duplicate is removed.

## 4. Questions and assumptions
- None needing a decision. `ProcessPendingViewportZoom` also consumes `pendingZoomWindow` and the BEDIT request, which were equally stranded; the same call fixes them. BEDIT now opens framed on the block, as it did in ImGui.
- The DXF/DWG import fit reads `viewportLastFbW/H`, which nothing in Qt ever wrote (they kept their 900 x 650 defaults), so imports fit to the wrong window shape. The viewport now publishes its pixel size every paint. Only the empty-drawing fallback uses the pending flag.
- A typed command on the Start tab first creates a new drawing (`EditorWindow::Submit`), so a ZOOMEXTENTS typed there cannot stay queued and later reframe another drawing.
- Spec note for the maintainer (not edited here, per the SPEC rules): REQ-120 quotes the refusal text `"Could not parse point…"` for a typed `ZOOMEXTENTS` mid-command; the current text is `LINE is still running, so "ZOOMEXTENTS" was read as input…`. The behaviour (refused, nothing relaxed) is unchanged.
- The pending request is not held during a window-resize preview: the preview already disables updates, and `EndWindowResizePreview` repaints, so the request is simply consumed at the real size.

## 5. Plan and verification
- Tests (all through the real viewport): `issue1024QtZoomExtentsFramesTheDrawing` (centre, log, and an independent check that the whole drawing fits with the binding axis at 8% free, in 1280x800, 600x900 and 1600x500 windows); `...WhenOrbited` (3D silhouette framing); `issue1024QtMiddleDoubleClickZoomsExtentsMidCommand` (typed command still refused, double-click works off the cube and on its centre, home button and corner, the LINE keeps its phase and first point); `issue1024QtQueuedZoomWindowFramesTheRectangle`; `...OnAnEmptyDrawingSaysWhyAndKeepsTheView`; `...InPaperSpaceFramesTheSheet`; `...InAFloatingViewportFramesTheViewport` (hand-worked REQ-123 numbers, then the zoom lock); `issue1024QtBlockEditFramesTheBlockContent`; `issue1024QtViewportPublishesItsPixelSizeAndImportsFitToIt` (DXF import into a 1600x500 window). The #997 grading screenshot test now uses the real `ZOOMEXTENTS`.

## 6. Implementation log
- 2026-10-25 — Confirmed `ProcessPendingViewportZoom` had no caller in `src/`. Added the call in `paintGL` with the viewport pixel size and aspect (width/height, as the ImGui caller did).
- 2026-10-25 — Self-review found the middle double-click had no Qt handler either; added one (mid-command safe, camera only).
- 2026-10-25 — First test drafts failed for test-side reasons (a `LINE` left active refused `ZOOMEXTENTS`; zoom is the inverse of the visible span). Corrected.
- 2026-10-25 — `/code-review` round 1, all addressed: reused `ClearPendingViewportZoom`; middle double-click ahead of the ViewCube check; split the test into focused cases with named constants and shared helpers; added the 8% framing check, first-point/phase check, typed-refusal check, paper-space, floating-viewport and zoom-lock coverage.
- 2026-10-25 — `/code-review` round 2, all addressed: one declaration of `ClearPendingViewportZoom`; added `RequestZoomExtents` for the two callers; the import fit claim was untrue — the viewport now publishes `viewportLastFbW/H` and a DXF import test proves the fit; removed the redundant resize-preview guard; added tests for a queued ZOOM WINDOW, BEDIT, orbited framing, other window shapes and more ViewCube points; named the test constants and added `IsFramed`. Checking the Start-tab concern showed it cannot occur (see §4). Writing the ZOOM WINDOW test exposed a separate defect — clicking never sets its first corner in Qt — filed as #1100 and kept out of this change.
- 2026-10-25 — Red run (round 2): with the paint-path zoom call and the size publishing disabled, all nine new tests fail; with them restored, they pass.

## 7. Verification result
- Plan verdict: APPROVE — restores calls the Qt migration dropped; no spec change.
- Implementation verdict: PASS — see §8 for the build and suite results.
- Review verdict: `/code-review` (Standards + Spec axes) was run twice; every finding from both rounds was addressed in the follow-up commits.

## 8. Outcome
- Requirements satisfied: REQ-120, REQ-122, REQ-123 in the Qt viewport.
- Tests added: the nine `issue1024Qt...` tests above; the #997 grading screenshot test now uses the real `ZOOMEXTENTS`.
- Assumptions / technical debt: ZOOM WINDOW cannot be started by clicking in Qt (#1100); its test queues the request directly. The Qt import fit is proven for DXF; DWG shares the same three lines but has no Qt test.
- Product documentation review: `README.md` reviewed; no change needed. `PRIVACY.md` reviewed; no change needed. In-app wiki (`resources/wiki/Commands/zoomextents.md`, `Views-and-Navigation.md`, `Keyboard-Shortcuts.md`) already documents the intended behaviour this fix restores; no change needed. Public GitHub Wiki documents the same behaviour; no change needed.
- Build and suite: recorded in the PR description.
