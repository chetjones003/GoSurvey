# TASK-014 — AutoCAD-style floating command line (REQ-040)

## Authority
- Requirement: **REQ-040** (accepted) — floating command bar + fading history + F2 console.
- Decision: **ADR-015** (decision log, 2026-06-19).
- Architecture: UI subsystem; settings on `AppCommandState` persisted via `UserPrefs`
  (existing pattern). No new global/layer/dependency/abstraction (architecture §11).

## Architectural-boundary check
- New abstraction? No — reuses the existing command input/autocomplete in one function
  branched by `floating`; pure fade/tail helpers in `ui/CommandBar.hpp`.
- New dependency? No. New global? No (fields on `AppCommandState`). New data format? No
  (additive `settings` keys in `gosurvey-user.json`, all optional + clamped).
- Verdict from Verification: **APPROVE**.

## Plan (delivered)
1. Pure helpers `cmdbar::HistoryAlpha` / `cmdbar::LogTailStart` (`ui/CommandBar.hpp`) + unit
   tests (`tests/CommandLineTests.cpp`).
2. State on `AppCommandState`: `cmdLineClassicDock`, `cmdBarVisible`, `cmdBarAnchorValid/X/Y`,
   `cmdConsoleOpen`, `cmdBarFadeDelaySec`, `cmdBarOpacity`, `cmdBarHistoryLines`,
   `cmdLogLastChangeTime`, `cmdLogLastSizeForFade`, `cmdEnteredHistory`.
3. Persist the tunables in `UserPrefs.cpp` (load + save).
4. `DrawCommandLinePanel` (CadUi.cpp): one function, `floating` flag. Floating = undecorated,
   bottom-center anchored, drag-grip; transparent window painting a bar background + history
   chips; F2 console (selectable read-only multiline); toolbar icons (grip/×/gear/prompt-caret/
   expand) drawn via the draw list with `InvisibleButton` hit-testing; settings + recent-command
   popups. Classic docked panel preserved behind the toggle.
5. F2 toggles console; Ctrl+9 hides/restores bar; ESC unchanged (always cancels command).
6. View menu: "Command line" (Ctrl+9) + "Classic command dock" toggles.
7. `SetupMainDockLayout` reserves the bottom dock only in classic mode (no empty strip when floating).
8. Record submitted entries in `ProcessCommandLineSubmit` for the history dropdown.

## Tests
- `CommandLineTests` — `[commandline]`, 13 assertions, green. Full suite 211/27 green.
- Build: clean (GoSurvey + GoSurveyTests). App launches without crash.

## Assumptions
- ASSUMPTION-1: defaults fade=4s / 3 history lines / ~15 console lines (user-confirmed ⚙).
  Risk-if-wrong: cosmetic; all are live-tunable via the wrench popup. Validate-by: user review.

## Open / deferred (manual verification by user)
- F2 console autoscroll-to-bottom not implemented (read-only multiline); user can scroll.
- Footer clickable hints render below the input in floating mode (input not pinned to the
  very bottom while a command with hints is active). Acceptable; revisit if undesired.
- Toggling classic dock at runtime doesn't auto-rebuild the dock layout (use View → Reset layout).
