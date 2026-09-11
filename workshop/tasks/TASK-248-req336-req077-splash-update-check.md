# TASK-248 — Move startup update check into splash (unblock What's New)

## Authority
- REQ-336 — What's New uses bundled `resources/whats-new.md`; must not be blocked by network update check
- REQ-077 — startup update check (user decision: run during splash, not as main-window modal gate)
- User request 2026-09-10

## Problem
The REQ-077 "Checking for updates" modal runs after the splash and blocks all input until the
network fetch resolves. What's New auto-opens on the Start screen but sits behind that modal, so
users on slow connections never see the bundled release notes.

## Approach
1. Start `BeginStartupCheck` before `RunStartupSplash` (after prefs load).
2. Poll `PollUpdateTask` inside the splash frame loop; keep the splash visible until both the
   minimum duration elapses **and** the check finishes (or was skipped/disabled).
3. Show "Checking for updates…" on the splash status line while `Phase::Checking`.
4. Remove the main-window modal for `Phase::Checking`; only REQ-078 dialogs appear after splash.
5. When an update is offered, close/suppress What's New so the update dialog takes priority.

## Files
- `src/ui/SplashScreen.hpp`, `SplashScreen.cpp`
- `src/app/main.cpp`
- `src/ui/CadUi_UpdateDialog.cpp`
- `src/update/UpdateService.hpp` (comments)

## Test
- Build + existing `UpdateCheckTests` (pure logic unchanged)
- Manual: launch with slow/offline network — splash shows check status; main opens with What's New
  when not dismissed; update offer closes What's New if open

## Status
Done — build green; UpdateCheckTests unchanged (pure logic)
