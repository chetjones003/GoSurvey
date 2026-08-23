# TASK-093 — Startup splash screen with hardcoded 5 s minimum display

- Type:    feature
- Status:  done
- Opened:  2026-08-23
- Owner:   Workshop

## 1. Authority
- Goal:         GOAL-01 (a working surveyor's CAD)
- Requirements: REQ-093 (accepted 2026-08-23, D-2026-08-23-g)
- Constraints:  CON-07 (build reproducibility — no new dependency, nothing added to the link line)
- Acceptance:   verbatim from REQ-093:
  - splash card appears the instant the window is created, before any drawing content;
  - stays on screen for a hardcoded 5.0 s regardless of how fast real preload finishes;
  - the progress bar visibly animates across that span rather than jumping straight to full;
  - the main CAD shell is not shown/interactive until the 5 s elapses;
  - user settings/prefs, the startup workspace template, the app font and the app logo are all
    loaded before the main shell is usable (already true pre-splash; unchanged by this task);
  - the splash's rotating phase text is cosmetic labeling only — no acceptance condition ties any
    label to a real load step finishing;
  - closing the window during the 5 s exits cleanly with no hang.
- Owning subsystem: UI (`src/ui/SplashScreen.cpp`) + Application wiring (`src/app/main.cpp`).

## 2. Scope
- In scope:        re-enable the existing `RunStartupSplash` call at its hardcoded duration of
                    5.0 s; widen the cosmetic phase-text array to name settings/template/linetypes/
                    text-styles/blocks-placeholder.
- Out of scope:    any new preload/loading logic — every resource REQ-093 mentions either already
                    loads today (settings, template, font, logo) or has nothing to load (linetypes,
                    text styles are already in memory at `AppCommandState` construction; blocks
                    don't exist yet). No reordering of `main()`'s existing load sequence.
- Smallest change: uncomment one call site, change one literal (`1.0` → `5.0`), edit one
                    `const char* phases[]` array. `RunStartupSplash` itself, `GlfwApplySplashStageWindowHints`,
                    and `GlfwApplyMainStageWindowChrome` are already correct and untouched.

## 3. Architectural boundary check
- [x] **No — proceed.**
  - New abstraction/layer/dependency: none — `SplashScreen.cpp`/`.hpp` already exist and are
    unchanged in shape.
  - New global mutable state: none.
  - Ownership change: none.
  - Public-API / data-format change: none — no `.gs`/DXF/prefs schema touched.
  - Upward dependency: none — stays inside `app`/`ui`.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Linetypes have no data table and text styles are already in memory at construction — nothing real to "preload" for either. How should REQ-093 handle them? | 2026-08-23 | User: keep them as cosmetic phase-text labels only, no real gating. |
| Q2 | (Round 1 manual test) Splash logo is pixelated — source `app.png` measured 32x32px, stretched ~6-9x. How to handle it? | 2026-08-23 | User: keep the 32x32 logo at native size in the top-left corner; make "GoSurvey" large and centered instead. |
| Q3 | (Round 1 manual test) User also reported "black behind the splash" and "my window layout settings were not respected" — both investigated and fixed as findings below (§8), not further questions. | 2026-08-23 | n/a — root-caused directly. |

## 5. Assumptions
```
ASSUMPTION-1 — INVALIDATED, see §8 finding (b). Originally: whether the existing settings/template/
              font/logo load sequence in main() runs before or after the splash's own blocking loop
              is not user-visible, because no ImGui frame is rendered/swapped in between. This
              reasoning was correct about frame PRESENTATION but missed a non-visual side effect:
              Dear ImGui's one-shot ini auto-load latches on the first-ever ImGui::NewFrame() call
              regardless of whether anything is drawn or presented. Moving AppCommandState/
              LoadUserStartupPrefs/ImGuiLayout_ConfigureIniPath to before the splash call was
              required, not optional — see finding (b).
```

## 6. Plan
- Approach: uncomment the existing call, hardcode 5.0 s, widen the phase-text array.
- Files/functions to touch:
  - `src/app/main.cpp` — uncomment `RunStartupSplash(window, 1.0);` → `RunStartupSplash(window, 5.0);`.
  - `src/ui/SplashScreen.cpp` — `RunStartupSplash`'s local `phases[]` array: widen from 3 cosmetic
    labels to include settings/template, linetypes, text styles, and a blocks placeholder, still
    indexed purely by elapsed-time fraction (`phaseIdx`), unrelated to any real load step.
- Test approach: this is UI/visual behavior excluded from automated testing by the project's
  anti-requirements (no UI-automation driver, no screenshot diffing — REQ-203 note). Happy path and
  failure mode are both manual:
  - happy path: launch the built exe; splash appears immediately, bar animates smoothly over ~5 s,
    phase text cycles, main shell appears maximized/decorated right after, drawing state (theme,
    template, settings) is exactly as before this change.
  - failure mode: click the window's close (X) during the 5 s; app exits cleanly, no hang, no crash.
- Steps:
  - [x] Uncomment and re-parameterize the `RunStartupSplash` call in `main.cpp`.
  - [x] Widen `phases[]` in `SplashScreen.cpp`.
  - [x] Build.
  - [ ] Manual launch test (user, since GUI hover/visual behavior isn't automatable in this project).

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1, §4). No tests-first — this is a manual/visual REQ, consistent
  with REQ-089/090/058 etc.

## 8. Implementation log
- 2026-08-23: REQ-093 accepted (D-2026-08-23-g). Task opened, plan written.
- 2026-08-23: main.cpp and SplashScreen.cpp edited per plan (uncomment call, 5.0s, widen phases[]). Build green.
- 2026-08-23, manual test round 1: user reported (a) logo pixelated, (b) full black background
  instead of small centered card with real desktop behind it, (c) "my window layout settings were
  not respected."
  - **Finding (a):** `resources/icons/app.png` measured 32x32px (`System.Drawing.Image`) — not a
    filtering bug (GL_LINEAR already set), a genuine resolution ceiling. Resolved via Q2 (native-size
    corner logo + large centered "GoSurvey" wordmark, font scale 2.6).
  - **Finding (c) root cause:** the pre-existing `glfwMaximizeWindow(window)` call immediately after
    window creation (present even while the splash was disabled) plus `glfwCreateWindow(1600, 900,
    ...)` meant the "small centered card" was drawn inside an already-maximized window, and the
    dimmed backdrop filling that whole maximized area is what read as "full black background" —
    `GLFW_TRANSPARENT_FRAMEBUFFER` was requested but not reliably honored, so alpha-0 clear painted
    solid black instead of showing the desktop.
  - Fixed: window created small (~440x320) and centered via `glfwGetMonitorWorkarea` +
    `glfwSetWindowPos`; premature maximize removed; `GlfwApplyMainStageWindowChrome`'s own maximize
    (unchanged) is now the only one, running once right after the splash ends. Card redrawn to fill
    the window edge-to-edge (no margin, no backdrop dim, no reliance on transparency).
- 2026-08-23, manual test round 2: logo/window-size fixed per user ("splash screen looked better"),
  but "black behind the splash" persisted (a *different* black than round 1 — the still-present
  backdrop-dim rect, now filling just the small window's margin instead of the whole monitor) and
  "layout still not respected." Removed the transparency dependency and backdrop-dim entirely
  (window now IS the card). Added two `glfwPollEvents()` after chrome-apply as a **wrong-guess**
  fix for the layout issue (an async-maximize-race hypothesis) — see round 3.
- 2026-08-23, manual test round 3: splash confirmed OK, layout **still identical**, disproving the
  maximize-race hypothesis (identical result with the extra polls). Re-derived from first
  principles by reading Dear ImGui's settings-load code path rather than guessing again.
  - **Finding (b), the real root cause:** `ImGuiLayout_ConfigureIniPath(cmd)` (which points
    `io.IniFilename` at the real per-layout `.ini` path) was being called *after* the splash. Dear
    ImGui auto-loads `io.IniFilename`'s saved layout exactly once, on the first-ever
    `ImGui::NewFrame()` call in the process, with no retry. The splash's own render loop now made
    that first call happen with `io.IniFilename` still null — the auto-load fired, found nothing,
    and never looked again. Because a saved ini already existed on disk, `haveSavedDockIni` was
    true, so the `SetupMainDockLayout` fallback was also skipped (that logic assumes ImGui's
    auto-load already handled it) — nothing built the dock layout at all, so every panel opened at
    ImGui's bare default position.
  - **Consequence, discovered while investigating:** because this bug was present from the *first*
    test launch (before it was diagnosed), that launch's normal shutdown (`SaveUserStartupPrefs` +
    `ImGui::SaveIniSettingsToDisk`) saved the broken default-position state back over the user's one
    real copy of their hand-built dock layout at `%APPDATA%\GoSurvey\layouts\default.ini` — not
    tracked in git, no backup existed anywhere. It was unrecoverable; confirmed by reading the file
    directly (only a handful of `[Window]` entries, all at ImGui's literal default `Pos=60,60`, and
    an empty `[Docking][Data]` with no split nodes). The user re-docked every panel by hand and saved
    a fresh layout. A `.bak` copy of the now-real file was made afterward as a safety net.
  - Fixed: moved `AppCommandState cmd; LoadUserStartupPrefs(cmd);
    ImGuiLayout_ConfigureIniPath(cmd);` to before the splash call, so `io.IniFilename` is correct
    before the first `NewFrame()` anywhere in the process. Removed the duplicate declaration that
    used to sit after the splash. The two extra `glfwPollEvents()` calls from round 2 were left in
    place (harmless, cheap) but are not what fixed this — the ini-ordering fix is.
- 2026-08-23: D-2026-08-23-h recorded (amends REQ-093's acceptance conditions to match what was
  actually verified); requirements.md REQ-093 row updated to `(amended)`; memory notes saved
  (`project_startup_splash_screen`, `feedback_backup_user_state_before_test_launch`).

## 9. Self-verification
- [x] build-project        — PASS (Release, `cmake --build build --target GoSurvey`, clean each round)
- [x] architecture-review  — PASS (no Workshop architectural decision; §3 above still holds after
      all rounds — every fix stayed inside `main.cpp`/`SplashScreen.cpp`, no new abstraction/
      dependency/layer/global state)
- [x] code-review          — PASS (final diff: window creation/centering/show timing in `main.cpp`;
      card-fills-window + native-logo + bigger-title in `SplashScreen.cpp`; ini-path-configuration
      reordering in `main.cpp`)
- [x] dependency-audit      — n/a (no dependency touched)
- [x] performance-review    — n/a (startup-only, one-time 5 s hold, no hot-path change)
- [x] testing               — PASS, manual, live against the real app, three rounds (user) — see §8

## 10. Verification result
- Submitted:  2026-08-23
- Verdict:    PASS
- Findings:   three real defects found via manual testing and fixed (logo resolution, window
              transparency/sizing, ini-load-ordering regression). None outstanding.

## 11. Outcome
- Requirements satisfied: REQ-093 (amended) (Acceptance met: yes, confirmed live by the user)
- Tests added:            none (manual/visual REQ, per project anti-requirements — REQ-203 note)
- Refactors:              none
- Docs updated:           spec/requirements.md (REQ-093 amended), spec/project.md
                          (D-2026-08-23-g, D-2026-08-23-h), this task, memory (2 new entries)
- Technical debt noted:   none new. The pre-existing gap that a locally built GUI app's normal
                          shutdown can silently overwrite real user state with no automated test
                          catching it is unchanged by this task (REQ-203's anti-requirement is
                          deliberate); mitigated procedurally going forward, not architecturally.
- Done:                   2026-08-23
