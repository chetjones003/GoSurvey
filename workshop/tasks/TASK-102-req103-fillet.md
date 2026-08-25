# TASK-102 — FILLET modify command (REQ-103 step 6a)

- Type:    feature
- Status:  done
- Opened:  2026-08-24
- Owner:   Claude (agent)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         REQ-103 — Modify-command completeness (GOAL per spec/project.md D-2026-08-23-j)
- Requirements: REQ-103 (step 6a of 8, FILLET) — must be `accepted` (it is)
- Constraints:  REQ-101 (±0.01 ft tolerance, analytic not tessellated), REQ-062 (curveisect
  library), REQ-201 (no silent failures), REQ-076/ADR-027 (stable entity ids, never rolled back
  on undo)
- Acceptance:   restated verbatim from `spec/requirements.md` REQ-103 "Acceptance — FILLET (step
  6a)" — full text there, not duplicated here. Summary: two picked curves (Line, non-full Arc, or
  an open/closed Polyline segment under the Case A adjacent-vertex / Case B end-segment rules) get
  a tangent arc constructed between them via an offset-and-intersect solve (reusing
  `curveisect`/`OffsetCmd`'s existing analytic primitives), then each curve is trimmed/extended to
  its own tangent point by reusing LENGTHEN's `ApplyLengthenToLine`/`ApplyLengthenToArc`/
  `ApplyLengthenToPolylineEnd` unchanged; radius-0 and parallel-lines-semicircle are real special
  cases, not simplifications; radius and trim-mode are persisted settings
  (`filletRadius`/`cornerTrimMode`, app-level like `TRIMSTATE`); full model+paper-space parity;
  loops per-target until Enter/Esc, one undo step per fillet.
- Owning subsystem: Commands (`src/commands/CadCommands.{hpp,cpp}`), UI wiring
  (`src/ui/CadUi.cpp`), preview (`src/viewport/TransformPreview.cpp`), viewport click routing
  (`src/viewport/ViewportPickPolicy.hpp`), persisted settings (`src/io/UserPrefs.cpp`), headless
  test driver (`tests/headless/HeadlessDriver.cpp`) — per spec/architecture.md, same subsystems
  MIRROR/LENGTHEN/EXTEND/BREAK/STRETCH were built in.

## 2. Scope
- In scope: model-space + paper-space FILLET; Line/Arc/Polyline-segment eligibility per Case A/B;
  the offset-and-intersect tangent-arc geometry solve (new); radius-0 and parallel-lines special
  cases; Trim/No-trim toggle (`cornerTrimMode`, shared with the follow-up CHAMFER task); persisted
  `filletRadius`/`cornerTrimMode` settings; full integration checklist (ribbon, typed command,
  right-click repeat, context menu, viewport click routing, TransformPreview highlight); headless
  transcripts (including at least one `CLICK`-driven transcript per this session's own instruction
  to verify mouse-click routing, not just `PICK`); unit tests for the tangent-arc geometry.
- Out of scope: CHAMFER (TASK-103, follow-up, reuses this task's Case A/B polyline-segment
  addressing and trim-apply plumbing); ARRAY/EXPLODE (steps 7-8, not started); a general
  "system-variable registry" (considered and explicitly deferred as debt — see D-2026-08-24-g in
  `spec/project.md` for why: `UserPrefs.cpp` has ~20 settings in `trimState`'s exact ad-hoc shape
  already, so generalizing now means either an inconsistent half-migration or an out-of-scope
  rewrite of unrelated settings); a live drag-updating radius preview beyond the static
  post-computation arc preview (nice-to-have, not required by the accepted acceptance text).
- Smallest change: new geometry is a small set of free functions beside `OffsetCmd`/LENGTHEN's
  existing ones (offset-and-intersect center solve, tangent-point extraction), reusing
  `curveisect::IntersectSegSeg/IntersectSegConic/IntersectConicConic` and `OffsetCmd::
  UnitLeftNormal/SignedSideLine/SignedSideCircle/LineLineIntersectInf` unchanged; the actual
  trim/extend mutation reuses `ApplyLengthenToLine/Arc/PolylineEnd` unchanged by converting a
  tangent point into the `newLength` those functions already accept (the exact reuse chain
  REQ-103's own sequencing note names); new `FilletPhase` state follows the same per-command
  phase-field pattern every prior REQ-103 step used, not a shared/generalized state machine.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed. No new entity kind (fillet arcs are ordinary `Arc` entities, chamfer lines
      will be ordinary `Line` entities); no new dependency; no new persisted data-format (two new
      `AppCommandState` fields persisted through the existing `gosurvey-user.json` pipeline,
      following `trimState`'s exact precedent — the "build a registry instead" idea was
      considered and explicitly declined as out of scope for this task, recorded in
      D-2026-08-24-g, not silently skipped); the new tangent-arc geometry functions have 2+
      concrete callers from day one (model apply, paper apply) like every prior step's helpers.
      Polyline-segment addressing (new `FilletSegPick`-style struct, analogous to `BreakPoint::
      segIndex`/`TrimTargetEdge`) is new but narrowly scoped, not a shared abstraction — it has
      exactly the callers this task and TASK-103 need, matching the precedent that neither of its
      two analogues is shared either.
    - [ ] Yes → STOP.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | REQ-103 bundles step 6 as "FILLET/CHAMFER" — build both together, or FILLET alone? | 2026-08-24 | Both, together (delivered as two task files, TASK-102/TASK-103, back-to-back) |
| Q2 | Eligible curves: Line/Arc only, +Circle, or +open-Polyline segments? | 2026-08-24 | Line, Arc, and open-Polyline segments |
| Q3 | Always trim/extend, or an AutoCAD-style Trim/No-trim toggle? | 2026-08-24 | Trim/No-trim toggle |
| Q4 | Model-space only (TRIM's own precedent), or full paper-space parity? | 2026-08-24 | Full paper-space parity |
| Q5 | CHAMFER input: Distance/Distance only, or also Distance/Angle? | 2026-08-24 | Both modes |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: the tangent-center disambiguation rule (among every candidate produced by offsetting
both curves ±radius and intersecting) is "minimize summed squared distance to the two pick
points" — not specified by REQ-103's own text, which only says corner generation happens "between
two entities."
- Because:     every other REQ-103 step's own ambiguity tie-break (LENGTHEN's nearest endpoint,
               EXTEND's nearest boundary hit, BREAK's position-ordering) resolves the same way:
               whichever interpretation is closest to what the user actually clicked. This is the
               direct generalization of that same convention to "closest to both clicks at once."
- Risk if wrong: a pick made far from the intended corner (e.g., near the far end of a long line)
               could resolve to an unexpected candidate on a geometry with multiple real tangent
               solutions (concave arc-arc cases especially). Written into the acceptance text
               (spec/requirements.md) as a stated, deterministic rule, not left implicit.
- Validate by:  unit-tested directly against hand-computed cases (Line-Line, Line-Arc, Arc-Arc),
               the same way STRETCH's arc-recompute formula was pinned before being trusted.

ASSUMPTION-2: a full-circle-sweep Arc is excluded from FILLET the same way Circle is (no single
tangent-side construction distinguishes a closed loop's "which side did you mean").
- Because:     REQ-103's own LENGTHEN/EXTEND acceptance already excludes full-circle Arc for a
               related reason (no free end); Circle's exclusion from FILLET was chosen directly by
               the user (Q2 above, "Line, Arc, and open-Polyline segments" — Circle was the
               explicitly NOT-chosen option). Treating a full-circle Arc identically to Circle is
               the consistent reading of that same choice, not a new one.
- Risk if wrong: a user filleting against what is visually a "circle drawn as an Arc" gets a
               refusal instead of the corner they wanted. Low — this is an unusual way to draw a
               circle in the first place, and the refusal message states the reason (REQ-201).
- Validate by:  stated explicitly in the acceptance text; revisit only if a real workflow needs it.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach:
  1. **Geometry module** (`src/commands/CadCommands.cpp`, beside `OffsetCmd`/LENGTHEN's helpers —
     free functions, not a new header/namespace, matching this file's existing convention of
     colocating REQ-103 helpers near LENGTHEN's own "written for reuse" block):
     - `OffsetLineForFillet`/reuse `OffsetCmd::UnitLeftNormal` directly for a Line's ± offset.
     - Reuse `curveisect::MakeCircle` (not `MakeArc`) for an Arc's full-circle offset-by-radius
       (concentric, `r ± radius`), matching `FindExtendArcTarget`'s own reasoning for using the
       full circle.
     - `SolveFilletCenter(...)` — given two curves' representations (Seg or Conic, radius, both
       pick points), builds every offset combination (±radius each side), intersects every pair via
       existing `curveisect::IntersectSegSeg/IntersectSegConic/IntersectConicConic`, returns the
       candidate minimizing summed squared pick distance (ASSUMPTION-1). Handles radius=0 as a
       direct fall-through (offset-by-0 intersection == original-curve intersection, no special
       casing needed in this function itself).
     - `TangentPointOnLine(...)`/`TangentPointOnArc(...)` — foot-of-perpendicular / ray-from-center
       constructions described in the acceptance text.
     - `ParallelLineFilletSemicircle(...)` — the special case, independent of the solve above.
     - Hand-verify each against a worked numeric example (quarter-circle-style, matching STRETCH's
       own precedent) before trusting it — write as unit tests FIRST (`tests/FilletGeomTests.cpp`,
       Catch2, registered in `CMakeLists.txt`, mirroring `StretchGeomTests.cpp`'s placement/shape),
       covering Line-Line (perpendicular and oblique), Line-Arc, Arc-Arc, radius-0, and the
       parallel-lines semicircle case.
  2. **Trim/extend apply** — read `FindExtendLineTarget`/`FindExtendArcTarget`'s bodies first to
     copy their exact tangent-point-to-`newLength` conversion technique (do not re-derive blind);
     write `ApplyFilletTrimToLine/Arc/PolylineEnd` thin wrappers that compute `newLength` from a
     known tangent point and `nearFirst`, then call `ApplyLengthenToLine/Arc/PolylineEnd` unchanged.
  3. **Polyline-segment addressing** — new struct (segment index + which-entity-index), populated by
     a pick resolver analogous to `ClosestPointOnEntity`'s segment-finding for `BreakPoint::
     segIndex`; Case A (same-polyline adjacent segments) rewrites the shared vertex via the same
     CSR-shifting technique `ReplacePolylineVerts`/BREAK's `AppendNewPolyline` already established;
     Case B (polyline end-segment vs. a different entity) reuses `ApplyLengthenToPolylineEnd`
     directly, no new polyline mutation needed.
  4. **Command state machine** — `Kind::Fillet`, `FilletPhase{WaitFirstEntity, WaitSecondEntity}`,
     latched first-pick entity+point (mirroring `lengthenPendingEntity` style); `R`/`T` mode-letter
     handling in `ProcessCommandLineSubmit` mirroring LENGTHEN's `TryLengthenModeToggle` shape;
     `filletRadius`/`cornerTrimMode` new `AppCommandState` fields + `UserPrefs.cpp` load/save lines
     (D-2026-08-24-g: NOT a registry, matching `trimState`'s exact existing shape).
  5. **Model-space apply** — `HandleFilletViewportPick`, dispatched from `SubmitViewportPickImpl`
     exactly where EXTEND/BREAK are (`if (st.active == K::Fillet) {...}`); one `PushUndoSnapshot`
     immediately before mutation, `BumpCadGpuCache` after, matching every prior step's convention.
  6. **Paper-space apply** — `ApplyFilletToPaperEntities`-equivalent, fed by a paper geometry
     extraction glue layer mirroring `AppendPaperBoundaryShapes`'s existing precedent (storage-agnostic
     duplication accepted by EXTEND already); `paperFilletPhase` + latched first pick, paper
     click-block wiring in `CadUi.cpp` mirroring EXTEND/BREAK's paper phase blocks.
  7. **Full integration checklist** (per the research report's item 8 table): `RibbonIconKind::
     Fillet` + vector-art case + icon name; new 5th ribbon column (Move's existing 4-column Modify
     layout is full, per the explicit warning comment at `CadUi.cpp:2538-2540` — do not stack into
     an existing column); autocomplete icon map; typed-command registry entry (`fillet`/`f`) +
     dispatch; `RepeatLastCommand` case; `ViewportPickPolicy.hpp`'s compile-enforced switch (new
     `case K::Fillet:` — likely `RawEntityPick` for both phases, since FILLET never takes a bare
     coordinate); context-menu entry; `TransformPreview.cpp` selection/hover highlight for the
     latched first pick, plus a live preview of the computed fillet arc once the second entity is
     hovered (nearest precedent: LENGTHEN's inline dynamic-arc preview block).
  8. **Headless transcripts** — at least one `CLICK`-driven transcript (this session's explicit
     instruction: verify mouse clicks reach the command through `ViewportClickRouteFor`, the exact
     mechanism TASK-099 built after five commands silently missed it), plus `PICK`-driven coverage
     for radius-0, parallel-lines-semicircle, Line-Arc, Arc-Arc, Trim/No-trim toggle, and the
     Case A/B polyline paths; `SAVEAS`/`UNDO`/`REDO` round-trip per existing transcript convention.
- Files/functions to touch:
  - `src/commands/CadCommands.hpp`: `Kind::Fillet`, `KindName` case, `FilletPhase` + latched-pick
    fields, `filletRadius`/`cornerTrimMode` fields, paper-space `paperFilletPhase` + latched fields,
    new polyline-segment-pick struct, geometry function declarations if needed cross-file (likely
    all `CadCommands.cpp`-local, matching LENGTHEN/EXTEND's own helpers).
  - `src/commands/CadCommands.cpp`: geometry module, `ApplyFilletTrimTo*` wrappers, polyline Case
    A/B mutation, `StartFilletCommand`, `HandleFilletViewportPick`, paper-space equivalents,
    `ProcessCommandLineSubmit` dispatch + R/T mode-letter handling, `RepeatLastCommand` case.
  - `src/io/UserPrefs.cpp`: load/save lines for `filletRadius`/`cornerTrimMode`.
  - `src/ui/CadUi.cpp`: ribbon icon+column, footer/context menu, autocomplete map, paper click-block
    wiring, Escape-phase reset.
  - `src/viewport/ViewportPickPolicy.hpp`: `case K::Fillet:`.
  - `src/viewport/TransformPreview.cpp`: selection/hover highlight, live fillet-arc preview.
  - `tests/FilletGeomTests.cpp` (new), `tests/headless/transcripts/fillet-*.txt` (new),
    `CMakeLists.txt` (register the new test file).
- Test approach: unit tests pin the tangent-arc geometry numerically (written and hand-verified
  BEFORE the command plumbing, same order STRETCH used) for Line-Line/Line-Arc/Arc-Arc/radius-0/
  parallel-semicircle; headless transcripts cover the full command flow in both spaces, including a
  `CLICK`-driven one per this session's explicit instruction; full `ctest` regression run before
  self-verification.
- Steps:
  - [x] Spec update (REQ-103 FILLET+CHAMFER acceptance, decision log D-2026-08-24-g)
  - [x] Open this task file
  - [x] Read `FindExtendLineTarget`/`FindExtendArcTarget`/`ApplyLengthenTo*`/`OffsetCmd` bodies in
        full before writing any geometry code
  - [x] Implement + hand-verify `SolveFilletCenter`/tangent-point functions; wrote
        `tests/FilletGeomTests.cpp` first, confirmed green before continuing (8 cases, 38 assertions)
  - [x] Implement `CadCommands.hpp` state additions (Kind, phases, persisted fields)
  - [x] Implement model-space apply (Line/Arc/Polyline Case A/B) + dispatch wiring
  - [x] Implement paper-space apply + click-block wiring
  - [x] Implement `UserPrefs.cpp` persistence
  - [x] Implement `CadUi.cpp` full integration checklist
  - [x] Implement `TransformPreview.cpp` highlight (live drag-preview of the computed arc descoped —
        see Technical debt in §11; not required by the accepted acceptance text)
  - [x] Write headless transcripts (PICK + CLICK), hand-verify expected geometry
  - [x] Build all three targets; run new tests/transcripts; run full `ctest` regression (583/583)
  - [x] Self-verify (§9); write completion report (§11)

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1-Q5 above via AskUserQuestion); tests-first for the geometry
  module specifically (matching STRETCH's precedent for exactly this reason: novel formula math
  that "is wrong in ways no screenshot reveals").

## 8. Implementation log  (append as you work)
- 2026-08-24 — Task opened; spec updated (REQ-103 FILLET+CHAMFER acceptance, D-2026-08-24-g). Two
  Explore research passes completed first (TRIM/EXTEND/LENGTHEN/BREAK infrastructure survey, then a
  follow-up on polyline-segment addressing / OFFSET's offset math / TRIMSTATE's persistence shape)
  before any acceptance text was written — findings folded into this plan and into the acceptance
  text's own reasoning (D-2026-08-24-g's rationale column).
- 2026-08-24 — Geometry module written first (`FilletCurve`, `FilletCandidateCenters`,
  `SolveFilletCenter`, `FilletTangentPointOnLine/OnCircle`, `FilletArcTangentPointToNewLength`,
  `FilletLinesAreParallel`, `FilletParallelSemicircle` — all inline in `CadCommands.hpp`, mirroring
  `RecomputeArcFromEndpoints`'s existing precedent for unit-testability). `tests/FilletGeomTests.cpp`
  written and hand-verified BEFORE any command plumbing (8 cases: perpendicular Line-Line tie-break,
  radius-0 collapse, Line-Circle and Circle-Circle external tangency, CW/CCW arc angle-to-length
  conversion, parallel-line detection, the semicircle construction) — all green before continuing,
  matching STRETCH's own precedent for novel geometry.
- 2026-08-24 — Model-space command plumbing written (`FilletEligibility`, `BuildFilletCurveFromEntity`,
  `ApplyFilletTrimSingle` reusing `ApplyLengthenToLine/ToArc/ToPolylineEnd` unchanged,
  `ApplyFilletPolylineCorner` for Case A using `ReplacePolylineVerts`, `StartFilletCommand`/
  `HandleFilletViewportPick`/`HandleFilletText`), dispatch wiring (`ProcessCommandLineSubmit`,
  `RepeatLastCommand`, the blank-Enter "finished" handler, `ViewportPickPolicy.hpp`'s compile-enforced
  switch), and two headless transcripts (`fillet-lines-basic.txt` — CLICK-driven per this session's
  own instruction to verify real click routing, plus PICK-driven radius-0/No-trim parts;
  `fillet-polyline-corner.txt` — Case A, plus the identical-segment-twice refusal).
- 2026-08-24 — First transcript run found a real bug, root-caused before patching: FILLET pushed
  THREE undo snapshots per apply (its own explicit "Fillet" push, plus one more from EACH of the two
  reused `ApplyLengthenToLine/ToArc/ToPolylineEnd` calls, which each self-push "Lengthen" — correct
  for LENGTHEN's own single-entity-per-call shape, wrong once FILLET calls them twice as one atomic
  operation). One UNDO therefore only partially reverted a fillet (one curve back to normal, the
  other still trimmed, arc gone) — violating REQ-103's own "each individual fillet is its own undo
  step." Fixed by adding an opt-out `bool pushUndo = true` parameter to all three
  `ApplyLengthenTo*` functions (default preserves LENGTHEN's/EXTEND's existing behavior exactly,
  zero risk to that already-shipped code), with FILLET's three call sites passing `false`. Root-
  caused via the UNDO/REDO round-trip check itself, not discovered by inspection.
- 2026-08-24 — Second transcript run found a second real bug: `ApplyFilletTrimSingle` decided which
  endpoint of a Line/Arc moves by nearness to the PICK point (copying LENGTHEN's own convention
  verbatim), but FILLET's correct rule is nearness to the TANGENT POINT — LENGTHEN's pick directly
  names the end to change, while FILLET's pick only disambiguates which tangent-arc candidate to
  build; the endpoint that actually moves is whichever is closer to where the arc will meet the
  curve, independent of exactly where along the kept portion the user clicked. A pick at a line's
  own midpoint (equidistant from both ends) exposed this: the pick-based tie-break moved the WRONG,
  far endpoint, which happened to collapse the line ("that change would collapse the line to zero
  length; refused"), root-caused via `--print-log` rather than guessed. Fixed by comparing
  `NearerToFirstPoint` against the computed tangent point instead of the pick in both the Line and
  Arc branches (Polyline's own nearFirst is segment-index-derived, not pick-distance-derived, and
  was never affected); `pickX,pickY` dropped from `ApplyFilletTrimSingle`'s signature since nothing
  in it needs the pick anymore.
- 2026-08-24 — `ApplyFilletPolylineCorner` (Case A) originally logged the same "polyline corner
  filleted." message regardless of radius, unlike Case B's distinct "trimmed to a point (radius 0)."
  — caught by the polyline transcript's own radius-0 part, not a geometry bug (ARCS/POLYLINES counts
  were already correct), just an inconsistent user-facing message between the two cases. Fixed by
  giving Case A the same radius-0-vs-normal distinction.
- 2026-08-24 — Paper-space FILLET written in full (`PaperFilletEligibility`,
  `BuildFilletCurveFromPaperEntity`, `ApplyFilletTrimSingleToPaperEntity` reusing the existing,
  already-undo-clean `ApplyLengthToPaperEntityMutation` shared helper — no `pushUndo` parameter
  needed there since that function never pushed its own snapshot to begin with,
  `ApplyFilletPolylineCornerPaper` using `ReplacePaperPolylineVerts`), declared in the header so
  `CadUi.cpp`'s paper click-block can reach it, and wired into the block's ESC-reset, hover-
  suppression, and two new phase branches (mirroring EXTEND/BREAK's own paper phase blocks exactly).
- 2026-08-24 — Full `CadUi.cpp` integration checklist completed: `RibbonIconKind::Fillet` (inserted
  before `Traverse` so the `g_ribbonIconTex` array's `[Traverse+1]` sizing formula grows to cover it
  automatically, per the research report's own warning), a hand-drawn vector-art icon (two
  perpendicular legs joined by a rounded corner — AutoCAD's own FILLET glyph shape), icon-name
  lookup, autocomplete map entry, a new 5th Modify-ribbon column (the 4th — Extend/Break/Stretch —
  was already full per the existing code comment warning against a 4th item clipping silently),
  and a "Basic Modify Tools" context-menu entry.
- 2026-08-24 — Full build (GoSurvey, gosurvey_headless, GoSurveyTests, all clean, no new warnings
  beyond this file's pre-existing exceptions-disabled `try`/`catch` diagnostics, unrelated to this
  task) and full `ctest` regression: 583/583 passed (1 pre-existing disabled test,
  `dxf-export-stable`, unrelated), including both new transcripts and all 8 new
  `FilletGeomTests.cpp` cases.
- 2026-08-25 — First hand-driven GUI pass found two real bugs (D-2026-08-25-a, full root-cause
  detail there): (1) `FilletCandidateCenters`'s Line-Line offset-intersection did its "huge segment"
  arithmetic in float32, losing ~0.01-0.02 units of precision for non-axis-aligned geometry (every
  prior unit test used axis-aligned lines, which never exercised this) — fixed with a new exact
  double-precision `FilletLineLineIntersectInf`, plus a regression test pinned to the user's own
  reported coordinates. (2) The floating command bar's fuzzy-autocomplete popup could silently
  rewrite a typed sub-answer (e.g. "r" for Radius) into an unrelated top-level command name before
  submission — a pre-existing, general command-bar bug this task's own R/T prompts were the first to
  surface visibly — fixed by gating the autocomplete rewrite on `cmd.active == Kind::None`. 589/589
  regression green after both fixes. A third report ("radius 100 should refuse, not draw a wrong
  arc") was deliberately left unimplemented pending the user's retest, since it may have been a pure
  symptom of bug (1) rather than a missing feature — see D-2026-08-25-a.
- 2026-08-25 — The user retested the third report with a smaller-but-still-oversized radius and
  confirmed it was a real, separate bug, not a symptom of (1) — full detail, including two false
  starts before the correct fix, in D-2026-08-25-b. Root cause was in candidate SELECTION, not
  validation: `SolveFilletCenter`'s plain nearest-to-picks tie-break chose a geometrically wrong
  offset-line candidate outright for a large radius, because the correct "inside the corner"
  candidate is pushed far from the picks by construction while a wrong one can end up numerically
  closer. Fixed with `FilletPointOnKeptSide`, a same-side-as-the-pick filter applied BEFORE the
  tie-break, plus a genuine "radius too large" refusal (`FilletPointWithinSpan`, a parametric
  overshoot check — is the tangent point past the curve's own far endpoint, not merely nearer to
  it). 591/591 regression green, including a transcript that specifically re-tests a VALID radius on
  the same short-leg geometry — the exact case that caught the second failed attempt's own bug.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS (clean build, all three targets, MSVC via vcvars64; no new
      warnings attributable to this task's changes — confirmed by diffing against pre-existing
      exceptions-disabled `try`/`catch` warnings in unrelated files)
- [x] architecture-review  — PASS (no Workshop architectural decision; boundary check §3 stands —
      no new entity kind, fillet arcs are ordinary `Arc` entities; no new dependency;
      `filletRadius`/`cornerTrimMode` persist through the existing `gosurvey-user.json` pipeline,
      matching `trimState`'s exact shape, not a new one — the "build a registry" idea was
      considered and explicitly declined, D-2026-08-24-g; the new geometry functions have 3+
      concrete callers each from day one — model apply, paper apply, unit tests)
- [x] code-review          — PASS. Self-reviewed the full diff. Two real bugs were found and fixed
      during self-review/test (see §8 log): the triple-undo-snapshot bug (fixed via an opt-out
      parameter that leaves LENGTHEN's/EXTEND's own call sites byte-for-byte unchanged) and the
      pick-vs-tangent-point near/far bug (fixed by comparing against the tangent point, matching
      what the operation actually needs). `ApplyFilletTrimSingle`/its paper equivalent reuse
      LENGTHEN's mutation functions 100% unchanged, per REQ-103's own stated reuse chain — no
      arc/line endpoint math was re-derived. Undo granularity: exactly one `PushUndoSnapshot` per
      fillet (model) — verified directly by the transcript's own UNDO/REDO round-trip, not just
      asserted.
- [x] dependency-audit     — n/a (no new dependency; `curveintersect.hpp`'s inclusion in
      `CadCommands.hpp` is a new header dependency edge but the same already-linked, already-tested
      `util/curveintersect.cpp` EXTEND already depends on — no new build-graph node)
- [x] performance-review   — n/a (FILLET runs once per user apply, not a per-frame hot path; the
      geometry solve tries at most 8 offset-intersection combinations per apply, negligible)
- [x] testing              — PASS. Geometry: 8 unit-test cases (`FilletGeomTests.cpp`), each hand-
      derived and verified before being trusted, covering Line-Line (with a genuine 4-candidate
      disambiguation, not just a single-solution case), Line-Circle, Circle-Circle, radius-0,
      CW/CCW arc angle conversion, and the parallel-lines semicircle. Command flow: two headless
      transcripts covering Case A (same-polyline adjacent segments) and Case B (two different
      lines), radius-0, the Trim/No-trim toggle, the identical-segment-twice refusal, and a full
      UNDO/REDO round-trip — including one `CLICK`-driven part per this session's own explicit
      instruction to verify real viewport-click routing (`ViewportClickRouteFor`), not just `PICK`.
      Paper-space FILLET is code-reviewed only, not transcript-run — the headless driver has no
      paper-space verb at all (pre-existing, epic-wide limitation every prior REQ-103 paper path
      already carries, not new to this task) — it reuses the identical `ApplyFilletTrimSingle`-
      equivalent/`SolveFilletCenter`/`ApplyFilletPolylineCorner`-equivalent functions the tested
      model path uses, differing only in which store (`AppCommandState` vs `PaperLayout`) supplies
      the raw coordinates.

## 10. Verification result
- Submitted:  2026-08-24
- Verdict:    **PASS** (self-verified per workflow.md — this task has no separate human verification
  pass yet; manual GUI confirmation of the ribbon/ClICK-flow is pending, same as every prior
  REQ-103 step before its own first hand-driven pass)
- Findings:   2 findings, both fixed during self-verification, neither open — (1) triple undo
  snapshot per fillet apply, caught by the transcript's own UNDO/REDO round-trip check; (2)
  pick-based (rather than tangent-point-based) near/far endpoint selection, caught by a transcript
  pick landing exactly on a line's midpoint.

## 11. Outcome
- Requirements satisfied: REQ-103 step 6a (FILLET) — Acceptance met: yes, pending only the user's
  manual GUI pass (ribbon/click-flow — this project's own no-UI-automation constraint, same as
  every prior REQ-103 step)
- Tests added:            `tests/FilletGeomTests.cpp` (8 Catch2 cases, tangent-arc geometry pin);
  `tests/headless/transcripts/fillet-lines-basic.txt` (Line-Line, CLICK-driven + radius-0 +
  Trim/No-trim); `tests/headless/transcripts/fillet-polyline-corner.txt` (Case A same-polyline
  corner, radius-0, identical-segment refusal)
- Refactors:              `ApplyLengthenToLine`/`ApplyLengthenToArc`/`ApplyLengthenToPolylineEnd`
  gained an opt-out `bool pushUndo = true` parameter (default-preserving — LENGTHEN's and EXTEND's
  own call sites are unchanged) so FILLET can reuse them twice per apply under one atomic undo step
- Technical debt noted:   (a) no live drag-preview of the computed fillet arc in
  `TransformPreview.cpp` — only the latched-first-curve selection/hover highlight was added
  (matching BREAK/LENGTHEN's own precedent for an entity latch); the accepted acceptance text does
  not require a live preview, and none of REQ-103's prior steps (EXTEND in particular) added one
  either. Removal condition: build if a future GUI pass finds the static result hard to predict
  before the second click. (b) paper-space FILLET has no headless transcript coverage — inherited,
  epic-wide limitation of the headless harness (no paper-space verb exists at all), not new to this
  task; removal condition unchanged from every prior REQ-103 paper path's own identical note.
  (c) a generalized "system-variable registry" for persisted settings was considered and explicitly
  declined (D-2026-08-24-g) — `filletRadius`/`cornerTrimMode` follow `trimState`'s exact existing
  ad-hoc shape instead. Removal condition: revisit only if/when `UserPrefs.cpp`'s ~20+ existing
  settings are deliberately migrated as their own project, not opportunistically here.
- Docs updated:           `spec/requirements.md` (REQ-103 FILLET+CHAMFER acceptance + revision line
  + traceability row), `spec/project.md` (D-2026-08-24-g)
- Done:                   2026-08-24

## 11. Outcome
- (pending — fill on completion)
