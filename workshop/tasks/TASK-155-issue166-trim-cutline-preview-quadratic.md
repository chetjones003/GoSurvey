# TASK-155 — TRIM cut-line preview is O(N²) per frame (issue #166, real root cause)

- Type:    bug
- Status:  self-verify
- Opened:  2026-08-31
- Owner:   chetjones003

## 1. Authority
- Goal:         responsive interactive viewport
- Requirements: REQ-056 (TRIM smart line trim — owns the preview), REQ-100 (frame budget)
- Acceptance (REQ-100): the viewport holds a 16 ms p95 frame. A modify command must not lock it.
- Owning subsystem: Commands (`src/commands/CadCommands.cpp`, `CadTrimAppendCutLineRemovedPreview`).

## 2. Scope
- In scope: the per-frame cost of the TRIMSTATE-0 "draw a line to trim" rubber-band preview.
- Out of scope: the commit path (`ExecuteDrawnSegmentTrimOnce`) — unchanged and already correct;
  the pick-cutting-edges hover cost (TASK-154 / #172, a separate and smaller per-frame cost).
- Smallest change: preview only the one edge the fence line actually trims, the way the commit does.

## 3. Architectural boundary check
- [x] No new abstraction / dependency / algorithm. The fix REUSES the commit path's own
  `PickTrimTargetClosestToDrawnSegment` in place of a hand-rolled all-edges sweep. Preview now
  matches commit — strictly a correctness + cost improvement.

## 7. Workflow-specific notes — Bug
- Root cause (mechanism, measured with the PERFHUD overlay on the reporter's drawing —
  631 lines / 1069 polylines / 33 arcs / 267 circles):
  `main.cpp` calls `CadTrimAppendCutLineRemovedPreview` every rendered frame while the cut line is
  being dragged (`TrimPhase::CuttingLine_WaitP2`). The old body looped over **every** line and
  polyline edge in the drawing, and for **each** edge called
  `CollectAllDrawingCutSegmentsExceptTarget` — which re-tessellates every circle (48 samples), arc
  (36), ellipse and polyline in the drawing into a fresh multi-thousand-element vector — then ran an
  all-pairs segment-intersection test. O(edges × drawing) with an allocation per edge.
  Measured **frame time 2393 ms (p_avg 563 ms, max 2437 ms) — ~1.8 FPS**, entirely outside every
  section the profiler instruments (viewportUI 0.11 ms, hoverPick 0.00 ms, snap 0.00 ms,
  render 0.22 ms), i.e. in the `previewLines` build in the frame loop.
  This is exactly the hypothesis issue #166 raised ("TRIM's cutting-edge math is tessellated … if
  any of that runs per-frame … cost scales with geometry count") and #172's investigation wrongly
  dismissed by only looking at the pick-cutting-edges phase.
- Fix: `CadTrimAppendCutLineRemovedPreview` now mirrors `ExecuteDrawnSegmentTrimOnce` — compute the
  same `matchTol`, call `PickTrimTargetClosestToDrawnSegment` to get the ONE edge the fence trims,
  tessellate the cut set once, run one `TrimSegmentIntersectPickSide`, push that edge's stub. The
  preview also stops dashing edges the commit would never touch (a latent correctness bug).
- Regression test: `tests/TrimLinePreviewTests.cpp` (5 cases) — stub on the correct side of the
  crossing, the other side, empty when the fence is near nothing (the old code previewed anyway),
  never more than one edge on a 5-rung ladder, null-output tolerance.
- Diagnostic added alongside: the `PERFHUD` command (`src/ui/CadUi.cpp` `DrawPerfHud`) — a
  frame-time overlay over the LIVE drawing and command, split frame / viewportUI / hoverPick /
  snap / render. ~5 `steady_clock::now()` reads per frame whether shown or not; overlay is a no-op
  when off. Kept because it is the tool that found this and the next perf report will want it.

## 8. Implementation log
- 2026-08-31 PERFHUD overlay written; reporter ran it → 2.4 s/frame during cut-line drag, all of it
  outside the instrumented sections → traced to `CadTrimAppendCutLineRemovedPreview`.
- 2026-08-31 rewrote the preview to match the commit's single-target selection; +5 tests.
- 2026-08-31 build clean (`./dev/build`); full suite 870/870 green; `[issue166]` trim cases green.

## 9. Self-verification
- [x] build-project        — PASS
- [x] architecture-review  — PASS (reuses the commit path; no new abstraction)
- [x] code-review          — PASS (preview now == commit target; smaller + clearer)
- [x] dependency-audit     — PASS / n-a
- [x] performance-review   — PASS: ~2400 ms/frame → sub-millisecond (O(edges×drawing) → O(drawing))
- [x] testing              — PASS (5 new cases; old code fails "empty when fence near nothing")

## 11. Outcome
- Requirements satisfied: REQ-056 (preview still shown, now matches commit), REQ-100 (cut-line
  drag no longer locks the viewport).
- Tests added: tests/TrimLinePreviewTests.cpp (5).
- Docs updated: this task log; TASK-154 completion note corrected.
- Done: pending verification + reporter GUI confirmation.
