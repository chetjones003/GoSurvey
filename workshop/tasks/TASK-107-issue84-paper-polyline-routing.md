# TASK-107 — Issue #84: paper-space POLYLINE commits to the MODEL store

- Type:    bug
- Status:  done — fixed, tested, and verified against the unfixed binary
- Opened:  2026-08-25
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-039** (paper-space objects have full model-space parity), Acceptance
                (5) "CIRCLE, ARC, ELLIPSE, POLYLINE, and MTEXT draw onto the sheet" and
                (6) "none of these paper edits change model geometry" — both already `accepted`.
- Constraints:  REQ-301 (no new abstraction) — the fix must reuse `CommitRectangle`'s existing
                shape, not invent a new one.
- Acceptance:   a POLYLINE (open, via END, or closed, via CLOSE) drawn while a paper layout is
                active is committed to that layout's `paperPoly*` stores; the model `userPolyline*`
                stores are unchanged; switching back to Model space does not surface the geometry.
- Owning subsystem: Commands (`CommitPolylineDraft` owns the commit).

## 2. Bug report (issue #84, filed by nrjohnson2604 2026-08-25)

| # | Observed | Expected |
|---|----------|----------|
| 1 | `POLYLINE` drawn with a paper Layout tab active (status bar reads PAPER) appears on the paper canvas at draw time, then reappears in **model space** the moment the user switches to the Model tab | the polyline stays paper geometry, like every other paper-space draw command |
| 2 | Both `END` and `CLOSE` exhibit this — same commit function, both paths | neither path touches the model store while paper space is active |
| 3 | Control case: `RECT` in the same layout stays correctly in paper space | POLYLINE should match RECT's behavior — same underlying storage shape (REQ-053) |

Reproducer: `tests/headless/transcripts/regression-84-paper-polyline-routing.txt` (new).

## 3. Root cause (evidence, not hypothesis)

`src/commands/CadCommands.cpp`, `CommitPolylineDraft` (originally :20428, now :20428 pre-fix):

```cpp
static void CommitPolylineDraft(AppCommandState& st, bool closed, std::vector<std::string>& log) {
  ...
  PushUndoSnapshot(st, closed ? "Polyline (closed)" : "Polyline");
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  const int baseVert = st.userPolylineOffsets.back();
  st.userPolylineVerts.insert(...);          // <-- unconditional MODEL store write
  ...
}
```

No `ActivePaperGeometryTarget(st)` branch anywhere in the function — it always writes
`st.userPolyline*`, regardless of which space is active. Confirmed by inspection against its
working sibling `CommitRectangle` (:15204), which branches on `ActivePaperGeometryTarget` and
writes `L->paperPoly*` when a paper layout is active (:15220-15232) — the two functions build the
same storage shape (REQ-053: a polyline is `verts`/`offsets`/`closed`/`attrs`; RECT is just a
fixed-4-vertex, always-closed instance of the same shape) and are the natural pair to compare.

### Sibling audit — scoped to POLYLINE only

`SubmitPolylineVertex` (:20497) and `StartPolylineCommand` (:14678) were both read end to end.
Neither needs a paper branch: `StartPolylineCommand` only sets command-mode state (identical in
shape to `StartLineCommand`, which also has none), and `SubmitPolylineVertex` only accumulates
into `st.polylineDraftVerts`, a space-agnostic scratch buffer that both `CommitRectangle`'s
sibling (`SubmitLineVertex`, which DOES branch) and `CommitPolylineDraft` read at commit time.
`CommitPolylineDraft` is confirmed as the single place that decides where the geometry lands,
matching the issue's own conclusion.

Issue #84 also lists CIRCLE (`CommitCircle`), ARC (`CommitArcThreePoints`), and ELLIPSE
(`FinishEllipseFromRatio`) as unconfirmed leads sharing the same cause. **Not touched here** —
out of scope per the issue's own framing ("I did not manage to reproduce... reporting them as a
lead"); left for a separate issue/task.

## 4. Architectural boundary check (workflow.md §4)

- New abstraction / layer / dependency / ownership change / global / public API / data-format
  change?
    - [x] **No — proceed.** One `if (PaperLayout* L = ActivePaperGeometryTarget(st))` branch,
      the exact shape `CommitRectangle` already uses at the line above it in the same file.

## 5. Assumptions

None beyond what's stated in §3 — no ambiguity surfaced.

## 6. Plan

- `src/commands/CadCommands.cpp` — give `CommitPolylineDraft` the `ActivePaperGeometryTarget`
  branch, writing `L->paperPolyVerts/Offsets/Closed/Attrs` (paper branch) vs.
  `st.userPolyline*` (existing else branch), matching `CommitRectangle`'s structure exactly.
- Add test infrastructure the transcript needs (none existed to switch space or assert paper
  polyline counts from a transcript): a `LAYOUT NEW|MODEL` verb in `HeadlessDriver.cpp` (the same
  kind of REQ-203 gap `CLIPCOPY` already documents — `AddPaperLayout`/`SetActiveSpace` are
  UI-tab-bound and otherwise unreachable from a transcript) and an `EXPECT PAPERPOLYLINES <n>`
  check alongside the existing `EXPECT POLYLINES <n>`.
- New transcript `regression-84-paper-polyline-routing.txt`: switch to a new paper layout, draw
  one open (END) and one closed (CLOSE) polyline, assert `POLYLINES 0` / `PAPERPOLYLINES 1` then
  `2` after each, and that switching back to Model doesn't change either count.

## 7. Steps
- [x] sibling audit (§3)
- [x] regression transcript written, proven to fail against the unfixed binary
- [x] the fix
- [x] full suite

## 8. Implementation log

- 2026-08-25 — sibling audit first (§3): read `SubmitPolylineVertex` and `StartPolylineCommand`
  end to end before touching anything; confirmed `CommitPolylineDraft` is the sole decision point.
- 2026-08-25 — wrote `regression-84-paper-polyline-routing.txt` and the two driver additions
  (`LAYOUT` verb, `EXPECT PAPERPOLYLINES`) it needs. Built `gosurvey_headless` against the
  unfixed code and ran it: **fails** — `EXPECT POLYLINES 0` at step 8, "got 1" (geometry landed in
  the model store as reported).
- 2026-08-25 — applied the fix: `CommitPolylineDraft` now branches on `ActivePaperGeometryTarget`,
  writing to the layout's `paperPoly*` stores when paper space is active and to `st.userPolyline*`
  otherwise — same shape as `CommitRectangle`. Rebuilt; transcript now **passes** (21 steps).
- 2026-08-25 — full regression: 541/541 unit tests (`GoSurveyTests`), 50/50 headless transcripts
  (1 pre-existing `DISABLED`) including the new one, `fuzz-smoke` clean. `GoSurvey` (the full GUI
  app) also rebuilt clean — no new warnings from either changed file.

## 9. Self-verification

- [x] build-project       — PASS. `gosurvey_headless`, `GoSurveyTests` (unchanged, no rebuild
      needed), and `GoSurvey` all clean; no new warnings introduced by either changed file.
- [x] architecture-review — PASS. One branch reusing `ActivePaperGeometryTarget`, the exact
      pattern `CommitRectangle` already establishes. No new abstraction, layer, dependency,
      global, public API, or data-format change. The `LAYOUT` test-driver verb is infrastructure
      only (mirrors the existing `CLIPCOPY` precedent for a UI-bound action), not production code.
- [x] code-review         — PASS. Fixes the root cause (the missing branch), not a symptom;
      mirrors the sibling exactly rather than inventing a new shape.
- [x] dependency-audit    — PASS. None added.
- [x] performance-review  — n/a. Same commit, one extra branch already paid for by
      `ActivePaperGeometryTarget`'s existing use elsewhere in the same function family.
- [x] testing             — PASS. New transcript proven red-before/green-after; full suite green
      (541 unit + 50 headless + fuzz-smoke).

## 10. Verification result

- Submitted: 2026-08-25
- Verdict:   **PASS**
- Findings:  none outstanding.

## 11. Outcome

COMPLETION REPORT — TASK-107 — 2026-08-25
- Requirements satisfied:  REQ-039 (Acceptance (5)/(6) met for POLYLINE: yes)
- Summary:                 `CommitPolylineDraft` had no paper-space branch and always wrote the
                           model `userPolyline*` stores. Gave it the same
                           `ActivePaperGeometryTarget` branch `CommitRectangle` already has, so a
                           polyline drawn on an active paper layout commits to that layout's
                           `paperPoly*` stores instead.
- Tests:                   `headless.regression-84-paper-polyline-routing` (new), proven to fail
                           against the unfixed binary. Full suite: 541/541 unit,
                           50/50 headless (+1 pre-existing disabled), fuzz-smoke clean.
- Verification verdict:    PASS (findings resolved: none)
- Assumptions:             none
- Architectural decisions: none made by Workshop.
- Dependencies:            none added
- Technical debt noted:    none — CIRCLE/ARC/ELLIPSE (issue #84's unconfirmed leads) are explicitly
                           out of scope; a follow-up issue should confirm and fix them the same way
                           if reproduced.
- Build:                   reproducible, clean on target platform (MSVC, ninja, vcvars64)
- Docs updated:            this log; `tests/headless/HeadlessDriver.cpp` inline comments
- Done:                    2026-08-25
