# TASK — Run the Qt GUI tests on an invisible desktop

Authority: user request, 2026-10-17 (test tooling only; no spec requirement, no product code change).
Problem: `GoSurveyQtTests` opens real top-level windows, which flash on the developer's screen, steal
focus, and (when a test hangs) leave a window that must be closed by hand.

## Approach

- `tools/run-qt-tests-hidden.ps1` starts the test executable on its own desktop (`CreateDesktop`), which
  is never displayed and has its own input queue, then enforces a timeout (exit 124, process killed).
  The real GPU driver is used; Qt's `offscreen` platform has no OpenGL on Windows and crashes at start.
- `ctest -R qt.editor` runs the suite through the launcher.
- Finding: the viewport tests failed on the hidden desktop only because a `QOpenGLWidget` initialises
  on its first paint and an invisible desktop never delivers one. The tests now call
  `grabFramebuffer()`, which creates the context and runs `initializeGL()` directly (also faster on a
  visible desktop than waiting for exposure).

## Verification

Full Qt suite hidden vs visible gives the same results. The remaining failures are independent of
display and pre-date this change: `nativeLayerManagerEditsAreUndoable`,
`releaseRibbonContextTracksSelectionAndRestoresNormalTab`,
`releaseRibbonHatchDefaultsAndSelectionEditsAreSeparateAndUndoable` (introduced on `beta` by #922) and
`tabsAppearInitiallyAndTypingRoutesFromEditorChrome` (fails on a visible desktop too).

## Not covered

A machine with no GPU (e.g. a hosted CI runner) still needs software OpenGL; not attempted here.
