# TASK-103 — CHAMFER modify command (REQ-103 step 6b)

- Type:    feature
- Status:  done
- Opened:  2026-08-24
- Owner:   Claude (agent)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         REQ-103 — Modify-command completeness (GOAL per spec/project.md D-2026-08-23-j)
- Requirements: REQ-103 (step 6b of 8, CHAMFER) — must be `accepted` (it is)
- Constraints:  REQ-101 (±0.01 ft tolerance, analytic not tessellated), REQ-062 (curveisect
  library), REQ-201 (no silent failures), REQ-076/ADR-027 (stable entity ids, never rolled back
  on undo)
- Acceptance:   restated verbatim from `spec/requirements.md` REQ-103 "Acceptance — CHAMFER (step
  6b)" — full text there, not duplicated here. Summary: eligible curves are Line and open/closed-
  Polyline segments only (no Arc — real AutoCAD restriction); Distance/Distance and Distance/Angle
  input modes, both persisted like `filletRadius`; the shared `cornerTrimMode` toggle (AutoCAD's
  own shared `TRIMMODE`); the same Case A (same-polyline adjacent segments) / Case B (two different
  curves, or a polyline's own end segment) split TASK-102 already built; parallel/non-intersecting
  lines refused (no semicircle special case, unlike FILLET — a real, stated asymmetry); both
  distances (or the Distance/Angle distance) at 0 trims to the intersection point directly, no
  chamfer Line created; full model+paper-space parity.
- Owning subsystem: Commands (`src/commands/CadCommands.{hpp,cpp}`), UI wiring
  (`src/ui/CadUi.cpp`), preview (`src/viewport/TransformPreview.cpp`), viewport click routing
  (`src/viewport/ViewportPickPolicy.hpp`), persisted settings (`src/io/UserPrefs.cpp`) — same
  subsystems TASK-102 (FILLET) touched, reusing its infrastructure directly.

## 2. Scope
- In scope: model-space + paper-space CHAMFER; Line/Polyline-segment eligibility (Case A/B, reusing
  TASK-102's polyline-segment addressing and end-segment-only Case B restriction unchanged);
  Distance/Distance and Distance/Angle corner-point construction (new geometry, small — reuses
  `SolveFilletCenter(c1,c2,0.f,...)` for the intersection point exactly like FILLET's own radius-0
  case); persisted `chamferDist1`/`chamferDist2`/`chamferAngle`/`chamferMode`; shared
  `cornerTrimMode`; full integration checklist; headless transcripts (including `CLICK`-driven).
- Out of scope: ARRAY/EXPLODE (steps 7-8, not started); Arc as an eligible curve (explicitly
  excluded by the accepted acceptance text — no standard chamfer-to-arc geometry).
- Smallest change: two new pure functions beside FILLET's own geometry block
  (`ChamferPointAtDistance`, `ChamferRayIntersect`) — everything else (eligibility restricted to
  non-Arc, polyline-segment addressing, Case A/B dispatch shape, trim reuse via
  `ApplyLengthenToLine`/`ToPolylineEnd`, undo-step discipline, paper-space duplication pattern, UI
  checklist) is copy-adapted from TASK-102's already-shipped, already-tested FILLET code, not
  reinvented.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed. No new entity kind (a chamfer connector is an ordinary `Line` entity); no
      new dependency; persisted settings follow `filletRadius`'s exact precedent (D-2026-08-24-g);
      the new geometry functions have 2+ concrete callers (model apply, paper apply) from day one.
    - [ ] Yes → STOP.

## 4. Questions  (workflow.md §5 — ask before guessing)
Answered together with FILLET's own pre-flight (D-2026-08-24-g): eligible curves restricted to
Line/Polyline-segment (Arc excluded — real AutoCAD restriction, no design latitude here); Distance/
Distance AND Distance/Angle both built (Q5, "CHAMFER input" — user chose both modes over
Distance/Distance alone); shared `cornerTrimMode` (AutoCAD's own convention, not a second toggle —
confirmed reasonable without a separate question); full paper-space parity (Q4, same answer as
FILLET). No new questions needed for this step.

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: Distance/Angle mode's ray-rotation sign (which of the two possible rotation
directions from curve 1's kept direction) is resolved by trying both and keeping whichever
intersection with curve 2 lands nearer curve 2's own pick point.
- Because:     the accepted acceptance text says only "rotated ... toward curve 2's side," not
               which of CW/CCW that is in this codebase's coordinate convention — the same
               "nearest to the pick" disambiguation FILLET's own SolveFilletCenter and every other
               REQ-103 step's tie-break already use.
- Risk if wrong: a pick placed unusually far from the natural chamfer corner could resolve to the
               wrong rotation. Low — the same risk profile FILLET's own tie-break already accepted.
- Validate by:  unit-tested directly against a hand-computed case in both rotation senses.

ASSUMPTION-2: the two picked curves' intersection point (needed by both D/D and D/A modes) is found
by calling `SolveFilletCenter(c1, c2, 0.f, pick1, pick2, &px, &py)` — FILLET's own radius-0 code
path — rather than a second, CHAMFER-specific line-intersection function.
- Because:     radius-0 offsetting collapses to the original curves' own intersection (already
               proven correct by tests/FilletGeomTests.cpp's own radius-0 case), so this is genuine
               reuse, not a coincidental shortcut — CHAMFER only ever calls it with two Line-typed
               `FilletCurve`s (Arc is excluded), which is exactly the sub-case that function already
               handles.
- Risk if wrong: none beyond what FILLET's own radius-0 path already carries — same tested code.
- Validate by:  covered transitively by tests/FilletGeomTests.cpp's existing radius-0 case; no new
               test needed for the intersection-finding step itself.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: mirror TASK-102's structure end to end, adapting only the corner-point construction:
  1. Two new pure geometry functions in `CadCommands.hpp` beside FILLET's own block
     (`ChamferPointAtDistance`, `ChamferRayIntersect`), unit-tested first
     (`tests/ChamferGeomTests.cpp`).
  2. `CadCommands.hpp` state: `Kind::Chamfer`, `ChamferPhase` (copy of `FilletPhase`), latched
     first-pick fields, `chamferDist1`/`chamferDist2`/`chamferAngle`/`chamferMode` persisted
     fields, `chamferTextAwaiting*` transient flags, paper-space `paperChamferPhase` + latched
     fields.
  3. Model-space apply: `ChamferEligibility` (copy of `FilletEligibility` minus the Arc case — Arc
     refused outright), `ApplyChamferTrimSingle` (copy of `ApplyFilletTrimSingle` minus the Arc
     branch), `ApplyChamferPolylineCorner` (copy of `ApplyFilletPolylineCorner`'s vertex-splice
     shape, connector is a Line not an Arc), `StartChamferCommand`/`HandleChamferViewportPick`/
     `HandleChamferText` (D/A mode-letter handling added to the R/T-equivalent shape).
  4. Paper-space apply: mirrors TASK-102's paper block exactly (`ApplyChamferToPaperEntities`,
     `ChamferEligibility`-equivalent for paper, `ApplyChamferPolylineCornerPaper`), same click-block
     wiring shape in `CadUi.cpp`.
  5. `UserPrefs.cpp` persistence for the four new settings, same shape as `filletRadius`.
  6. Full integration checklist: `RibbonIconKind::Chamfer` (inserted before `Traverse` beside
     `Fillet`, same sizing-formula reasoning), vector-art icon (two legs joined by a straight
     bevel, not a rounded corner — visually distinct from FILLET's icon), 6th Modify-ribbon column
     (the 5th, added for FILLET alone, now holds two items — still within the 3-row limit), typed
     command registry (`chamfer`/`cha`) + dispatch, `RepeatLastCommand` case, `ViewportPickPolicy.hpp`
     switch, context-menu entry, `TransformPreview.cpp` highlight (same latched-entity shape as
     FILLET).
  7. Headless transcripts: `CLICK`-driven Line-Line D/D case, D/A mode, radius-0-equivalent
     (both distances 0), parallel-lines refusal (proving NO semicircle special case, unlike
     FILLET), Case A polyline corner, Trim/No-trim toggle.
- Files/functions to touch: same file list as TASK-102 (§6 there), plus `tests/ChamferGeomTests.cpp`
  and `tests/headless/transcripts/chamfer-*.txt`.
- Test approach: same shape as TASK-102 — geometry unit-tested and hand-verified first, then
  headless transcripts for command flow, full `ctest` regression before self-verification.
- Steps:
  - [x] Open this task file
  - [x] Implement + hand-verify `ChamferPointAtDistance`/`ChamferRayIntersect`; wrote
        `tests/ChamferGeomTests.cpp` first, confirmed green before continuing (3 cases, 11 assertions)
  - [x] Implement `CadCommands.hpp` state additions
  - [x] Implement model-space apply (Line/Polyline Case A/B) + dispatch wiring
  - [x] Implement paper-space apply + click-block wiring
  - [x] Implement `UserPrefs.cpp` persistence
  - [x] Implement `CadUi.cpp` full integration checklist
  - [x] Implement `TransformPreview.cpp` highlight
  - [x] Write headless transcripts, hand-verify expected behavior
  - [x] Build all three targets; run new tests/transcripts; run full `ctest` regression (588/588)
  - [x] Self-verify (§9); write completion report (§11)

## 7. Workflow-specific notes
- Feature: no new pre-flight questions (see §4) — this step's design latitude was already resolved
  by FILLET's own D-2026-08-24-g. Tests-first for the two new geometry functions, same reasoning as
  TASK-102.

## 8. Implementation log  (append as you work)
- 2026-08-24 — Task opened, immediately after TASK-102 (FILLET) closed. Reusing its infrastructure
  directly rather than re-deriving; see TASK-102's own §8 log for the two real bugs found there
  (triple undo-snapshot; pick-vs-tangent-point near/far selection) — both fixes live in shared code
  (`ApplyLengthenToLine/ToArc/ToPolylineEnd`'s `pushUndo` parameter, `NearerToFirstPoint` called
  against the computed point rather than the pick) that CHAMFER inherits for free.
- 2026-08-24 — Two new geometry functions written (`ChamferPointAtDistance`, `ChamferRayIntersect`,
  inline in `CadCommands.hpp` beside FILLET's own block). `tests/ChamferGeomTests.cpp` written and
  hand-verified first (3 cases: Distance/Distance signing on both sides of a curve; Distance/Angle
  picking the correct one of two possible rotation senses by nearest-to-pick, verified against a
  hand-derived 45-degree/perpendicular-target case; a parallel-ray no-intersection refusal) — all
  green before continuing.
- 2026-08-24 — Model-space command plumbing written, reusing `BuildFilletCurveFromEntity`/
  `ApplyFilletTrimSingle` UNCHANGED (both already dispatch by entity type and simply never see an
  Arc, since `ChamferEligibility` refuses it upstream) and `SolveFilletCenter(c1,c2,0.f,...)` for
  the two curves' intersection point (FILLET's own radius-0 path, already proven correct).
  `ChamferEligibility`/`ApplyChamferPolylineCorner` copy-adapted from FILLET's own Case A/B shape;
  `StartChamferCommand`/`HandleChamferViewportPick`/`HandleChamferText` add the D(istance)/
  A(ngle)/T(rim) mode-letter flow. Two headless transcripts written
  (`chamfer-lines-basic.txt` — CLICK-driven D/D, both-distances-0, a parallel-lines REFUSAL proving
  the real asymmetry with FILLET's own semicircle special case, and D/A mode cross-checked against
  D/D's own result for the same corner; `chamfer-polyline-corner.txt` — Case A). **Both transcripts
  passed on the first run** — the two real bugs TASK-102 found and fixed (triple undo-snapshot;
  pick-vs-computed-point near/far selection) live entirely in the shared, reused functions, so
  CHAMFER inherited both fixes for free rather than repeating either mistake.
- 2026-08-24 — Paper-space CHAMFER written in full (`PaperChamferEligibility`,
  `ApplyChamferPolylineCornerPaper` using `ReplacePaperPolylineVerts`,
  `ApplyChamferToPaperEntities` reusing `ApplyFilletTrimSingleToPaperEntity`/`SolveFilletCenter`/
  `BuildFilletCurveFromPaperEntity` unchanged), wired into `CadUi.cpp`'s paper click-block (ESC-
  reset, hover-suppression, two new phase branches) mirroring paper FILLET's own shape exactly.
- 2026-08-24 — Full `CadUi.cpp` integration checklist completed: `RibbonIconKind::Chamfer` (inserted
  before `Traverse` beside `Fillet`), a hand-drawn vector-art icon (straight bevel, visually
  distinct from FILLET's rounded corner — AutoCAD's own CHAMFER glyph shape), icon-name lookup,
  autocomplete map entry, added to FILLET's own 5th Modify-ribbon column (two items, still within
  the 3-row limit), and a "Basic Modify Tools" context-menu entry.
- 2026-08-24 — Full build (GoSurvey, gosurvey_headless, GoSurveyTests, all clean, no new warnings)
  and full `ctest` regression: 588/588 passed (1 pre-existing disabled test, `dxf-export-stable`,
  unrelated), including both new transcripts and all 3 new `ChamferGeomTests.cpp` cases.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS (clean build, all three targets, MSVC via vcvars64; no new
      warnings beyond this file's pre-existing exceptions-disabled `try`/`catch` diagnostics,
      unrelated to this task)
- [x] architecture-review  — PASS (no Workshop architectural decision; boundary check §3 stands —
      no new entity kind, a chamfer connector is an ordinary `Line` entity; no new dependency;
      `chamferDist1`/`chamferDist2`/`chamferAngle`/`chamferMode` persist through the existing
      `gosurvey-user.json` pipeline, matching `filletRadius`'s exact shape per D-2026-08-24-g; the
      two new geometry functions have 3+ concrete callers each from day one — model apply, paper
      apply, unit tests. CHAMFER reuses FILLET's own `FilletCurve`/`SolveFilletCenter`/
      `BuildFilletCurveFromEntity`/`ApplyFilletTrimSingle` unchanged rather than duplicating them —
      the exact "smallest change" this task's own plan committed to)
- [x] code-review          — PASS. Self-reviewed the full diff. No new bugs found during self-
      review — both real defects this whole REQ-103 step-6 pair surfaced (triple undo-snapshot;
      pick-vs-computed-point near/far selection) were already fixed in TASK-102's shared code, and
      CHAMFER's own two headless transcripts passed on the first run, which is itself evidence the
      reuse was genuine rather than superficial (a re-derived, subtly different implementation
      would plausibly have hit at least one of the same two failure modes independently). Undo
      granularity: exactly one `PushUndoSnapshot` per chamfer (model), same discipline as FILLET.
- [x] dependency-audit     — n/a (no new dependency; reuses the same `curveintersect.hpp` edge
      FILLET already added)
- [x] performance-review   — n/a (CHAMFER runs once per user apply, not a per-frame hot path;
      `ChamferRayIntersect` tries at most 2 ray directions per apply, negligible)
- [x] testing              — PASS. Geometry: 3 unit-test cases (`ChamferGeomTests.cpp`), each hand-
      derived and verified before being trusted, covering Distance/Distance's signed-side
      construction and Distance/Angle's two-rotation-sense disambiguation (cross-checked against an
      independently hand-computed 45-degree case). Command flow: two headless transcripts covering
      Case A (same-polyline adjacent segments) and Case B (two different lines), Distance/Distance,
      Distance/Angle (cross-checked against Distance/Distance's own result for the identical
      corner — a real invariant, not just a plausible-looking number), both-distances-0, the
      parallel-lines REFUSAL (proving the real asymmetry with FILLET's semicircle special case,
      not merely asserting it in the spec text), the identical-segment-twice refusal, and a full
      UNDO/REDO round-trip — including one `CLICK`-driven part per this session's own explicit
      instruction. Paper-space CHAMFER is code-reviewed only, not transcript-run — same pre-
      existing, epic-wide headless-harness limitation TASK-102 already documents, not new here.

## 10. Verification result
- Submitted:  2026-08-24
- Verdict:    **PASS** (self-verified per workflow.md; manual GUI confirmation of the ribbon/click
  flow is pending, same as TASK-102 and every prior REQ-103 step before its own first hand-driven
  pass)
- Findings:   none — both real defects this REQ-103 step-6 pair surfaced were found and fixed
  during TASK-102's own self-verification, in shared code CHAMFER reuses unchanged.

## 11. Outcome
- Requirements satisfied: REQ-103 step 6b (CHAMFER) — Acceptance met: yes, pending only the user's
  manual GUI pass (ribbon/click-flow — this project's own no-UI-automation constraint)
- Tests added:            `tests/ChamferGeomTests.cpp` (3 Catch2 cases, corner-point geometry pin);
  `tests/headless/transcripts/chamfer-lines-basic.txt` (Line-Line, CLICK-driven D/D + D/A +
  both-distances-0 + parallel-lines refusal); `tests/headless/transcripts/chamfer-polyline-corner.txt`
  (Case A same-polyline corner, distance-0, identical-segment refusal)
- Refactors:              none beyond TASK-102's own (CHAMFER adds no new refactor of its own —
  it consumes FILLET's `pushUndo` parameter and tangent-point-based `NearerToFirstPoint` usage as
  already-shipped)
- Technical debt noted:   (a) no live drag-preview in `TransformPreview.cpp` — same reasoning and
  removal condition as TASK-102's identical note (only the latched-first-curve highlight was
  added, matching precedent; the accepted acceptance text does not require a live preview).
  (b) paper-space CHAMFER has no headless transcript coverage — same inherited, epic-wide
  headless-harness limitation TASK-102 already carries, not new here.
- Docs updated:           `spec/requirements.md` (REQ-103 traceability row), `spec/project.md`
  (D-2026-08-24-g already covers both FILLET and CHAMFER's design decisions — no new entry needed)
- Done:                   2026-08-24
