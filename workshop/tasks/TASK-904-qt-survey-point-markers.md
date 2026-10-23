# TASK-904 — Qt survey-point marker visibility and style controls

Issue: https://github.com/chetjones003/GoSurvey/issues/904

## Status: Option A (stacked controls) selected and approved — REQ-428 implemented; broader verification FAIL (pre-existing, see below)

The issue requests full beta 0.7.4 parity and two or three integrated screenshot
options for each major affected surface, followed by the user's selection.

## Investigation

- `src/ui/CadUiSettings.cpp`, `DrawUserPrefsSurveyPoints`, exposes coordinate
  precision, plotted cross span, point-ID visibility, plotted label height,
  east/north label offsets, and leader-arrow width.
- `src/qt/EditorViewport.cpp` already reads point-ID visibility but draws its own
  survey-point overlay. Rendering must be checked as part of the workflow port.
- Accepted REQ-090 covers rollover readouts; REQ-153 covers the contextual ribbon;
  REQ-357 covers drawing scale; REQ-377 covers project point filtering. None states
  the marker appearance/settings workflow requested here. REQ-427 concerns
  geographic/position markers, which are different objects.

## Decision needed

Recommend recording the existing 0.7.4 survey-point marker and linked-label
controls as the accepted Qt parity target, preserving their existing state,
validation, persistence, cancellation, undo and read-only behavior where applicable.
No new marker shapes or new filtering behavior are proposed.

User resolved the gap: “Yes. Full Qt port”. REQ-428 and D-2026-10-18-f record
the decision. Plan verified: UI uses existing state, label services and preference IO;
no new dependency, ownership model or format. The existing live-edit/Close behavior
is retained; geometry undo and coordinate storage remain unchanged.

Affected files: EditorDialogs.cpp / EditorWindow.hpp (native controls),
QtAnnotations.cpp / EditorViewport.cpp (display), QtEditorTests.cpp (workflow and
captures), and the parity ledger. Verification: Qt build and hidden-desktop tests,
then the full Windows build/CTest suite. Applicable constraints are Windows MSVC,
downward dependencies and no speculative abstraction (project principles).

## Proposed implementation and verification after resolution

1. Record the approved requirement and decision, then verify the implementation
   plan against the accepted criteria and architecture.
2. Reuse existing preference/state and label-update services in the Qt settings
   surface; make the Qt viewport honor the same marker controls.
3. Add focused Qt workflow coverage for control effects and applicable edge cases.
4. Capture two or three integrated UI options on the hidden desktop; obtain the
   user's selected direction and show final meaningful-state screenshots.
5. Build with the repository Windows workflow, run relevant tests and verification
   checklists, and update `docs/qt-074-parity-ledger.md` with actual evidence.

Boundary: Qt owns widgets and presentation; existing command/domain and persistence
owners retain authoritative state. No dependency or storage-format change is planned.

## Implementation evidence (2026-10-19)

- Native Settings entry and modeless controls use existing state and label services.
  Edits are live; Close/Escape retains them, matching legacy Options' misleading
  Cancel behavior. Save Preferences reports failure without discarding live edits.
- Qt marker rendering now uses the shared plotted-span conversion and the camera
  basis; linked labels retain readability and screen-facing text, with moved-label
  leaders. Point-grid coordinate formatting leaves the underlying editor value intact.
- Windows Release Qt build and full `build.bat` succeeded. The first Qt build exposed
  a missing test include (`QBoxLayout`), corrected before the successful build.
- Initial focused hidden-desktop run: 6 passed / 0 failed (four workflow/capture
  functions plus initialization/cleanup), 61.551 seconds while the full build ran.
  Log: `build/artifacts/qt-904-focused.log`.
- Full CTest run: 2669/2672 passed; two unchanged non-Qt targets failed:
  `DWG export writes an INSERT with five attributes without corruption (issue #606)`
  (five missing attribute checks, LibreDwgCadTests.cpp:3710) and
  `headless.regression-surfacedelete-selection` (out-of-range surface selection).
  The third failure was Qt, explicitly stopped after it stalled in an unattended
  New Drawing chooser. Log: `build/artifacts/qt-904-ctest.log` (440.01 seconds).
- The flushed Qt log identified two viewport tests that never created a drawing
  and a third using interactive NEW for fixture setup. All three now create a blank
  drawing directly; assertions are unchanged. Their captures also use the required
  artifacts directory. The initial hypothesis about the template screenshot test
  was disproved by the flushed log; that test is unchanged.
- Final focused run before the screenshot clipping fix: 11 passed / 0 failed,
  9.335 seconds, including six survey-display cases, the three corrected viewport
  cases and lifecycle slots (`build/artifacts/qt-904-final-focused.log`).
- Raster timing, 1,000 markers at equivalent eight-pixel span, 30 frames:
  legacy fixed cross 0.426 ms/frame; new scaled/antialiased cross 1.174 ms/frame.
  This measures this overlay only, not the full REQ-100 reference-scene benchmark.
- Visual inspection caught clipped save-error text. The dialog now reserves the
  wrapped status height and resizes; a regression assertion checks the complete
  message fits without changing the dialog width.
- Final Qt Release rebuild succeeded (`qt-904-width-build.log`); final focused run
  passed 11/11 in 12.866 seconds (`qt-904-verified-focused.log`). Output-layout check
  passed again after that run. `git diff --check` is clean.
- Full Qt rerun reached its configured 280-second timeout: 86 passing slots and
  one existing editor-focus assertion (`tabsAppearInitiallyAndTypingRoutesFromEditorChrome`,
  missing layer widget). The log reached `nativeDatabaseGridResolvesCollisionAndHonorsReadOnly`
  before termination. No claim of a green full suite: `qt-904-qt-rerun.log` and
  `Release/qt-tests-17796.log` preserve the evidence.
- Verification verdict: focused REQ-428 behavior PASS; broader suite FAIL for the
  recorded non-Qt failures and Qt assertion/timeout. No dependency change. The
  raster timings are bounded overlay measurements, not a full reference-machine
  performance certification. Visual selection and final approval remain outstanding.
- Builds emitted existing third-party LibreDWG C4200 warnings and an untouched
  Rw5Import.cpp C4701 warning; no warning observed in changed production files.
- Architecture/code review: UI-only implementation; no new dependency, state owner,
  persistent format, geometry command or undo policy. Qt parents own the widgets;
  modeless callbacks reference stable application state rather than drawing arrays.
- Two integrated layouts captured using hidden-desktop `widget->grab()`, and the
  viewport with `grabFramebuffer()`. Visual selection/approval is pending; no commit
  or PR until the repository's visual gate is satisfied.
