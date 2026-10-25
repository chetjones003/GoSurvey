# TASK-1024 — Qt ZOOM EXTENTS never changes the viewport

- Type: bug
- Status: needs review
- Opened: 2026-10-25
- Owner: Workshop

## 1. Authority
- Goal: Qt parity with the retired ImGui viewport
- Requirements: REQ-120 (accepted), REQ-122 (accepted), REQ-123 (accepted), REQ-201
- Acceptance:
  - Typed `ZOOMEXTENTS`, the View menu item and the middle-button double-click frame the drawing in the Qt model viewport.
  - Paper space frames the sheet; an activated floating viewport frames the model into its own rectangle (existing `ProcessPendingViewportZoom` behaviour).
  - An empty drawing reports why nothing moved and leaves the view unchanged.
  - A Qt-level test checks the camera after `ZOOMEXTENTS` (the headless transcripts cannot).
- Owning subsystem: Qt viewport (`EditorViewport::paintGL`); the zoom maths stays in Commands.

## 2. Scope
- In scope: call `ProcessPendingViewportZoom` from the Qt viewport paint path; Qt regression test; this log.
- Out of scope: any change to framing maths, the spec, or other frontends.
- Smallest change: one call at the top of `paintGL`, before the camera is built.

## 3. Architectural boundary check
- No new abstraction, dependency or state. `ProcessPendingViewportZoom` is the existing Commands entry point; the retired ImGui `CadUi.cpp` called it every frame with the viewport size, and that call was lost in the Qt move (#966).

## 4. Questions and assumptions
- None. The same call also consumes `pendingZoomWindow` (ZOOM WINDOW), which was equally stranded; this is the same defect and needs no separate handling.

## 5. Plan and verification
- Files: `src/qt/EditorViewport.cpp`, `tests/QtEditorTests.cpp`.
- Test: `issue1024QtZoomExtentsFramesTheDrawing` draws a line, moves the camera far away, submits `ZOOMEXTENTS`, and checks pan/zoom/log; then an empty drawing keeps its view and logs "nothing to frame". The #997 grading screenshot test now uses the real `ZOOMEXTENTS` instead of setting the camera by hand.

## 6. Implementation log
- 2026-10-25 — Confirmed `ProcessPendingViewportZoom` had no caller in `src/`. Added the call in `paintGL` with the viewport pixel size and aspect (width/height, as the ImGui caller did).
- 2026-10-25 — First test draft failed because `LINE` was still active (the command correctly refused `ZOOMEXTENTS` mid-command); the test now ends the LINE first.

## 7. Verification result
- Plan verdict: APPROVE — restores a call the Qt migration dropped; no spec change.
- Implementation verdict: PASS — `build.bat` succeeded; full `ctest --preset ninja-release` 191/191 (the new test's first draft had two test-side mistakes, fixed: LINE left active, and zoom is inverse so the assertion was wrong; shard-0 re-run passed with the other 190 unchanged).
- Review verdict: PASS — two-line production change in the paint path; self-reviewed against the issue and REQ-120/123.

## 8. Outcome
- Requirements satisfied: REQ-120, REQ-122, REQ-123 in the Qt viewport.
- Tests added: `issue1024QtZoomExtentsFramesTheDrawing`; the #997 grading screenshot test now uses the real `ZOOMEXTENTS`.
- Assumptions / technical debt: none.
- Product documentation review: `README.md` reviewed; no change needed. `PRIVACY.md` reviewed; no change needed. In-app wiki (`resources/wiki/Commands/zoomextents.md`, `Views-and-Navigation.md`, `Keyboard-Shortcuts.md`) already documents the intended behaviour this fix restores; no change needed. Public GitHub Wiki documents the same behaviour; no change needed.
