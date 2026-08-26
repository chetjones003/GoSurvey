# TASK-117 — ZOOMEXTENTS's own acceptance list, actually verified (REQ-122, GitHub issue #88)

- Type:    feature (plus one defect found while verifying)
- Status:  done
- Opened:  2026-08-26
- Owner:   Nathan Johnson

## 1. Authority
- Requirements: **REQ-122** — accepted 2026-08-26 by **D-2026-08-26-c**.
- Also honoured: REQ-120 (the gesture and the space branch, unchanged), REQ-045 (middle-drag pan
  unchanged), REQ-201 (a refusal states its reason), ADR-002 (the Catch2 target links no command
  translation unit — the constraint that shaped the whole approach).
- Acceptance: REQ-122's nine conditions, which restate GitHub issue #88's ZOOMEXTENTS list.
- Owning subsystem: `Commands`. No UI change — REQ-120 already owns the gesture.

## 2. Scope
- In scope: the framing arithmetic and its four guarantees; the hoist that makes them testable; the
  three call sites that consume it; the GUI pass for the halves a unit test cannot reach.
- Out of scope:
  - **what counts as the drawing's extents.** `ComputeRobustWorldExtents` and its outlier rejection
    are untouched. Issue #88 asks the implementation to "use the application's existing geometry
    bounds/extents system where possible", and it already does;
  - giving the headless harness a synthetic viewport (TASK-113 DEBT-1 — see §12, still open);
  - paper space framing anything but the sheet (REQ-120's stated limitation, unchanged).
- Smallest change: one file moved, one constant corrected, one guard added.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No.** One function became one function with the same job. It moved from
          `CadCommands.cpp` into `src/commands/ZoomFraming.hpp` — header-only and pure, which is the
          existing shape of `OrthoConstrain.hpp`, `ViewportPickPolicy.hpp`, `ColorContrast.hpp` and
          `SurfaceStyle.hpp`, all of them there for exactly this reason. No new type, no new field,
          no new caller, no dependency.
    - The move is not cosmetic: it is the whole reason the acceptance list can be *verified* rather
      than asserted. See §7.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Is #88's ZOOMEXTENTS list a testing gap or a behaviour gap? | 2026-08-26 | **Both.** Seven of the nine conditions held and were merely untested. One did not hold: degenerate extents framed at zoom ~4.6e6. Recorded in D-2026-08-26-c (a). |
| Q2 | Should the minimum-span floor apply to `ZOOMWINDOW` too, or only to `ZOOMEXTENTS`? | 2026-08-26 | **Both, through the one shared function.** Splitting it per caller would be the second copy of the arithmetic #88's Architecture section exists to forbid, and "never zoom to an unusable scale" is not a property only ZOOMEXTENTS should have. The cost is stated in REQ-122 rather than hidden: no view frames tighter than one world unit. |

## 5. Assumptions
```
ASSUMPTION-1: One world unit is a usable minimum framed span for this application.
- Because:       `zoom == 1` shows 100 world units, so one unit is 1% of the view the app opens
                 with — tight, but a scale a user can work at, and far above the float spacing of
                 the coordinates being drawn.
- Risk if wrong: a user who legitimately needs to inspect sub-unit detail cannot zoom closer,
                 through ZOOMEXTENTS or ZOOMWINDOW.
- Validate by:   measured in the GUI (§13). A degenerate ZOOM WINDOW — both corners on the same
                 pixel — landed on a 1.09-unit-tall view with the drawing centred and legible,
                 against ~1.1e-5 units under the old pad. A 0.1-unit feature still occupies 10% of
                 that view, so the floor does not hide sub-unit detail; it only stops the descent.
                 Open for the user/reviewer to overrule with a different constant — it is one
                 named value in one header.
```

## 6. Plan
- `src/commands/ZoomFraming.hpp` (new) — `zoomframing::FrameWorldRect`, the framing math, plus its
  three named constants (`kOrthoHalfHRef`, `kMarginFraction`, `kMinFrameSpan`). Returns `bool`:
  false means *nothing was written*.
- `src/commands/CadCommands.hpp` / `.cpp` — drop `ApplyViewportZoomToWorldRect`, include the new
  header, and let `ProcessPendingViewportZoom`'s two branches log a stated reason on refusal.
- `src/commands/CadCoordinateFrame.cpp` — `FitViewportToDrawing` returns false rather than fitting
  to a non-finite rectangle after an import.
- `tests/ZoomFramingTests.cpp` (new) + its `CMakeLists.txt` entry.
- Test approach: **Catch2 for the arithmetic, GUI for the state.** Every condition that is a claim
  about the camera given a rectangle is a unit test; the conditions that need a drawing, a
  framebuffer and a real gesture (empty drawing, parity with the typed command, middle-drag pan)
  are the GUI pass in §13.

- Steps:
  - [x] 1. Hoist the framing math into `ZoomFraming.hpp`, unchanged, and repoint the three callers.
  - [x] 2. Write the tests against the **visible rectangle**, not the formula.
  - [x] 3. Fix what they catch: the minimum span, and the non-finite refusal.
  - [x] 4. Prove them red against the old constants, then green.
  - [x] 5. GUI pass for the state-dependent half.

## 7. Workflow-specific notes
- The issue said this needed "either a manual GUI pass against each of the above, or a follow-up
  task that also gives the harness a synthetic viewport". It needed **neither**, and finding that
  out was the useful part of the recon. Both options assume the guarantees live in
  `ProcessPendingViewportZoom` — the function that is genuinely unreachable. They do not: margin,
  aspect, clipping, degenerate extents and NaN safety are decided entirely in the arithmetic
  underneath it, and that arithmetic has no framebuffer in it at all. Hoisting it into a header
  cost one file move and made five of the six open conditions ordinary unit tests. The synthetic
  viewport is still worth having one day (DEBT-1), but this issue did not need it.
- **Testing the visible rectangle, not the formula.** Every assertion derives
  `halfH = 50 / zoom`, `halfW = halfH * aspect` and asks the user's question — is the drawing
  inside, with room around it? A test written against `needHalfH` would have passed against the
  broken constant, because the constant was what it would have been reading.
- **Two of the tests were initially written against `kMinFrameSpan` itself** and passed under the
  old `1e-5` — a test that reads the constant it is checking proves nothing. Rewritten as absolute
  bounds (`at least half a world unit visible`), they went red. Kept as a note here because it is
  the failure mode this whole task exists to correct, repeated in miniature.

## 8. Implementation log
- 2026-08-26 Recon: read the framing function before planning anything. Seven of #88's nine
  ZOOMEXTENTS conditions were already true — the margin is real (8%, 4% a side), the aspect is
  really consulted, the empty case really refuses. The eighth was not.
- 2026-08-26 The degenerate defect, precisely: `kMinSpan = 1e-5` expanded a zero-span rectangle just
  enough to divide by, giving `needHalfH ≈ 1.09e-5` and `zoom ≈ 4.6e6`. At a local coordinate of ~86
  the float spacing is ~7.6e-6 — comparable to the entire view height, so the rendering at that zoom
  is quantization noise. "Well defined" and "usable" turned out to be different requirements, which
  is exactly what #88's "a minimum extent **or** minimum zoom level should be used" was getting at.
- 2026-08-26 The overflow case is not theoretical for a `double` API: `-1e308 .. 1e308` are both
  finite and their difference is not. Refused explicitly rather than left to the clamp.

## 9. Self-verification
- [x] build-project        — PASS (clean; MSVC /W4 /permissive-, no new warnings)
- [x] architecture-review  — PASS (§3; a header-only pure module beside four others of the same shape, and ADR-002's "the test target links no command TU" property is preserved, not worked around)
- [x] code-review          — PASS (one moved function, one corrected constant, one guard; the three call sites read better for handling a refusal explicitly)
- [x] dependency-audit     — n-a (no dependency added; the new header includes `<algorithm>` and `<cmath>`)
- [x] performance-review   — n-a (four `isfinite` checks on a once-per-gesture path)
- [x] testing              — PASS (§11)

## 10. Verification result
- Submitted:  2026-08-26
- Verdict:    PASS
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-122 (all nine acceptance conditions — met); REQ-120 (gesture and space
                          branch unchanged — met, and re-verified live in §13); REQ-045 (middle-drag
                          pan unchanged — met); REQ-201 (both new refusals state their reason — met)
- Tests added:            `tests/ZoomFramingTests.cpp` — 11 Catch2 cases, 231 assertions. **3 of the
                          11 proven red** against the old constants before the fix (the single-point
                          scale, the degenerate-extents floor, the non-finite refusal) and green
                          after. Full suite **622/622 ctest green** (611 before; 611/611 confirmed
                          green on `upstream/beta` before any change).
- Refactors:              `ApplyViewportZoomToWorldRect` → `zoomframing::FrameWorldRect`, moved into
                          `src/commands/ZoomFraming.hpp`. Three call sites updated. No behaviour
                          change from the move itself.
- Docs updated:           `spec/requirements.md` (REQ-122 added; REQ-120's Status amended to point at
                          it instead of describing an open gap; traceability row),
                          `spec/project.md` (D-2026-08-26-c)
- Done:                   2026-08-26

## 12. Technical debt
```
DEBT-1 (TASK-113) — UNCHANGED and still open, deliberately.
- What:      the headless driver models no framebuffer, so ProcessPendingViewportZoom (which
             early-returns on fbW <= 0) is still unreachable from any test target. No zoom
             TRANSCRIPT exists or can exist.
- Why it is not closed here: this task made the framing GUARANTEES testable, not the deferred
             consumer. What remains uncovered by automation is the plumbing around them — the
             space branch, the flag handshake, the gesture itself — which is what the GUI pass in
             §13 covers, as REQ-120's did.
- Remove by: unchanged — a REQ-203 harness extension adding a synthetic viewport and a ZOOM verb.
- Follow-up: still not filed. TASK-113 said "raise it if a third zoom requirement lands"; REQ-122
             is that third one, but it did not need the harness, which is itself an argument that
             the debt is smaller than it looked. Left for the reviewer to call.
```

## 13. GUI verification — 2026-08-26
Driven against the real window. Every number below is read off the **status-bar coordinate
readout** at a known screen position, not estimated from pixels: sampling two screen points gives
units-per-pixel on both axes and the camera centre, which is the whole camera to four decimal
places. Screenshots kept for the framing shots.

**Setup.** Lines `0,0–50,50` and `400,300–450,350`, plus a 1e-6-long line at `100,100` for the
degenerate case.

- **Empty drawing (acceptance: handled safely, no invalid camera).** `ZOOMEXTENTS` on the freshly
  opened empty drawing logged `ZOOM EXTENTS — nothing to frame.` and the readout was byte-identical
  before and after (`X 35.7214  Y 15.2256`). No camera written, nothing to write it from.
- **Degenerate extents (acceptance: a usable view).** With only the 1e-6 line present,
  `ZOOMEXTENTS` logged `span 0 x 0` and framed a view **1.09 world units tall**, centred on the
  geometry — measured, not estimated. The old pad would have framed ~1.1e-5 units. This is the
  condition the whole task turned on.
- **A real drawing (acceptance: centred, fits, margin, aspect, no clipping).** `span 450 x 350`.
  Probes at screen `(700,400)` and `(1600,900)` read `(-46.2095, 312.7436)` and
  `(467.1208, 27.5601)` → **0.570367 units/px on both axes** (so the aspect is honoured and pixels
  are square), a visible height of ~380 units for a 350-unit drawing (**8.0% margin**, height
  binding at this aspect), and a centre that lands on the viewport centre to within a pixel. Both
  lines visible with clear room at top and bottom; nothing clipped.
- **Parity with the gesture (acceptance: the same viewport result).** From a genuinely different
  view — a `ZOOMWINDOW` that had moved the camera to `(58.6053, 189.5632)` at the same probe point —
  a middle double-click reproduced `(-46.2095, 312.7436)` and `(467.1208, 27.5601)` **exactly**, to
  the last displayed digit, at both probes. Same function, same answer.
- **No pan state left behind (acceptance).** The two probes after the gesture involved moving the
  mouse 900 px between them; both read the canonical extents view. A stuck pan would have shifted
  the second.
- **Middle-drag still pans (REQ-045).** A middle drag of 200 px × 96 px moved the world under the
  cursor by `(+114.07, −54.76)` — exactly `200 × 0.570367` and `96 × 0.570367`. It panned, and did
  not zoom.
- **A drag is not a double-click (acceptance).** Two middle press-move-release cycles in immediate
  succession produced **two pans** (a total X shift of exactly 200 px worth) and left the view at
  `Y 257.9884`, not at the extents view's `312.7436`. The gesture is not triggered by fast repeated
  drags.
- **`ZOOMWINDOW` under the shared floor.** Both corners clicked on the same pixel — a zero-size
  window — framed a **1.09-unit-tall** view (0.0016296 units/px on both axes) rather than the
  ~4.6e6 zoom the old pad gave. The floor's stated cost, behaving as stated.
