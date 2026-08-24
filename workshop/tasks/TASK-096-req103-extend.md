# TASK-096 — EXTEND command (REQ-103 step 3 of 8)

- Type:    feature
- Status:  done (headless); manual GUI pass pending
- Opened:  2026-08-24
- Owner:   Workshop

## 1. Authority
- Goal:         GOAL-01 (a working surveyor's CAD)
- Requirements: REQ-103 (accepted 2026-08-23, D-2026-08-23-j; EXTEND's acceptance written 2026-08-24, D-2026-08-24-b) — this task delivers step 3 (EXTEND) of 8
- Constraints:  CON-07 (build reproducibility); REQ-101 (numerical tolerance — boundary intersection MUST be analytic, not tessellated); REQ-201 (no silent failures)
- Acceptance:   verbatim from REQ-103's EXTEND acceptance:
  - eligible boundaries: anything but Annotation/FeatureLine/Surface (refused, stated reason); eligible targets: Line, open Polyline, non-full-circle Arc (same set LENGTHEN established, same refusals);
  - boundary intersection is analytic (`curveisect`), never tessellated;
  - the end nearest the pick extends along its own existing direction/circle to the nearest boundary, within REQ-101 tolerance; the other end stays fixed;
  - a target that reaches no boundary is refused with a stated reason, geometry unchanged;
  - two-phase pick (boundaries, Enter, targets), boundaries read as a selection while picked, loops back to "select object" after each extend until Enter/Esc, one undo step per extend;
  - reachable from ribbon, typed `EXTEND`/`EX`, right-click repeat; works in model space, floating model space, AND native paper space.
- Owning subsystem: Commands (`src/commands/CadCommands.cpp`/`.hpp`) + UI (`src/ui/CadUi.cpp`, `src/viewport/TransformPreview.cpp`) + `src/util/curveintersect.hpp` (REQ-062, reused unmodified)

## 2. Scope
- In scope: EXTEND end-to-end per the acceptance above, in model space, floating model space, and
  native paper space; every integration site a modify command touches (checklist established by
  TASK-094/095).
- Out of scope: BREAK/STRETCH/FILLET/CHAMFER/ARRAY/EXPLODE (REQ-103 steps 4–8); a TRIMSTATE-style
  alternate "smart line" boundary mode (TRIM-specific UX, no AutoCAD EXTEND analogue); adding a new
  `IntersectRay*` family to `curveisect` (the long-query-segment / full-circle-conic technique
  avoids needing one).
- Smallest change: no new entity kind, no new store, no schema change, no new dependency (`curveisect`
  already exists and is already linked). EXTEND's own new code is limited to: boundary-edge
  selection state (copy-adapted from TRIM's), a boundary→`curveisect` shape conversion, and a
  nearest-valid-hit search — the actual geometry mutation is 100% reused from
  `ApplyLengthenToLine`/`ToArc`/`ToPolylineEnd` (TASK-095), not re-implemented.

## 3. Architectural boundary check
- [x] **No — proceed.**
  - New abstraction/layer/dependency: none — `curveisect` is already an accepted, shipped, linked
    library (REQ-062); the boundary-shape conversion and nearest-hit functions are additive
    utilities beside TRIM/OFFSET/LENGTHEN's.
  - New global mutable state: none — phase/boundary fields live on the existing per-command
    `AppCommandState` / paper-click state, same shape MIRROR/LENGTHEN's already have.
  - Ownership change: none.
  - Public-API / data-format change: none — no `.gs`/DXF schema touched.
  - Upward dependency: none — stays inside Commands/UI, reusing `curveisect` (already a Commands/
    viewport-layer dependency via `CadSnap.cpp`).
  - The one new shared model/paper "nearest boundary hit" function is justified by two genuine
    callers from the start (model + paper), not a speculative one-caller abstraction.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Should EXTEND's paper-space case be built now, or declined as out of scope given it needs a two-phase pick with no typed-text fallback (unlike MIRROR/LENGTHEN's paper simplifications)? | 2026-08-24 | User: build paper-space EXTEND too. |

## 5. Assumptions
```
ASSUMPTION-1: EXTEND's boundary intersection uses curveisect's existing finite-segment/finite-conic-
              sweep functions against a deliberately long (1,000,000-unit) query segment (Line/
              Polyline targets) or a full-circle Conic (Arc targets), rather than adding a new
              ray/infinite-line primitive to curveisect itself.
- Because:       curveisect has no ray concept anywhere (confirmed by research: every function
                 clamps both sides to [0,1]/sweep-containment) and EXTEND is its only caller that
                 would need one — a new shared primitive for one caller is the abstraction CLAUDE.md
                 rule 2 warns against; a sufficiently long finite segment is indistinguishable from
                 an infinite one for any real drawing's coordinate range.
- Risk if wrong: a drawing whose boundary sits further than 1,000,000 units from a target's fixed
                 end would miss it — essentially impossible for a survey drawing at REQ-101's scale.
- Validate by:   the headless transcripts' distances are all well within this bound by construction.

ASSUMPTION-2: EXTEND targets the same three entity kinds LENGTHEN established (Line, open Polyline,
              non-full-circle Arc), with the same refusal set for everything else, rather than
              inventing a different eligibility list.
- Because:       AutoCAD's own EXTEND supports exactly this set (general CAD knowledge); LENGTHEN's
                 own code comment says its Arc math was written for EXTEND's reuse; no REQ text
                 constrains this differently.
- Risk if wrong: none material — matches both precedent and the reused-math design.
- Validate by:   n/a, confirmed by design.
```

## 6. Plan
- Approach: copy-adapt TRIM's boundary-edge-collection UI shape (own `extendPhase`/
  `extendBoundaries`, not shared code); convert boundaries to `curveisect::Seg`/`Conic`; find the
  nearest valid extension hit via a long query segment (Line/Polyline) or full-circle conic (Arc);
  convert that hit into a `newLength` and delegate to the exact `ApplyLengthenTo*` functions
  TASK-095 already built and tested — no new mutation code.
- Files/functions to touch:
  - `src/commands/CadCommands.hpp` — `Kind::Extend`; `ExtendPhase` (`SelectBoundaries`,
    `SelectTargets`); `extendBoundaries` (`std::vector<SelectedEntity>`); paper equivalents
    (`paperExtendPhase` int, `paperExtendBoundaries` `std::vector<PaperEntityRef>`).
  - `src/commands/CadCommands.cpp`:
    - Modify `LengthenEligibility` to take a `const char* cmdName` parameter (used in its log
      messages instead of the hardcoded "LENGTHEN" prefix) so EXTEND can reuse it verbatim for
      target eligibility + near-end + current-length, with correct wording; update LENGTHEN's one
      call site to pass `"LENGTHEN"`.
    - `AppendModelBoundaryShapes(st, entity, &segs, &conics)` / `AppendPaperBoundaryShapes(L,
      ref, &segs, &conics)` — per-store boundary→`curveisect` conversion (Line/Polyline-edge→`Seg`,
      Circle/Arc/Ellipse→`MakeCircle`/`MakeArc`/`MakeEllipse`).
    - `FindExtendLineTarget(segs, conics, fixedX, fixedY, movingX, movingY, &outNewLength)` —
      storage-agnostic; long query `Seg`, `IntersectSegSeg`/`IntersectSegConic`, nearest hit beyond
      the current endpoint's parameter.
    - `FindExtendArcTarget(segs, conics, cx, cy, r, startRad, sweepRad, nearFirst, &outNewLength)`
      — storage-agnostic; full-circle `Conic` query, `IntersectSegConic`/`IntersectConicConic`,
      angular-distance-in-the-sweep's-own-direction nearest hit.
    - `StartExtendCommand`, boundary-pick dispatch (refuse Annotation/FeatureLine/Surface, dedupe,
      collect), Enter-advance (`SelectBoundaries`→`SelectTargets`, requires ≥1 boundary), target-pick
      dispatch (`LengthenEligibility` for eligibility/near-end/current-length, then
      `FindExtendLineTarget`/`FindExtendArcTarget`, then `ApplyLengthenToLine`/`ToArc`/
      `ToPolylineEnd`, refuse with a stated reason if no boundary reached).
    - `ApplyExtendToPaperEntity`-equivalent using `AppendPaperBoundaryShapes` +
      `FindExtendLineTarget`/`ArcTarget` + the paper `Apply*` mutation shapes LENGTHEN's paper path
      already established.
    - Full integration checklist wiring (Kind/KindName, `kRegistry` `{"extend","ex",...}`,
      typed dispatch, `SubmitViewportPickImpl` model+floating-model-space dispatch — the RECT-
      history site — `lastCommand`+`RepeatLastCommand`, `CancelActiveCommand` log line, one
      `PushUndoSnapshot` per extend positioned right before the mutation — inside the reused
      `ApplyLengthenTo*` functions already, so this is automatic, not a new site to get wrong).
  - `src/ui/CadUi.cpp` — footer hint (`SelectBoundaries`/`SelectTargets` phase text), ribbon button
    (new icon — EXTEND has no existing stub), autocomplete icon map, context menu,
    hover-suppression while boundary-picking (mirroring TRIM's `trimEntityPick` gate), and the
    paper-space `paperExtendPhase` click flow: boundary collection (click to add, refuse Text),
    **Enter key advances phase 1→2** (checked alongside the existing `ImGuiKey_Escape` handling in
    the same paper-click block MIRROR/LENGTHEN's paper phases already hook into), target-picking
    loop.
  - `src/viewport/TransformPreview.cpp` — `extendBoundaries` read as a selection while being
    picked, hover suppressed for anything already a boundary, matching TRIM's exact precedent.
- Test approach:
  - happy path: extend a line to a line boundary from each end (two transcripts' worth of "which
    end moves" coverage, or one transcript covering both); extend an open polyline's terminal
    segment to a boundary; extend an arc to a boundary circle (hand-computable case, same rigor as
    LENGTHEN's arc transcript — derive the expected new sweep by hand before trusting the
    transcript).
  - failure mode: a target that doesn't reach any boundary in the extending direction is refused,
    geometry unchanged; a Circle/closed-Polyline/full-circle-Arc target is refused with the reused
    `LengthenEligibility` message; an Annotation/FeatureLine/Surface boundary pick is refused.
  - undo/redo: plain `SAMEFILE` shape (EXTEND edits in place like LENGTHEN, allocates no new id) —
    confirm by running, not assuming (LENGTHEN's own self-verification found a real trap here
    once already, for MIRROR's duplicate-id case — different command, same caution warranted).
  - GUI-only (manual, no UI-automation driver exists): ribbon button, boundary-selection visual
    read-as-selection, mode-switch mid-loop n/a (EXTEND has no typed mode), paper-space and
    floating-model-space click routing, Enter-key phase advance in paper space specifically.
- Steps:
  - [x] `LengthenEligibility` cmdName parameterization
  - [x] `AppendModelBoundaryShapes`/`AppendPaperBoundaryShapes` + `FindExtendLineTarget`/
        `FindExtendArcTarget`
  - [x] `Kind::Extend` + phase state + `StartExtendCommand` + boundary/target viewport-click
        dispatch (verified first in `SubmitViewportPickImpl`, RECT precedent)
  - [x] Paper-space `paperExtendPhase` + Enter-key phase advance + click flow
  - [x] Footer hint + ribbon button (new) + autocomplete icon + context menu + hover-suppression +
        selection-highlight for boundaries + repeat-last + cancel-log
  - [x] Headless transcripts
  - [x] Build + headless run + full regression; manual GUI pass still pending (user)

## 7. Workflow-specific notes
- Feature: pre-flight design settled during planning; one question asked and answered (§4). No
  tests-first — this codebase's modify commands are verified by headless transcript + manual GUI
  pass, not Catch2 unit tests (`AppCommandState` cannot link into `GoSurveyTests`).

## 8. Implementation log
- 2026-08-24: REQ-103 EXTEND acceptance written (D-2026-08-24-b). Task opened, plan written. One
  Explore-agent research pass preceded this plan (TRIM's boundary-UI shape confirmed non-shared;
  `curveisect`'s analytic coverage and lack of any ray primitive confirmed; the long-query-segment/
  full-circle-conic technique chosen specifically to avoid adding one).
- 2026-08-24: Core commands-layer implementation (boundary-shape conversion, `FindExtendLineTarget`/
  `FindExtendArcTarget`, model+paper `HandleExtendViewportPick`/`ApplyExtendToPaperEntity`, full
  integration checklist) landed; intermediate build of all three targets green.
- 2026-08-24: UI wiring completed (ribbon button — 4th in the "Join/Mirror/Lengthen" group, its
  `colW` widened to include "Extend" at both call sites; autocomplete icon map; context-menu item;
  hover-suppression gate extended to cover `Kind::Extend`'s boundary/target picks alongside TRIM's,
  mirroring `trimEntityPick`; `TransformPreview.cpp`'s `BuildSelectionHighlight`/`BuildHoverHighlight`
  extended so `extendBoundaries` reads as a selection while being picked, matching `trimCutters`'
  exact precedent). Full three-target build green (MSVC toolchain had moved to a new install path,
  `Program Files\Microsoft Visual Studio\18\Community\...` — not `2022` — since the prior task; found
  and adjusted for, no code implication).
- 2026-08-24: **Bug found and fixed during self-verification design** (before any transcript was
  run): for a bent (3+ vertex) open Polyline target, both `HandleExtendViewportPick` (model) and
  `ApplyExtendToPaperEntity` (paper) built the boundary-search ray from the polyline's GLOBAL first
  and last vertices — but the actual mutation, `ApplyLengthenToPolylineEnd` /
  `ApplyLengthToPaperEntityMutation`'s Polyline case, extends the endpoint along the LOCAL last
  segment's own direction (the two vertices immediately adjacent to the moving end). For any polyline
  with a bend, those two directions differ, so the ray would find a boundary crossing the actual
  per-segment extension then overshoots past (hand-verified on the case now covered by
  `extend-line-and-polyline.txt`: chord-ray math would have landed the endpoint at x~511.4 against a
  boundary at x=500). Fixed by ray-casting from the SAME local fixed/moving vertex pair the mutation
  function itself uses, then converting the returned "new local segment length" into the "new total
  polyline length" currency `ApplyLengthenToPolylineEnd`/its paper equivalent already expect
  (`newLen = curLen(total) - localSegLen + newLocalSegLen`). Applied identically to both the
  model-space and paper-space code paths. Caught before any test ran — by re-deriving the geometry
  by hand while designing the polyline transcript, the same practice that caught TASK-095's arc-math
  gap.
- 2026-08-24: Two headless transcripts written and passing on first run after the fix above:
  `extend-line-and-polyline.txt` (Line extension in both directions against a shared boundary; the
  bent-Polyline case above, hand-verified to land exactly on its boundary post-fix; undo/redo
  SAMEFILE/DIFFERENTFILE oracle, 3 individual undo steps for 3 individual extends) and
  `extend-arc-and-refusals.txt` (Arc target — hand-computed 180°→270° sweep growth onto a boundary
  line, same rigor as TASK-095's arc transcript; no-boundary-reached refusal; Circle and closed-
  Polyline target refusals reusing `LengthenEligibility`'s existing messages verbatim). `cmake -S . -B
  build` reconfigured to pick up the new transcript files (`file(GLOB ...)` registration, per
  TASK-094/095 precedent) before either would run.
- 2026-08-24: Full regression: `ctest -C Debug` — 557/557 run tests passed (1 pre-existing disabled:
  `dxf-export-stable`), including both new EXTEND transcripts.

## 9. Self-verification
- [x] build-project        — PASS: clean MSVC build, all three targets (GoSurvey, gosurvey_headless,
      GoSurveyTests), zero errors; only pre-existing warning classes (C4244/C4456/C4457/C4530),
      none introduced by this task's new code.
- [x] architecture-review  — PASS: no new entity kind/store/dependency/global state; the one shared
      model/paper "nearest boundary hit" helper pair (`FindExtendLineTarget`/`FindExtendArcTarget`)
      has two genuine callers from the start, matching §3's boundary check; the geometry mutation is
      100% reused from TASK-095, not re-implemented.
- [x] code-review          — PASS: self-reviewed while designing the polyline transcript; found and
      fixed the local-vs-chord boundary-ray bug (§8) before any test ran. `LengthenEligibility`'s
      `cmdName` parameterization confirmed applied consistently (EXTEND passes `"EXTEND"` at both its
      model and paper call sites).
- [x] dependency-audit     — n/a (`curveisect` already an accepted, linked dependency; no new one added)
- [x] performance-review   — n/a (no hot-path change; one-shot per-pick command)
- [x] testing              — PASS: `extend-line-and-polyline.txt` and `extend-arc-and-refusals.txt`
      both pass; full regression `ctest -C Debug` 557/557 run tests green (1 pre-existing disabled).
      Manual GUI pass (ribbon rendering, live boundary-selection highlight, paper-space click/Enter
      flow) is left to the user — no UI-automation driver exists in this codebase.

## 10. Verification result
- Submitted:  2026-08-24 (self-run; solo task, no separate verification agent in this session)
- Verdict:    PASS (headless); manual GUI pass pending user confirmation
- Findings:   1 real bug found and fixed pre-test (bent-Polyline boundary-ray direction, §8) — not an
  open finding, already resolved and covered by a regression transcript.

## 11. Outcome
- Requirements satisfied: REQ-103 step 3/8 (Acceptance met: yes, pending the user's manual GUI pass)
- Tests added:            `tests/headless/transcripts/extend-line-and-polyline.txt`,
  `tests/headless/transcripts/extend-arc-and-refusals.txt`
- Refactors:              `LengthenEligibility` gains a `cmdName` parameter (backward-compatible;
  LENGTHEN's call site updated to pass `"LENGTHEN"` explicitly)
- Technical debt noted:   none new. (Carried forward from TASK-094: paper-space modify commands still
  can't offer a typed-value prompt, so EXTEND's paper path — like MIRROR/LENGTHEN's — is click-only;
  unchanged scope, no regression.)
- Docs updated:           spec/requirements.md (REQ-103), spec/project.md (D-2026-08-24-b), this task
- Done:                   yes (headless); manual GUI pass remains open with the user
