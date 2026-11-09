# Test executables make no Windows sound calls

Authority: user request, 2026-11-09 ("remove the sound in all tests"). No SPEC requirement covers sound in test executables, so none applies. REQ-202 (a non-zero test exit fails the build) and REQ-203 (the headless transcript corpus runs on every push) are unchanged: exit codes still come from the test programs.

Cause, measured: the blocked-call log of one full `ctest -R "^qt[.]editor"` run recorded 54 `PlaySoundW` calls. Every one came from Qt's `QMessageBox::showEvent`, through `QAccessible::updateAccessibility` in `qwindows.dll`. GoSurvey code opened the message boxes, for example `EditorWindow::ShowProjectPointWarning`, `ShowPasteWarning` and `CreatePdfCoordinate`. The 183 headless ctest entries and the Catch unit shards recorded no sound calls.

Change: `tests/TestSoundGuard.cpp` and `tests/TestSoundGuard.hpp`. At process start the guard replaces the Windows sound entry points listed in `kBlocked` (`MessageBeep`, the `MessageBox` family, `PlaySound`, `sndPlaySound`, `mciSendString`, `waveOutOpen`, `Beep`, `Shell_NotifyIcon`) with counting stubs that return success without playing. Each stub overwrites the first 12 bytes of its entry point with `mov rax, imm64; jmp rax`. The effect is limited to the test process; other programs and the sound scheme are not touched. Setting `GOSURVEY_BLOCKED_SOUND_LOG` writes every blocked call with its call stack. The guard is linked into `GoSurveyTests`, `GoSurveyQtTests` (`cmake/GoSurveyQtTests.cmake`) and `gosurvey_headless`. It is not linked into `GoSurvey.exe`.

Tests: `tests/TestSoundGuardTests.cpp` (`[test-sound]`) checks that `MessageBeep` and `Beep` go through the guard and that the blocked-call count rises by two.

Verification:
- `build.bat` (Release) builds all three test executables. The build log shows no warnings from the new files; the only warnings are the existing OpenXLSX ones.
- `ctest -R "^qt[.]editor"`: 6/6 shards passed. 54 blocked calls logged, as above.
- `ctest -R "^(headless[.]|unit[.]catch)"`: 185/185 passed. The only blocked calls are the two made by the `[test-sound]` test.
- Not verified by ear. The blocked-call log is the evidence.

Documentation review (REQ-453):
- `README.md`: the test commands and layout are unchanged. No update needed.
- `PRIVACY.md`: no related content. No update needed.
- `resources/wiki` (in-app help): the only match is the application setting "Beep on error in user input", which this change does not touch. No update needed.
- Public GitHub wiki (`chetjones003/GoSurvey.wiki`, master at f56fbf6e): the same setting is the only match. No update needed.

Known gaps:
- `GoSurvey.exe` is not guarded, so the `qt.deployment` smoke run, which starts the shipped application, is not covered.
- Sounds made without these entry points (DefWindowProc beeps, WinRT toasts) are not covered. The logs from the test runs show none.

Assumptions and technical debt:
- The patch assumes x64 calling conventions and entry points at least 12 bytes long. The project builds for x64 only.
- The patch is process-local. The tests still open their message boxes, now silently, on the hidden desktop.