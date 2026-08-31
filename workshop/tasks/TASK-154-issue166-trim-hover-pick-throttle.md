# TASK-153 — Throttle the per-frame viewport hover pick (issue #166)

- Type:    bug
- Status:  self-verify
- Opened:  2026-08-31
- Owner:   chetjones003

## 1. Authority
- Goal:         responsive interactive viewport
- Requirements: REQ-056 (TRIM smart trim — owns the "UI hover gate"), REQ-100 (frame budget)
- Constraints:  CON-07 (build reproducibility — unaffected)
- Acceptance (REQ-056, verbatim relevant clause): "While picking cutting edges (and trim targets),
  entity picking uses the existing hover highlight and pick … hovering an object while picking
  cutting edges highlights it; picked edges stay highlighted." Still met — the highlight is
  unchanged, only its refresh cadence is bounded.
- Acceptance (REQ-100): the viewport holds a 16 ms p95 frame. TRIM must not blow that budget with
  per-frame CPU work the idle state does not pay.
- Owning subsystem: UI (`src/ui/CadUi.cpp`) + a pure helper in `src/util/`.

## 2. Scope
- In scope: bound how often the viewport entity hover pick (`PickClosestCadEntity` +
  `PickCadAnnotationAt` + `PickCadTableAt` + `PickFilledRegionAt`) re-runs. It currently runs on
  every rendered frame the cursor is over the viewport — idle and through the
  TRIM/EXTEND/BREAK/LENGTHEN entity-selection phases (REQ-056).
- Out of scope: spatial indexing / broad-phase pruning of the pick itself (larger change, arguably
  an algorithm the spec did not specify); the "line-trim" phases, which do no entity hover.
- Smallest change: a movement + rate gate at the single call site, reusing the previous frame's
  result when the cursor, view and geometry are unchanged.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / API / data-format / algorithm change?
  - [x] No — proceed. The helper is a concrete free function over a plain aggregate (the
    `util/hoverdwell.hpp` precedent, same §11.4 reasoning). No new dependency. The pick algorithm
    is unchanged; only its call cadence is. The hover result is semantically identical — a highlight
    that can lag a moving cursor by up to ~33 ms, which REQ-056 does not constrain.

## 5. Assumptions
```
ASSUMPTION-1: A hover highlight that refreshes at ~30 Hz while the cursor sweeps, and immediately
              on any cursor stop / view change / geometry change, is indistinguishable to the user
              from one that refreshes every frame.
- Because:       REQ-056 requires the highlight to appear on hover but says nothing about latency.
- Risk if wrong: a barely-perceptible lag on the hover glow while dragging the cursor fast.
- Validate by:   the reporter's own GUI pass on the branch (same manual-verification route as the
                 original report).
```

## 6. Plan  (as executed)
- `src/util/hoverpickgate.hpp` — `HoverPickGate` state + `HoverPickGateShouldRun(...)` predicate.
  Pure (`<cmath>`/`<cstdint>`), so `GoSurveyTests` links it with no GL context.
- `AppCommandState::viewportHoverPickGate` — one field beside `viewportHoverEntity`.
- `CadUi.cpp` — wrap the existing `if (!blockEntityHover) { … }` hover-pick block: run it only when
  the gate says to; otherwise leave `viewportHoverEntity{,Valid}` (which already persist across
  frames) untouched. Reset the gate when hover is blocked.
- `tests/HoverPickGateTests.cpp` — 9 cases: fresh-run, still-cursor-does-not-spin, moving-capped-at-
  ~30 Hz, sub-pixel jitter, drift accumulation, view change, revision bump, backward clock, null gate.
- Gate parameters at the call site: moveTol 1 px, min re-run interval 1/30 s while moving, idle
  ceiling 0.25 s (safety net for a UCS change / layer freeze not folded into the view/revision key).

## 7. Workflow-specific notes — Bug
- Root cause (mechanism): the hover pick is an O(entities) linear scan (arcs 36-sampled, ellipses
  sampled, polylines segment-walked). `main.cpp` renders unconditionally (`glfwPollEvents` + always
  draw), so on a dense drawing the scan is a fixed per-frame CPU cost that directly lowers the frame
  rate. It runs during TRIM's `SelectCuttingEdges`/`SelectTrimTargets` (and the EXTEND/BREAK/
  LENGTHEN equivalents) because REQ-056 deliberately keeps hover feedback on for those — the one
  command family that does. Every other command suppresses hover, which is the "full frame rate"
  baseline the report compares against. The issue's other suspicion — that `CollectCutSegments`
  tessellation runs per frame — is **not** the cause: `BuildTrimCutSegments` is only reached from
  `SubmitTrimViewportPick`, i.e. on click, never per frame.
- EXTEND / BREAK / LENGTHEN: confirmed they share the exact same per-frame block and are fixed by
  the same gate (the investigation ask).
- Regression test: `HoverPickGateTests` — "A still cursor … does not re-run every frame" fails
  against the unpatched logic (which has no gate at all).

## 8. Implementation log
- 2026-08-31 open → plan → implement. Helper + field + call-site gate + tests.
- 2026-08-31 build clean (MSVC/Ninja release via `./dev/build`); full suite 858/858 green;
  `[issue166]` filter 9 cases / 23 assertions green.

## 9. Self-verification
- [x] build-project        — PASS (`./dev/build`, clean)
- [x] architecture-review  — PASS (no Workshop architectural decision; helper mirrors hoverdwell)
- [x] code-review          — PASS (single call site, previous behavior preserved on the run path)
- [x] dependency-audit     — PASS / n-a (no new dependency)
- [x] performance-review   — PASS: still cursor drops from 1 scan/frame to ≤4 scans/s; sweeping
      cursor capped at ~30 scans/s regardless of refresh rate. No new per-frame allocation.
- [x] testing              — PASS (happy + failure-mode, green; regression fails pre-patch)

## 11. Outcome
- Requirements satisfied: REQ-056 (Acceptance met: yes — highlight unchanged), REQ-100 (per-frame
  hover cost during TRIM no longer scales with refresh rate).
- Tests added: tests/HoverPickGateTests.cpp (9 cases).
- Docs updated: this task log.
- Done: pending verification + reporter GUI pass.
