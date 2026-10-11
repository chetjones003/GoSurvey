# TASK-054 — BUG-013: ask for the discrete GPU, and let the user hand it back

- Type:    bug
- Status:  done — fixed, measured, and verified end to end in the running application
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-100** (frame budget on the reference machine — the budget was being measured,
  and met, on a device the spec does not name); **REQ-201** (no silent failures — applies to the new
  setting, which reports rather than fails quietly)
- Decisions:    user, 2026-08-15 — (1) fix it **and** add a setting to opt back to the integrated
  GPU for battery life; (2) REQ-100 is judged on the RTX 5060, with the integrated-GPU figures kept
  as a documented floor, **both recorded separately**. Recorded in `project.md`'s decision log.
- Constraints:  architecture §11.3 (no new global mutable state), REQ-300 (no dependency — these are
  OS imports), REQ-301 (no speculative abstraction)
- Owning subsystem: `platform/` (the Win32 registry call) + `app/` (the exported symbols) + `ui/`
  (one checkbox) + `io/` (one persisted bool). No domain, renderer or command logic changes.

## 2. Root cause

**GoSurvey expressed no GPU preference at all, so Windows chose — and chose the integrated GPU.**

On a hybrid laptop the driver looks for two exported symbols to decide whether an application wants
the discrete part: `NvOptimusEnablement` (NVIDIA) and `AmdPowerXpressRequestHighPerformance` (AMD).
GoSurvey exported neither, so the decision fell to Windows' heuristics, which are free to answer
differently between launches and answered "integrated" on the reference machine.

Evidence, in the order it was gathered (TASK-053 §8):

| # | Observation |
|---|---|
| 1 | The new mesh bench profile failed at 21.40 ms, reproduced at 21.45 and 21.84 |
| 2 | The **unchanged** line profile measured 9.27 ms at 21:21 and 13.13 ms at 22:39 — same binary, same scene, so the variable was not in the code |
| 3 | Not thermal: 4 minutes idle changed nothing, GPU at 44 °C. Not process state: a fresh process was equally slow. Not the visual style: the ribbon read "2D Wireframe" afterwards |
| 4 | `nvidia-smi` reported the RTX 5060 at **0% utilisation and 12 W** *while the benchmark ran*, and the process's 3D-engine load sat on the other adapter |
| 5 | Forcing the discrete GPU: 9.27 → **1.38 ms**. The mesh profile 21.40 → **1.97 ms** |
| 6 | The source exports neither symbol — confirmed by search, which is the cause rather than a correlate |

## 3. Fix

Two mechanisms, because the decision asked for a default *and* an override, and they are different
kinds of thing:

1. **The default — the exported symbols** (`src/app/main.cpp`). Read by the driver at process start,
   so they must be exported (the linker would otherwise drop two variables nothing references) and
   they cannot change while the application runs. This is what makes a fresh install, with no
   registry state anywhere, land on the discrete GPU.
2. **The override — Windows' own per-application preference** (`src/platform/GpuPreference.{hpp,cpp}`),
   the same `HKCU` setting the Settings → Display → Graphics page writes, keyed by the executable's
   path. Deliberately Windows' mechanism rather than one of our own: it is the one Windows actually
   consults, the user can see and change it outside GoSurvey, and it leaves an inert registry value
   behind rather than behaviour with no visible source.

The second overriding the first is **not assumed — it is measured** (§5). Had it not, the setting
would have been a checkbox that did nothing, and the honest fix would have been to drop the exports
and drive both states from the registry.

Files:

| File | Change | Why |
|---|---|---|
| `src/app/main.cpp` | the two exported symbols | the root cause, directly |
| `src/platform/GpuPreference.{hpp,cpp}` | read/write the per-app preference | the override the decision asked for; `platform/` already owns OS calls (`HttpFetch`, `ProcessRun`) |
| `src/commands/CadCommands.hpp` | `systemPreferIntegratedGpu` + the message string | beside `systemHardwareAcceleration`, whose box it shares. No new global — it is state on the existing object |
| `src/io/UserPrefs.cpp` | load + save that bool | one line each, the established pattern |
| `src/ui/CadUiSettings.cpp` | checkbox in the Hardware Acceleration box; box 110 → 160 | where a user looks for it. The height was raised because the checkbox was clipped mid-word — found by looking at it, not by reading the code |
| `CMakeLists.txt` | the new source, in both targets | |

## 4. Regression tests

`tests/GpuPreferenceTests.cpp` — 3 cases, 14 assertions:
round-trip of both preferences; clearing removes the value rather than recording a third state;
setting twice is idempotent. They write only the **test** executable's own key and restore it, which
was verified: GoSurvey's own preference was untouched across a suite run.

**What no test can cover, stated rather than implied.** Which GPU a driver hands the process is not
observable without a GL context, real hardware and an external instrument, so the exports themselves
are verified by measurement (§5), not by assertion. A test that appeared to cover it would be
worse than none. What the tests do cover is the half that can regress silently: a typo in the
property string or the value name would leave the checkbox looking like it worked.

## 5. Verification — measured, not asserted

Every combination run on the reference machine, `nvidia-smi` as the instrument, `BENCH 250000` as
the workload:

| exports | Windows preference | GPU used | p95 |
|---|---|---|---|
| (before the fix) | none | integrated | 9.27–13.13 ms |
| yes | none — the shipped default | **discrete**, 20% / 20 W | **1.46 ms** |
| yes | `GpuPreference=1` (the setting, checked) | **integrated**, 0% / 14.7 W | 12.42 ms |
| yes | `GpuPreference=2` (the setting, unchecked) | **discrete** | 1.38 ms |

So the exports fix the default and the setting genuinely overrides them — the checkbox does what it
says. End-to-end through the real UI: opening Settings → System and clicking the checkbox wrote
`GpuPreference=1;`, unchecking wrote `GpuPreference=2;`, and the dialog reported "Saved. Takes effect
the next time GoSurvey starts."

- [x] build-project        — PASS, clean.
- [x] architecture-review  — PASS. New module sits in the subsystem that already owns OS calls; no
      new global mutable state (the two fields live on `AppCommandState`); no layer, dependency or
      data-format change. `winreg` is an OS import, not a dependency (REQ-300 does not apply, same
      reasoning ADR-029 recorded for WinHTTP).
- [x] code-review          — PASS. The value is parsed by searching for `GpuPreference=` rather than
      assuming the property list's layout — the machine's own pre-existing entry for another
      application carries `AppStatus=` instead, which a positional parser would have misread.
- [x] testing              — PASS. 3 new cases; full suite **332/332**.
- [x] performance-review   — the point of the task: 6–9× on every scene measured.

## 6. Outcome

- Root cause fixed at the cause, not the symptom: the application now *asks*, where before it
  accepted whatever it was given.
- **User-visible:** every hybrid-laptop user gets the discrete GPU, and can hand it back for battery
  life. Given most field laptops are hybrid, this is likely the largest single performance change
  the project has shipped — and it was never reported, because slowness does not look like a bug.
- Tests added:      `tests/GpuPreferenceTests.cpp` (3 cases, 14 assertions)
- Technical debt:   none added. One limit worth knowing: the setting cannot take effect until the
  next launch, which the UI states plainly rather than papering over with a restart prompt.
- Docs updated:     TRACKER BUG-013; REQ-100 (both devices recorded); `project.md` §7 + decision
  log; `spec/roadmap.md` risk table; this log.
- Done:             2026-08-15
