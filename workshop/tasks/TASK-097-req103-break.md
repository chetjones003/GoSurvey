# TASK-097 — BREAK command (REQ-103 step 4 of 8)

- Type:    feature
- Status:  done
- Opened:  2026-08-24
- Owner:   Workshop

## 1. Authority
- Goal:         GOAL-01 (a working surveyor's CAD)
- Requirements: REQ-103 (accepted 2026-08-23, D-2026-08-23-j; BREAK's acceptance written 2026-08-24, D-2026-08-24-c) — this task delivers step 4 (BREAK) of 8
- Constraints:  CON-07 (build reproducibility); REQ-101 (numerical tolerance); REQ-076/ADR-027 (stable entity identity — the far piece of a split gets a fresh id, never a copied one); REQ-201 (no silent failures)
- Acceptance:   verbatim from REQ-103's BREAK acceptance:
  - eligible entities: Line, Arc (any sweep, including full-circle), Circle, open Polyline, closed
    Polyline; refused with a stated reason: Ellipse, Annotation, FeatureLine, Surface, Mesh,
    FilledRegion, PdfUnderlay, Text, Mtext, SurveyPoint;
  - the entity-selecting pick also supplies break point 1 (closest point ON the entity, not the raw
    pick); a second pick supplies break point 2, projected onto the same entity; a break point
    coinciding with an existing endpoint (REQ-101 tolerance) is treated as that endpoint exactly;
  - OPEN entities (Line, non-full Arc, open Polyline): break points ordered by position along the
    entity (independent of click order); material between them removed. Both interior → original
    shortened in place + a NEW duplicate (fresh id) for the far piece. One at an endpoint → shortened
    in place only, no duplicate. Both at the two endpoints → refused ("would remove the entire
    entity"), unchanged;
  - CLOSED entities (Circle, full-circle-sweep Arc, closed Polyline): click order matters — material
    swept from point 1 to point 2 (CCW for Circle/full Arc; stored vertex order for closed Polyline)
    is removed, leaving one open result starting at point 2, ending at point 1 (AutoCAD's circle-break
    convention). Circle → new Arc entity (fresh id, conversion between existing kinds, not a new
    kind). Full-circle Arc → mutated in place (same id). Closed Polyline → mutated in place (same id,
    `Closed` cleared). Identical repeated points → opens the entity there with nothing removed (valid
    "break at point" case, not a refusal);
  - each break is its own undo step; loops back to "select object" after each completed break until
    Enter/Esc;
  - reachable from ribbon, typed `BREAK`/`BR`, right-click repeat; works in model space, floating
    model space, and native paper space.
- Owning subsystem: Commands (`src/commands/CadCommands.cpp`/`.hpp`) + UI (`src/ui/CadUi.cpp`,
  `src/viewport/TransformPreview.cpp`)

## 2. Scope
- In scope: BREAK end-to-end per the acceptance above, in model space, floating model space, and
  native paper space; every integration site a modify command touches (checklist established by
  TASK-094/095/096).
- Out of scope: STRETCH/FILLET/CHAMFER/ARRAY/EXPLODE (REQ-103 steps 5–8); Ellipse targets (no
  elliptical-arc entity kind exists — adding one is a new-entity-kind change this step's own spec
  note rules out); a general reusable "closest point on entity" abstraction (written narrowly for
  BREAK's own need, one caller today).
- Smallest change: no new entity kind (Circle→Arc is a conversion between two entity kinds that
  already exist), no new store, no schema change, no new dependency. New code is limited to: a
  closest-point-on-entity projection helper, the open-entity split path, and the closed-entity
  split/convert path.

## 3. Architectural boundary check
- [x] **No — proceed.**
  - New abstraction/layer/dependency: none.
  - New global mutable state: none — phase/selection fields live on the existing per-command
    `AppCommandState` / paper-click state, same shape MIRROR/LENGTHEN/EXTEND's already have.
  - Ownership change: none.
  - Public-API / data-format change: none — no `.gs`/DXF schema touched; Circle→Arc conversion moves
    an entity between two arrays that already exist in the schema, using the existing
    `DuplicatedEntityAttrs` fresh-id pattern.
  - Upward dependency: none.
  - No shared abstraction claimed for the closest-point-on-entity helper — one caller today (BREAK
    itself), per CLAUDE.md rule 2.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Should Circle/full-circle-sweep Arc be an eligible BREAK target this increment (converts to Arc), or deferred to keep scope smaller? | 2026-08-24 | User: include it now. |
| Q2 | Should closed Polyline be an eligible BREAK target this increment (splits open), or refused like LENGTHEN/EXTEND already refuse it? | 2026-08-24 | User: support it now. |

## 5. Assumptions
```
ASSUMPTION-1: BREAK's two break points are supplied by projecting each pick onto the already-
              identified entity (closest point on that entity to the pick), not by using the raw
              pick coordinates directly.
- Because:       matches AutoCAD's own BREAK behavior and is the only way the operation is well-
                 defined for a pick that lands near but not exactly on the entity.
- Risk if wrong: none material — this is BREAK's defining behavior, not a judgment call.
- Validate by:   transcripts use PICK coordinates near, not exactly on, curved entities and assert
                 the resulting geometry lands exactly on the entity.

ASSUMPTION-2: For a closed entity, "point 1" is the FIRST pick (the one that also selected the
              entity), not whichever pick is geometrically first in parameter order — click order is
              load-bearing here, unlike the open-entity case.
- Because:       this is AutoCAD's own circle-break convention (material removed CCW from point 1 to
                 point 2); reversing the picks reverses the result, which is expected and matches
                 real AutoCAD behavior, not a bug to guard against.
- Risk if wrong: a transcript picking points in the "wrong" order would need reversing, not the code.
- Validate by:   hand-computed angles in the Circle/closed-Polyline transcripts, checked before
                 trusting the transcript (same discipline as TASK-095/096's arc math).
```

## 6. Plan
- Approach: new `BreakPhase` (`SelectFirstPoint`, `SelectSecondPoint`) state, copy-adapted from the
  established two-phase shape (TRIM/EXTEND), but selecting exactly one entity rather than a set. A
  new closest-point-on-entity projection function is the only genuinely new geometry primitive;
  everything downstream (ordering, removal, duplication) is BREAK-specific mutation code, since no
  existing "shorten/split" function covers the closed-entity conversion cases.
- Files/functions to touch:
  - `src/commands/CadCommands.hpp` — `Kind::Break`; `BreakPhase`; break-target state (selected
    `SelectedEntity` + break point 1, world coords + per-type parameter); paper equivalents
    (`paperBreakPhase` int, a stored `PaperEntityRef` + break point 1).
  - `src/commands/CadCommands.cpp`:
    - `ClosestPointOnEntity(st, entity, wx, wy, &outPoint, &outParam)` — Line/Polyline via the
      existing `ClosestPointOnSegment` (`:7818`); Arc/Circle via `atan2` against center, normalized
      into the arc's own sweep (or unrestricted for Circle/full-sweep Arc). Paper equivalent against
      `PaperLayout`'s arrays.
    - Open-entity split: order two params, shorten original in place, optionally push a new
      duplicate (`DuplicatedEntityAttrs`) for the far piece; refuse if both points are at the two
      existing endpoints.
    - Closed-entity split: Circle → erase + push new `userArcs` entry (`DuplicatedEntityAttrs`);
      full-circle Arc → mutate `startRad`/`sweepRad` in place; closed Polyline → rewrite vertex list
      walking forward from point 2's cut to point 1's cut (wrapping through the closing edge), clear
      `Closed`.
    - `StartBreakCommand`, viewport-pick dispatch (`SelectFirstPoint`: `PickClosestCadEntity`, refuse
      ineligible types; `SelectSecondPoint`: project onto selected entity, apply, loop back), full
      integration checklist wiring (Kind/KindName, `kRegistry` `{"break","br",...}`, typed dispatch,
      `SubmitViewportPickImpl`, `lastCommand`/`RepeatLastCommand`, `CancelActiveCommand` log line, one
      `PushUndoSnapshot` per break immediately before the mutation).
    - Paper-space equivalent using the same projection helper against `PaperLayout` arrays, paper
      duplication copying `EntityAttributes` verbatim (existing precedent, not a new policy).
  - `src/ui/CadUi.cpp` — footer hint (`SelectFirstPoint`/`SelectSecondPoint` phase text), ribbon
    button (new icon), autocomplete icon map, context menu, hover-suppression during
    `SelectSecondPoint` (mirrors TRIM/EXTEND's entity-pick gate), paper-space `paperBreakPhase` click
    flow (Enter/Escape handled in the same paper-click block MIRROR/LENGTHEN/EXTEND's paper phases
    already hook into, `CadUi.cpp:8668-8823`).
  - `src/viewport/TransformPreview.cpp` — selected entity highlighted like a single-entity selection
    while `SelectSecondPoint` is pending; a small marker at the live-projected point follows the
    cursor.
- Test approach:
  - happy path: Line two-piece split (both interior), Line single-end shorten (one point at an
    endpoint), open Polyline two-piece split across a bend, closed Polyline split-open including the
    same-point "break at point" case (hand-verified vertex order before trusting the transcript),
    Circle→Arc conversion (hand-computed angles, same rigor as LENGTHEN/EXTEND's arc transcripts),
    full-circle-sweep Arc in-place break.
  - failure mode: Line both-points-at-endpoints refusal (whole-entity removal); Ellipse/Text/
    SurveyPoint entity-selection refusal.
  - undo/redo: some breaks mutate only (SAMEFILE-shaped single id), others allocate a new id
    (DIFFERENTFILE) — confirm both shapes by running, not assuming.
  - GUI-only (manual, no UI-automation driver): ribbon button, live break-point marker, paper-space
    and floating-model-space click routing.
- Steps:
  - [x] `ClosestPointOnEntity` (model + paper)
  - [x] Open-entity split path (shorten + optional duplicate + whole-entity refusal)
  - [x] Closed-entity split path (Circle→Arc, full-Arc in-place, closed-Polyline in-place)
  - [x] `Kind::Break` + phase state + `StartBreakCommand` + viewport-click dispatch
  - [x] Paper-space `paperBreakPhase` + click flow
  - [x] Footer hint + ribbon button (new) + autocomplete icon + context menu + hover-suppression +
        selection-highlight + repeat-last + cancel-log
  - [x] Headless transcripts
  - [x] Build + headless run + full regression

## 7. Workflow-specific notes
- Feature: pre-flight design settled during planning (two scope questions asked and answered, §4).
  No tests-first — this codebase's modify commands are verified by headless transcript + manual GUI
  pass, not Catch2 unit tests (`AppCommandState` cannot link into `GoSurveyTests`).

## 8. Implementation log
- 2026-08-24: REQ-103 BREAK acceptance written (D-2026-08-24-c). Task opened, plan written. One
  Explore-agent research pass preceded this plan (entity stores, fresh-id duplication pattern,
  confirmed absence of any closest-point-on-entity utility, integration checklist unchanged from
  EXTEND). Two scope questions asked and answered by the user (§4) before design was finalized.
- 2026-08-24: Core commands-layer implementation landed: `ArcSweepParam`/`CircleBreakStartSweep`
  shared angle math; `ClosestPointOnEntity` (model) and `ClosestPointOnPaperEntity` (paper)
  projection; `ApplyBreakTo{Line,Circle,Arc,Polyline}` (open + closed polyline sub-paths) and their
  paper equivalents; `ReplacePolylineVerts`/`AppendNewPolyline` (+ paper equivalents) for CSR
  vertex-range rewrites, modeled directly on OVERKILL's existing cleanup-pass technique;
  `StartBreakCommand`/`HandleBreakViewportPick`/`ApplyBreakToPaperEntity`; full integration
  checklist wiring (Kind/KindName, `kRegistry` `{"break","br",...}`, `DispatchByPrimary`,
  `SubmitViewportPickImpl`, `RepeatLastCommand`, `CancelActiveCommand`/`ResetCadToolStateToIdle`
  phase resets, bare-Enter handling). Clean build of all three targets on first attempt.
- 2026-08-24: **Bug found and fixed during implementation** (before any test ran): the paper-space
  wrapper `ApplyBreakToPaperEntity` originally pushed one `PushUndoSnapshot` unconditionally before
  dispatching to the per-type paper function, but each per-type function can itself refuse (e.g.
  "would remove the entire line") without mutating anything — which would have left a spurious,
  no-op undo step behind on every refusal, corrupting the undo stack. Fixed by moving undo-snapshot
  responsibility into each `ApplyBreakToPaper*` function (returning `bool`, pushing its own snapshot
  only immediately before a mutation it has already decided will happen), mirroring the model-space
  functions' shape exactly — the wrapper now only calls `BumpCadGpuCache` when a sub-function
  reports success. The model-space functions were written this way from the start; only the paper
  wrapper had the bug, caught by re-reading the code before writing the paper transcript coverage.
- 2026-08-24: **Second bug found and fixed while designing the ribbon layout for BREAK's own
  button**: EXTEND's ribbon button (TASK-096) was added as a 4th item stacked into the SAME
  `ImGui::BeginGroup()`/`EndGroup()` column as Join/Mirror/Lengthen — but `RibbonSectionBegin` draws
  each section as a fixed-height `ImGui::BeginChild(..., ImGuiWindowFlags_NoScrollbar)`, and a
  small-button column is sized for exactly 3 rows (`colH`). A 4th item in one column falls below the
  child window's own clip rect and is genuinely invisible and unclickable — confirmed by reading
  `RibbonSectionBegin`'s `BeginChild` call and cross-checking against the Inquiry/Survey sections'
  own explicit "the panel is three small buttons tall, so a 4th needs its own column" comments,
  which EXTEND's own addition did not follow. Fixed by splitting the Modify section's third group
  into two: Join/Mirror/Lengthen (exactly 3, unchanged) and a new second column holding Extend and
  Break (2, well under the 3-row limit) — restoring EXTEND to visible/clickable at the same time
  BREAK was added, not a separate follow-up.
- 2026-08-24: Two headless transcripts written. Hand-derivation preceded both (same discipline as
  TASK-095/096's arc math): `break-line-and-polyline.txt` (Line two-piece split; Line
  endpoint-coincident shorten with no duplicate; Line whole-entity-removal refusal; open Polyline
  split across a 3-segment bend; Ellipse refusal; undo/redo accounting verifying a refused break
  pushes NO undo snapshot) and `break-circle-and-closed-polyline.txt` (Circle→Arc conversion with
  hand-computed CCW sweep angles; closed-Polyline split-open via the rotated-cumulative-param
  forward walk; the same-point "break at point" case opening a closed ring with nothing removed).
  `cmake -S . -B build` reconfigured to register the new transcripts (`file(GLOB ...)`, per
  established precedent) before either would run.
- 2026-08-24: **Two transcript-authoring bugs found on first run, both fixed, not code bugs**: (1)
  the CIRCLE draw command stays ACTIVE after committing (`ResetCircleDraft` does not clear
  `st.active`, unlike LINE/POLYLINE/ELLIPSE's terminal commits) — an omitted `ESC` after `CMD 10`
  let the immediately-following `CMD POLYLINE`/coordinate lines be consumed as further circle
  center/radius entries, producing 5 circles instead of 1; fixed by adding the `ESC` (EXTEND's own
  prior transcript had this same `ESC` after its `CMD CIRCLE` block — missed copying it forward).
  (2) the transcripts originally asserted `SAMEFILE` between the pre-BREAK save and the save taken
  after 3 undos; this can never hold whenever any of the undone breaks minted a fresh entity id
  (T1/T4/T6 all do, via `DuplicatedEntityAttrs`) — REQ-076 requires `nextEntityId` never roll back on
  undo, so the counter is permanently higher post-undo even though every entity's own id and
  geometry are otherwise correctly restored (verified instead via the LINES/POLYLINES/CIRCLES/ARCS
  counts already asserted, plus `CHECK ALL`). Removed those two assertions, documented why in the
  transcripts themselves, and kept the post-redo `SAMEFILE` checks (which do hold, since redo
  restores the exact forward snapshot verbatim). Both transcripts passed on the next run.
- 2026-08-24: Full regression: `ctest -C Debug` — 559/559 run tests passed (1 pre-existing disabled:
  `dxf-export-stable`), including both new BREAK transcripts.

## 9. Self-verification
- [x] build-project        — PASS: clean MSVC build, all three targets (GoSurvey, gosurvey_headless,
      GoSurveyTests), zero errors; only pre-existing warning classes (C4244/C4456/C4530), none
      introduced by this task's new code.
- [x] architecture-review  — PASS: no new entity kind (Circle→Arc converts between two entity kinds
      that already exist, per §3); no new store/dependency/global state; the ribbon-layout fix
      (§8) is a bug fix to existing (EXTEND's) code discovered while wiring BREAK's own button into
      the same section, not a new abstraction.
- [x] code-review          — PASS: self-reviewed while designing the transcripts; found and fixed
      the paper-undo-on-refusal bug and the ribbon-clipping bug (§8) before/while testing, not after
      a user report. `ClosestPointOnEntity`/`ClosestPointOnPaperEntity` and the
      `ApplyBreakTo*`/`ApplyBreakToPaper*` function pairs checked line-by-line against each other
      for the same shared math (`ArcSweepParam`, `CircleBreakStartSweep`) producing identical
      results on both stores.
- [x] dependency-audit     — n/a (no new dependency)
- [x] performance-review   — n/a (no hot-path change; one-shot per-pick command)
- [x] testing              — PASS: `break-line-and-polyline.txt` and
      `break-circle-and-closed-polyline.txt` both pass (72 and 60 steps respectively); full
      regression `ctest -C Debug` 559/559 run tests green (1 pre-existing disabled). A full-circle-
      sweep Arc's own BREAK path is NOT independently transcript-tested — constructing one through
      ARC's 3-point entry within the 1e-4 rad "is this exactly full" tolerance is impractical by
      hand-picked points; it shares 100% of its angle math with the Circle case (transcript-tested
      and hand-verified), and only the erase-vs-mutate-in-place difference is untested by
      transcript — reviewed by hand instead (§6/§8), a documented technical-debt-shaped gap, not a
      silent one. Manual GUI pass (ribbon rendering — including confirming EXTEND is now actually
      visible — live break-point marker, paper-space click flow) is left to the user — no
      UI-automation driver exists in this codebase.

## 10. Verification result
- Submitted:  2026-08-24 (self-run; solo task, no separate verification agent in this session)
- Verdict:    **PASS** — closed 2026-08-24: the user confirmed the manual GUI pass covering REQ-103 steps 1-5.
- Findings:   2 real bugs found and fixed pre/during-test (paper undo-on-refusal, §8; EXTEND's
  ribbon-clipping regression from TASK-096, §8) — neither an open finding, both resolved and
  covered by this task's own regression run (EXTEND's headless transcripts don't exercise ribbon
  rendering at all, so that bug could only be caught by code-reading, which is how it was found).

## 11. Outcome
- Requirements satisfied: REQ-103 step 4/8 (Acceptance met: yes; manual GUI pass confirmed by the user 2026-08-24)
- Tests added:            `tests/headless/transcripts/break-line-and-polyline.txt`,
  `tests/headless/transcripts/break-circle-and-closed-polyline.txt`
- Refactors:              none to existing shipped code beyond the two bug fixes noted in §8 (the
  EXTEND ribbon fix touches TASK-096's code, but as a correctness fix, not a refactor)
- Technical debt noted:   full-circle-sweep Arc's BREAK path is untested by transcript (§9) —
  removal condition: a future task that needs to construct one anyway (e.g. exercising it via a
  different entry point) should add coverage then, rather than this task inventing an artificial
  one now. Carried forward from TASK-094: paper-space modify commands still can't offer a typed-
  value prompt, so BREAK's paper path — like MIRROR/LENGTHEN/EXTEND's — is click-only; unchanged
  scope, no regression (BREAK needs no typed value at all, so this doesn't limit it further).
- Docs updated:           spec/requirements.md (REQ-103), spec/project.md (D-2026-08-24-c), this task
- Done:                   2026-08-24
