# Test executables make no Windows sound calls

Authority: user request, 2026-11-09 ("remove the sound in all tests"). No SPEC requirement covers sound in test executables, so none applies. REQ-202 (a non-zero test exit fails the build) and REQ-203 (the headless transcript corpus runs on every push) are unchanged: exit codes still come from the test programs.

Cause, measured: the blocked-call log of one full `ctest -R "^qt[.]editor"` run recorded 54 `PlaySoundW` calls. Every one came from Qt's `QMessageBox::showEvent`, through `QAccessible::updateAccessibility` in `qwindows.dll`. GoSurvey code opened the message boxes, for example `EditorWindow::ShowProjectPointWarning`, `ShowPasteWarning` and `CreatePdfCoordinate`. The 183 headless ctest entries and the Catch unit shards recorded no sound calls.

Change: `tests/TestSoundGuard.cpp` and `tests/TestSoundGuard.hpp`. At process start the guard replaces the Windows sound entry points listed in `kBlocked` (`MessageBeep`, the `MessageBox` family, `PlaySound`, `sndPlaySound`, `mciSendString`, `waveOutOpen`, `Beep`, `Shell_NotifyIcon`) with counting stubs that return success without playing. Each stub overwrites the first 12 bytes of its entry point with `mov rax, imm64; jmp rax`. A replacement that cannot be installed stops the process at startup with exit code 3 and a message on standard error, so a run never continues with sounds that are not blocked. The effect is limited to the test process; other programs and the sound scheme are not touched. Setting `GOSURVEY_BLOCKED_SOUND_LOG` writes every blocked call with its call stack. Log paths are read and opened as UTF-16, and the log and standard error are written as UTF-8, so non-ASCII paths work. A log that cannot be opened leaves the log that is open unchanged. The guard is linked into `GoSurveyTests`, `GoSurveyQtTests` (`cmake/GoSurveyQtTests.cmake`) and `gosurvey_headless`. It is not linked into `GoSurvey.exe`.

Tests: `tests/TestSoundGuardTests.cpp` (`[test-sound]`, five cases):
- `MessageBeep` and `Beep` are blocked, and the blocked-call count rises by two.
- Every entry point is replaced when the process starts.
- `PatchEntryPoint` returns false for an address it cannot change. That return value is the failure that stops the process. The stop itself (`StopProcess`) cannot run inside the test process, so it is covered by reading the code and by that return value.
- The log opens at a path with non-ASCII characters and records the call. The environment log is reopened afterwards.
- A log that cannot be opened is reported, the log that was open stays open, and sounds stay blocked.

## PR review (PR #1186)

Collected from `gh pr view 1186 --comments`, the inline review comments and the review list, on commit 11d7341b. The only automated code review is PR-Agent's (one guide plus two inline findings). CodeRabbit posted a "review skipped" notice (auto reviews are disabled); that is not a finding, and no review was triggered by an agent.

1. [F1] Silent failure (PR-Agent, `tests/TestSoundGuard.cpp:130-134`). When `VirtualProtect` failed, the original sound function stayed callable, installation continued, and the log said `replaced`. Fixed: `PatchEntryPoint` returns a bool; `Install` stops the process on any failed step (module load, export lookup, patch); only completed replacements are logged.
   Class check, every Win32 call whose failure changes behaviour: `VirtualProtect` first call (returns false), `VirtualProtect` restore (reported), `FlushInstructionCache` (reported), `LoadLibraryW` and `GetProcAddress` (stop), `CloseHandle` (reported in `CloseLogLocked`), `WriteFile` to the log (reported once), `WriteFile` to standard error (best effort, documented: the debugger copy is always written and a stopped process fails through its exit code), `SymInitialize` (reported), `GetModuleHandleExW` in `DescribeFrame` (the frame is logged as `unknown`).
   Self-check item that would have caught it: error paths.
2. [F2] Non-ASCII path (PR-Agent, `tests/TestSoundGuard.cpp:138-143`). `GetEnvironmentVariableA` and `CreateFileA` fail or corrupt paths outside the ANSI code page, so logging was silently disabled. Fixed: wide APIs for the variable, the log path and the module file name; UTF-8 for the log and standard error. A test opens a log at a path with non-ASCII characters. End to end, the non-ASCII path appears intact in the standard error message.
   Self-check item that would have caught it: locale and unit handling (list every narrow Win32 call and the text it carries).

Self code review (inline, against `spec/coding-standards.md`, `workshop/implementation-rules.md` and `CLAUDE.md`):

3. [S1] `OutputDebugStringA` carried log paths, the same class as F2. Fixed: `ToWide` and `OutputDebugStringW`.
   Self-check item: locale and unit handling.
4. [S2] `CloseHandle` was called twice and its result was never checked. Fixed: one helper, `CloseLogLocked`, reports failures.
   Self-check item: error paths; duplicated code.
5. [S3] The write-failure message said later calls "are not logged", which was false: writes are still attempted, and only the first failure is reported. Fixed: the message now says further write failures are not reported.
   Self-check item: error paths (each message must say what really happens).
6. [S4] The patch is x64-only, with no compile-time check. Fixed: `static_assert(sizeof(void*) == 8, ...)`.
   Self-check item: boundary values and architecture assumptions.
7. [S5] Magic number `32768` for the path buffer. Fixed: `kMaxWindowsPathLength`.
   Self-check item: boundary values.
8. [S6] `ignored` is an unclear name for the second `VirtualProtect` output. Fixed: `protectBeforeRestore`.
   Self-check item: naming (Mysterious Name).
9. [S7] The result of `OpenBlockedCallLog` is ignored in `OpenLogFromEnvironment`. Kept on purpose: the failure is reported inside the function and sounds stay blocked. The comment says so.
   Self-check item: error paths.
10. [S8] Globals were named `gLog`, `gBlockedCalls` and so on, but `coding-standards.md` §5 and `CLAUDE.md` name statics `s_Name`. Renamed to `s_` names. Constants keep the `kName` style used by the `constexpr` constants in `tests/`; the §5 table's PascalCase row is not followed anywhere in the repo.
    Self-check item: naming (standards).
11. [S9] No test covered the unopenable-log path, which is a new public failure path. Added.
    Self-check item: error paths (each failure path has a test).
12. [S10] Found by running the shard's own order (`--order lex`), not by reading the code. A failed open closed the log that `GOSURVEY_BLOCKED_SOUND_LOG` had opened, so calls made later in the same process were not logged. Fixed: a failed open keeps the current log, and the non-ASCII test reopens the environment log when it ends (`RestoreEnvironmentLog`).
    Self-check item: test independence (no test leaves shared state behind for another).

Self-check re-run on the final diff: error paths (every failure reaches a return value, a report or a stop); boundary values (the 32767-character path limit, an empty or unset variable, the 12-byte patch); locale and unit handling (the only narrow calls left are `GetProcAddress` with export names, which are ASCII, and `SymFromAddr` symbol names, which the frame log already tolerates); permission guards (the `VirtualProtect` failure); undo steps (not applicable: the guard has no undo); REQ acceptance criteria (none apply; the authority is the user request).

## Documentation review (REQ-453)

The change is test-only, and no user-facing text changed, so the review from the first commit still holds:
- `README.md`: the test commands and layout are unchanged. No update needed.
- `PRIVACY.md`: no related content. No update needed.
- `resources/wiki` (in-app help): the only match is the application setting "Beep on error in user input", which this change does not touch. No update needed.
- Public GitHub wiki (`chetjones003/GoSurvey.wiki`): the same setting is the only match. No update needed.

## Verification

On the commit that holds these fixes (the working tree matched it at each step):
- `build.bat` (Release) built with exit 0 twice, before and after the log-order fix. No warnings from the guard files.
- `GoSurveyTests.exe "[test-sound]"`: all tests passed (16 assertions in 5 test cases), exit 0. Run in lexicographic order, the order the shard runner uses, with `GOSURVEY_BLOCKED_SOUND_LOG` set: three blocked calls were logged (`MessageBeep`, then `MessageBeep` and `Beep`) with call stacks. Run in declaration order: the same three calls, and the environment log was reopened after the non-ASCII test and logged the call that follows it.
- `ctest -R "^(headless[.]|unit[.]catch)"` with `GOSURVEY_BLOCKED_SOUND_LOG` set: 185/185 passed, exit 0. The log holds three blocked calls (two `MessageBeep`, one `Beep`), all made by the `[test-sound]` cases. Every other sound-entry call in these shards was absent.
- MSVC `/analyze` with `/WX` (`.github/scripts/run_msvc_analyze.py`, analysis tree `build/.cmake/analyze`, `--compiler-launcher ""`): exit 0 for the four guard translation units. This is the local guard-only subset. CI analyzes the full project because the branch changes CMake inputs relative to `origin/beta`; CI's result is the authority.
- clang-tidy (`tools/run-clang-tidy-diff.ps1 -BaseSha 54e10a52 -BuildDirectory build\.cmake\tidy`, run on the committed files): exit 0 for the three guard files, with no user-code warnings.
- `ctest -R "^qt[.]editor"` (six shards, `qt.editor.shard-0` to `shard-5`): passed, 6/6 shards, exit 0, 775 s of test time. The blocked-call log holds 54 `PlaySoundW` calls, and every call stack includes a `QMessageBox` frame.

Note on the documented command: `ctest --preset ninja-release -R "^qt[.]editor$"` (CLAUDE.md, build and test table) matches no test. The Qt editor suite is named `qt.editor.shard-N`, so the working filter is `-R "^qt[.]editor"`. The documented command should be corrected.

Not verified by ear. The blocked-call log is the evidence.

## Known gaps

- `GoSurvey.exe` is not guarded. The `qt.deployment` test runs `GoSurvey.exe --smoke-test` from a clean copy (`tools/verify-qt-deployment.ps1`) through the hidden-desktop runner, so that test is not covered, and whether it makes sounds has not been measured.
- Sounds made without these entry points (DefWindowProc beeps, WinRT toasts) are not covered. The logs from the test runs show none.
- The stop-at-startup path (`StopProcess`) is not exercised inside a test run; see Tests.

## Assumptions and technical debt

- The patch assumes x64 calling conventions and entry points at least 12 bytes long. The project builds for x64 only, and the `static_assert` checks the pointer size. The 12-byte length is not checked against this machine's exports.
- The patch is process-local. The tests still open their message boxes, now silently, on the hidden desktop.
- The environment log is shared by the cases in one process. The non-ASCII test restores it, and the unopenable-path test leaves it unchanged.
- Naming: statics use `s_Name` per the coding standards; constants keep `kName`, which the repo uses throughout (176 `constexpr` names in `tests/`).
