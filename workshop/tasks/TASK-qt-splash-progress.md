# TASK: Qt splash progress bar and phase label advance (issue #1028)

## Requirement authority
REQ-093 (accepted, fixed minimum display, animated progress bar, cosmetic phase text), decision D-2026-10-21-f (applies it to the Qt splash: 8 s after the window is built, six phase labels). Related: REQ-077 (the update check gates the session), D-2026-08-23-g (labels are cosmetic).

## Acceptance criteria (issue #1028)
- The progress bar visibly advances during the splash display (8 s, per D-2026-10-21-f).
- Neutral phase labels (six, per the user) visibly change during that display.
- Startup timing and keeping the main shell hidden until the splash duration ends remain intact.

## Files / subsystems
- `src/qt/QtSplash.hpp/.cpp` (new): pure progress/label functions plus the splash frame painter.
- `src/qt/main.cpp`: shows the first frame, then redraws the splash while waiting for max(5 s, update check) before showing the window.
- `cmake/GoSurveyQt.cmake`: adds `QtSplash.cpp`.
- Tests: `QtEditorTests` (`issue1028...`) checks timing/labels, bar fill and label change, and saves frames to `build/artifacts/`.

## Approach
Progress and label are functions of wall-clock time on a clock restarted once the main window has been built, so no timers or threads are added. The existing wait loop in `main()` (already pumping events for the update check) now also runs until 8 s have passed and redraws the splash each pass. Smoke-test runs skip the splash and the hold.

## Architectural-boundary check
Qt UI only; no new dependencies, state or threads.

## Assumptions / technical debt
EditorWindow construction (~3 s) blocks the UI thread, so the splash shows its first frame unchanged during it; the 8 s clock starts afterwards. Startup is therefore roughly 3 s + 8 s.
