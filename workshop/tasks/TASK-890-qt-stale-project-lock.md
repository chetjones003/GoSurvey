# TASK-890 — Port stale project-lock takeover to Qt

- Type: feature
- Status: complete
- Opened: 2026-10-18
- Owner: Claude

## 1. Authority

- GitHub issue #890 (parent tracker #878); accepted **REQ-382** clauses 2-3 (second opener is told who holds the project and offered read-only / cancel; a stale lock can be taken over with an explicit warning).
- The ImGui `Kind::Locked` project prompt and shared `gsproj::TryAcquire` / `IsStale` / `TakeOver` are the 0.7.4 behavior reference.
- Issue acceptance requires a usable native Qt workflow, preserved cancel/failure/read-only semantics, focused workflow coverage, an updated parity ledger, and 2-3 screenshot options with the user's selected direction.

## 2. Scope

- Qt `OpenProject` currently offers only "Open read-only?" when the lock is held. Replace with a native dialog that names the holder and time, flags a stale lock, and offers Open read-only / Take over / Cancel.
- Move the Windows process-liveness check into `gsproj::ProcessIsRunning` so both frontends share one copy (two concrete uses).
- Out of scope: lock file format, stale rule, ImGui behavior.

## 3. Architectural boundary check

- New abstraction/dependency/data format? [x] No. One shared helper next to `IsStale`; UI stays in the Qt boundary.

## 4. Questions

| # | Question | Asked (date) | Answer |
|---|---|---|---|
| Q1 | Which integrated Qt visual direction should the lock dialog use? | 2026-10-18 | Option B — two choice cards (Open read-only recommended, Take over flagged). |

## 5. Plan

- [x] `gsproj::ProcessIsRunning`; ImGui switched to it.
- [x] Qt `AskLockedProject` dialog wired into `EditorWindow::OpenProject`.
- [x] Tests: `staleProjectLockOffersReadOnlyTakeOverAndCancel`, `lock: ProcessIsRunning...`.
- [x] Ledger + migration doc.
- [x] Screenshot options, user selection (B), final screenshots.

## 6. Verification

- `.uild.bat` (both frontends, Release) - PASS.
- `GoSurveyTestImGui.exe "[req382]"` - 21 assertions in 5 test cases PASS (includes the new `ProcessIsRunning` case).
- `staleProjectLockOffersReadOnlyTakeOverAndCancel` (Qt, hidden desktop) - PASS.
- Full `ctest --preset ninja-release` - 2642 of 2644 pass. Failures, neither touching project locks: `qt.editor` `tabsAppearInitiallyAndTypingRoutesFromEditorChrome` (`tree->hasFocus()`, same unrelated failure recorded in TASK-887) and `headless.regression-surfacedelete-selection` (surface selection invariant; no surface code changed here).
- Review against REQ-382 clauses 2-3 and the ImGui prompt: same stale rule (`gsproj::IsStale`), same three choices, same takeover warning for a live holder; no format or domain change.

## 7. Completion report

Qt now shows a native Project in use dialog (choice cards, option B chosen by the user) with Open read-only / Take over / Cancel and stale-lock detection. Screenshot: `build/artifacts/Release/qt-890-stale-lock.png`.
