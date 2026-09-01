# TASK-158 — Frame-stall watchdog for the multi-select freeze (issue #168)

- Type:    bug (investigation instrument — no fix yet)
- Status:  self-verify
- Opened:  2026-08-31
- Owner:   chetjones003

## 1. Authority
- Goal:         responsive interactive viewport
- Requirements: REQ-100 (frame budget — the property this watch measures a violation of),
  REQ-201 (make failures observable, don't fail silently)
- Constraints:  CON-07 (build reproducibility — unaffected)
- Acceptance (this task): a frame that overruns the stall threshold appends one diagnostic line to
  `%APPDATA%\GoSurvey\frame-watch.log` naming the active command, the selection size, the drawing
  counts and the per-subsystem millisecond split at the moment of the stall; a healthy frame writes
  nothing; a sustained multi-second hang writes its two edges (began / ended) and not one line per
  frame. Pure detector logic is unit-tested with no GL context.

## 2. Scope
- In scope: a header-only pure stall detector (`src/util/framewatch.hpp`), one instance in the
  `main.cpp` frame loop fed the existing `cmd.perfFrameMs`, and a best-effort file logger for the
  snapshot. Unit tests.
- Out of scope: **any behavioural fix for issue #168.** Root cause is not yet identified with
  evidence (the issue's "suspected areas" are explicitly the reporter's guesses), and the repo
  forbids a speculative fix. The issue stays open until a capture comes back.
- Out of scope: a watchdog *thread*. This detector runs on the main thread and cannot see a true
  never-returning infinite loop. That is a deliberate first step — if a confirmed freeze produces
  an empty log, the freeze is a genuine deadlock and a separate thread is the next task.
- Smallest change: reuse `cmd.perfFrameMs` (already measured at the top of the loop for PERFHUD);
  add no new timing, no new per-frame allocation, no new dependency.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / API / data-format / algorithm change?
  - [x] No — proceed. `framewatch.hpp` is a concrete free function over a plain aggregate, the exact
    shape and rationale of `util/hoverpickgate.hpp` and `util/hoverdwell.hpp` (§11.4 not engaged).
    The log file mirrors BENCH's `bench-req100.txt` precedent (investigation output to
    `UserDataDirectory()`). PERFHUD (issue #166) set the precedent for investigation instrumentation
    added without its own REQ.

## 5. Assumptions
```
ASSUMPTION-1: The freeze in issue #168 is a slow frame / multi-second recoverable stall, not a
              permanent deadlock.
- Because:       the two prior per-frame-cost bugs in this codebase (#166, #173) presented
                 identically — "app stops responding" that was actually ~2.4 s/frame — and the
                 issue itself says "intermittent … not every time", which a hard deadlock is not.
- Risk if wrong: the log stays empty after a confirmed freeze; we learn it is a true deadlock and
                 escalate to a watchdog thread. The instrument still cost almost nothing.
- Validate by:   the reporter reproducing one freeze on this branch and sending the log.

ASSUMPTION-2: A 350 ms threshold separates "the freeze the user means" from every legitimate heavy
              frame (file open, surface regen, first paint).
- Because:       a healthy frame is vsync-capped near 16 ms; the heaviest normal one-shots are tens
                 of ms; the issue describes the app needing a kill.
- Risk if wrong: either noise in the log (threshold too low) or a missed shorter stall (too high).
                 It is one constant in the header, trivially retuned.
```

## 6. Plan  (as executed)
- `src/util/framewatch.hpp` — `framewatch::FrameWatch` state + `FrameWatchTick(FrameWatch*, double)`
  returning a `Tick{event, stalledFrames, stalledMs, frameMs}`. Events: None / StallBegan /
  StallContinued / StallEnded. Non-finite input is treated as healthy (fail-open, like
  `HoverPickGateShouldRun`).
- `src/app/main.cpp`:
  - `#include "util/framewatch.hpp"`, `<fstream>`.
  - `AppendFrameWatchLog(const AppCommandState&, const framewatch::Tick&)` — static, formats the
    snapshot and appends to `frame-watch.log`. Best-effort: a failed open returns silently.
  - one `framewatch::FrameWatch frameWatch;` before the loop; `FrameWatchTick(&frameWatch,
    cmd.perfFrameMs)` right after the existing perf-frame block; log on `StallBegan` / `StallEnded`.
- `tests/FrameWatchTests.cpp` — 9 cases: healthy never trips, single slow frame open+close,
  sustained stall logs edges only, episode count/duration on the closing tick, threshold boundary
  (nextafter), non-finite never fabricates a stall, non-finite during a stall closes it (documented),
  null watch inert, custom threshold. Registered in `CMakeLists.txt` beside `HoverPickGateTests.cpp`.

## 7. Workflow-specific notes — Bug
- Root cause: NOT YET IDENTIFIED. Investigation to date (this branch's PR description has the full
  trace): the "cursor disappears" is the CAD crosshair (`ImGuiMouseCursor_None`, `CadUi.cpp:17129`)
  not being repainted while a frame is stalled — a symptom of the freeze, not a second bug. The
  per-frame selection-highlight build (`BuildSelectionHighlight`) and box-select preview are O(1) /
  O(selected); the known-expensive hover-pick scan is already rate-gated (#172). No infinite loop
  or O(n²) blow-up found on the plain click-accumulate or marquee path in a bounded read.
- Regression test: n/a — no behaviour changed. The unit tests pin the detector so a later refactor
  of the frame loop cannot silently disable it.

## 8. Implementation log
- 2026-08-31 open → plan → implement. Detector + loop hook + file logger + tests.
- 2026-08-31 build: clean (MSVC/Ninja release via `./dev/build`).
- 2026-08-31 tests: full suite 897/897 green; `[framewatch]` 9 cases / 627 assertions green.

## 9. Self-verification
- [x] build-project        — PASS (`./dev/build`, clean)
- [x] architecture-review  — PASS: helper mirrors hoverpickgate; log mirrors bench-req100.txt; no
      Workshop architectural decision
- [x] code-review          — PASS: single call site; best-effort I/O off the measured path (it runs
      only on an already-slow frame); no new per-frame allocation
- [x] dependency-audit     — PASS / n-a (no new dependency)
- [x] performance-review   — PASS: the tick is a compare + a few adds every frame; the file write
      happens only on a stalled frame (≤2 per episode), i.e. never on the hot path
- [x] testing              — PASS (happy + failure-mode: non-finite, null, threshold boundary)

## 11. Outcome
- Requirements satisfied: none closed — this is an instrument, not a fix. REQ-100 gains a field
  diagnostic for stalls it cannot currently attribute.
- Tests added: tests/FrameWatchTests.cpp (11 cases).
- Docs updated: this task log.
- Issue #168: stays OPEN, commented, awaiting a capture from a real freeze.
