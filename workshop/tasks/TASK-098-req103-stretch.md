# TASK-098 — STRETCH modify command (REQ-103 step 5)

- Type:    feature
- Status:  done (headless); manual GUI pass pending
- Opened:  2026-08-24
- Owner:   Claude (agent)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         REQ-103 — Modify-command completeness (GOAL per spec/project.md D-2026-08-23-j)
- Requirements: REQ-103 (step 5 of 8, STRETCH) — must be `accepted` (it is)
- Constraints:  REQ-101 (±0.01 ft tolerance), REQ-201 (no silent failures), REQ-076/ADR-027
  (stable entity ids, never rolled back on undo)
- Acceptance:   restated verbatim from `spec/requirements.md` REQ-103 "Acceptance — STRETCH (step 5)":
  - crossing/window box first (L→R window/fully-inside, R→L crossing/overlap — REQ-039's rule),
    then base point, then destination (or typed relative displacement); one displacement applies
    to the whole selection in a single apply (MOVE/ROTATE/SCALE granularity, not a per-target loop);
  - every entity's "definition points" tested independently against the box; only in-box points move:
    - Line/Polyline (open or closed)/FeatureLine: every endpoint/vertex independent (FeatureLine
      elevation untouched, matching MOVE/ROTATE/SCALE's existing plan-only transform of it);
    - Arc: both endpoints tested. 0/2 in-box → no-op/whole translate. Exactly 1 in-box → true
      partial stretch (center/radius recomputed, included angle preserved). Full-circle-sweep Arc
      exempt, follows the Circle rule instead. Degenerate (new chord ~0) → refused, stated reason,
      arc left unchanged;
    - Circle/Ellipse (center only), Annotation/Text/Mtext/Dim (insertion point, dim extension
      points not independently tested), PdfUnderlay (insertion point), FilledRegion (one reference
      point, whole-region translate), SurveyPoint (its own point): one definition point each, moves
      as a whole only if that point is in-box;
    - Surface, Mesh excluded (existing transform restrictions);
  - a selected entity with no in-box definition point is a legitimate no-op, not a refusal;
  - one undo step for the whole apply;
  - works in model space, floating model space, and native paper space with TRUE per-point partial
    stretch in both (full parity, not simplified in paper space); a paper selection built by a plain
    click (not a box) degrades to whole-entity translate (no box to test points against);
  - the point-membership test uses plain world-XY (model) / paper-inch XY (paper), not
    camera-projected — stated, accepted simplification, recorded as technical debt;
  - reachable from the Modify ribbon, typed `STRETCH`, and right-click repeat.
- Owning subsystem: Commands (`src/commands/CadCommands.{hpp,cpp}`), UI wiring
  (`src/ui/CadUi.cpp`), preview (`src/viewport/TransformPreview.cpp`) — per spec/architecture.md,
  same subsystem MIRROR/LENGTHEN/EXTEND/BREAK were built in.

## 2. Scope
- In scope: model-space + paper-space STRETCH, full arc partial-stretch geometry, all entity types
  listed in Acceptance above, headless test coverage, full regression.
- Out of scope: FILLET/CHAMFER/ARRAY/EXPLODE (steps 6-8, not started); per-vertex FilledRegion
  boundary stretch (single reference point only, per Acceptance); camera-projected point-membership
  test under 3D orbit (documented simplification/debt).
- Smallest change: reuse `ComputeSelectionFromRect`/`SelectPaperEntitiesInBox` for entity candidacy
  unchanged; add one new per-point gate + one new arc-recompute function, reused by both spaces;
  new per-command phase/state fields following the exact pattern every prior step in this epic used.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] No — proceed. No new entity kind; no new dependency; no new persisted state (the new
          `AppCommandState` fields are transient, in-session command state, same category as every
          prior step's `paper*Phase` fields). `RecomputeArcFromEndpoints` has two concrete callers
          (model `userArcs`, paper `paperArcs`) from day one, not a speculative abstraction.
    - [ ] Yes → STOP.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Arc partial stretch: refuse the mixed one-endpoint-in-box case (simpler) vs. full AutoCAD parity (recompute center/radius preserving included angle)? | 2026-08-24 | Full AutoCAD parity |
| Q2 | Paper-space scope: whole-entity-only (matches ROTATE/SCALE today) vs. full crossing-window vertex-level parity with model space? | 2026-08-24 | Full parity |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: the per-point box-membership test operates in plain world-XY (model) / paper-inch XY
(paper) coordinates, not through ComputeSelectionFromRect's optional camera-projected screen-space
test (which still governs entity candidacy, unchanged).
- Because:       GoSurvey is a plan-based survey/civil CAD tool; 3D orbit is a secondary
                 visualization feature (REQ-057..061), not how survey geometry is normally edited.
- Risk if wrong: under an orbited 3D camera, a point could be screen-inside/world-outside the box
                 (or vice versa) at the margin, causing a stretch to include/exclude a vertex a user
                 watching the screen would not expect.
- Validate by:   recorded in REQ-103's STRETCH acceptance as a stated, accepted simplification, not
                 a silent gap; revisit only if a real user workflow needs 3D-orbit + STRETCH together.

ASSUMPTION-2: STRETCH is single-shot (one box-select, one base, one destination, done), not a
looping per-target command like BREAK/EXTEND/LENGTHEN.
- Because:       real AutoCAD's STRETCH operates this way (one selection, one displacement, applies
                 to everything in one step); it matches MOVE/ROTATE/SCALE's existing shape, not
                 TRIM/OFFSET's.
- Risk if wrong: low — this is well-established AutoCAD behavior, not a guess.
- Validate by:   consistent with REQ-103's own acceptance text ("one displacement applies to the
                 whole selection in a single apply").
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: see the approved plan at
  `C:\Users\chetj\.claude\plans\list-out-tasks-that-iridescent-stearns.md` for full design detail
  (arc-recompute formula with numeric verification, per-type point rules, model/paper command flow).
  Summary: reuse existing box-select for entity candidacy in both spaces; add a per-point
  in-box gate; add one shared `RecomputeArcFromEndpoints` for true arc partial stretch; new
  `ApplyStretchToSelection` (model) / `ApplyStretchToPaperSelection` (paper); new phase state
  (`stretchRectMnX/MxX/MnY/MxY` model, `paperStretchPhase` + `paperSelBoxLastValid` + rect paper);
  full integration-checklist wiring in `CadUi.cpp`/`TransformPreview.cpp`.
- Files/functions to touch:
  - `src/commands/CadCommands.hpp`: `Kind::Stretch`, new state fields.
  - `src/commands/CadCommands.cpp`: `RecomputeArcFromEndpoints`, `ApplyStretchToSelection`,
    `ApplyStretchToPaperSelection`, `StartStretchCommand`, `finishBox` new branch, parallel
    `K::Stretch` branches at every `K::Move`-adjacent dispatch site, full checklist wiring.
  - `src/ui/CadUi.cpp`: footer hint, ribbon button, autocomplete icon map, context menu, paper
    click-block additions (`closePaperSelBox` rect capture, `paperStretchPhase` branches, Escape
    reset), selection-invalidation in `ClearPaperEntitySelection`/`TogglePaperEntitySelection`.
  - `src/viewport/TransformPreview.cpp`: selection highlight during base/destination phases.
- Test approach: happy path = model/paper Line/Polyline true stretch (straddling box),
  whole-translate (fully inside), no-op (fully outside); Circle/Ellipse center-in/out; Arc
  whole-translate/no-op/true partial stretch (hand-verified numeric case) + full-circle-Arc via
  Circle rule; window vs crossing mode; paper click-select degrade-to-whole-move. Failure mode =
  degenerate-chord arc guard (refused, logged, unchanged) — transcript-tested if constructible by
  hand-picked points, else reviewed by hand and documented (BREAK's full-circle-Arc precedent).
- Steps:
  - [x] Spec update (REQ-103 STRETCH acceptance, decision log D-2026-08-24-d)
  - [x] Open this task file
  - [x] Implement `CadCommands.hpp` state additions
  - [x] Implement `RecomputeArcFromEndpoints` + hand-verify numerically before trusting it
  - [x] Implement model-space `ApplyStretchToSelection` + `StartStretchCommand` + dispatch wiring
  - [x] Implement paper-space `ApplyStretchToPaperSelection` + `closePaperSelBox`/click-block wiring
  - [x] Implement `CadUi.cpp` ribbon/hint/context-menu/autocomplete wiring
  - [x] Implement `TransformPreview.cpp` highlight wiring
  - [x] Write headless transcripts, hand-verify arc numeric cases before trusting them
  - [x] Build all three targets; run new transcripts; run full `GoSurveyTests` regression
  - [x] Self-verify (§9); write completion report (§11)

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1/Q2 above, via AskUserQuestion during plan mode); tests-first —
  transcripts written and hand-verified alongside implementation, run before self-verification.

## 8. Implementation log  (append as you work)
- 2026-08-24 — Task opened; spec updated (REQ-103 STRETCH acceptance, D-2026-08-24-d). Plan approved
  by user with full-parity scope on both open questions (arc geometry, paper-space parity).
- 2026-08-24 — Confirmed while planning tests: `tests/headless/HeadlessDriver.cpp` drives only
  `ProcessCommandLineSubmit`/`SubmitViewportPick` (model space). Paper-space interaction —
  including every prior REQ-103 step's paper path (MIRROR/LENGTHEN/EXTEND/BREAK) — lives entirely
  in `CadUi.cpp`'s per-frame ImGui paper click-block, which the headless driver never reaches (no
  paper-space verb exists in its grammar at all, confirmed by grep). This is pre-existing,
  epic-wide technical debt already implicitly accepted by every prior shipped step in this epic
  (each shipped with "manual GUI pass pending" covering paper space), not something new to
  STRETCH. Paper-space STRETCH is therefore verified by code review only (it reuses the identical
  `StretchOneArc`/`RecomputeArcFromEndpoints` functions the model path uses and tests) — headless
  transcript coverage in this task is model-space only, consistent with precedent.
- 2026-08-24 — Caught before any code was written: the first Acceptance draft omitted FeatureLine
  entirely. `ComputeSelectionFromRect` already treats FeatureLine as a normal whole-entity-selectable
  type (bbox test, same as Polyline/Arc), so it would have been silently picked up by STRETCH's
  box-select and then done nothing (no case in the apply function, no log) — a REQ-201 violation.
  Fixed by adding it to the vertex-independent bucket alongside Line/Polyline (structurally a CSR
  vertex list, same as Polyline; elevation left untouched, matching MOVE/ROTATE/SCALE's existing
  `TransformSelectedFeatureLinesInPlace`) before writing any implementation.

- 2026-08-24 — First transcript run failed: Part C1's WINDOW box-select (expected to select
  nothing) actually left a non-empty `st.selection`. Root cause: `ComputeSelectionFromRect` only
  ADDS hits to `st.selection` in add mode — it never clears stale entries first (by design, so a
  real drag-select tool can accumulate across several boxes). `StartStretchCommand`'s own stated
  design ("STRETCH always runs a fresh box-select") had not actually been backed by a selection
  clear, so leftover entities from a PRIOR command (here, Part B's polyline, still selected after
  its own STRETCH completed) were silently re-evaluated against Part C1/C2's box and displacement
  too. Fixed by adding `ClearSelection(st)` to `StartStretchCommand`'s model-space branch, right
  before entering `PickSelection` — makes every STRETCH invocation start from a verifiably clean
  selection, matching the design intent exactly rather than merely stating it.
- 2026-08-24 — `RecomputeArcFromEndpoints` moved to be `inline` in `CadCommands.hpp` (definition,
  not just declaration) rather than living in `CadCommands.cpp`, so it is unit-testable without
  linking the whole command layer — the same reasoning `ExtendedGeometryGateTests.cpp`/
  `SurfaceRebuildLifetimeTests.cpp` already established for other header-only pure functions.
  Added `tests/StretchGeomTests.cpp` (Catch2, registered in `CMakeLists.txt`): four cases — the
  quarter-circle one-endpoint-moved case (hand-derived numerically in the plan), an independent
  semicircle case (`cot(90°)=0`, center must equal the chord midpoint — a different branch of the
  same formula), the both-endpoints-shifted-equally degenerate-to-pure-translation case, and the
  collapsing-chord refusal case (arc left byte-unchanged). All four pass. `StretchOneArc` (the
  box-gating wrapper) stays in `CadCommands.cpp` — its own logic is simple enough to trust via
  code review plus transcript coverage, not worth the same extraction.
- 2026-08-24 — Two more transcript-authoring findings, both about pre-existing behavior discovered
  while writing tests, not bugs in this task's own code: (1) `CircleIntersectsAABB`
  (`src/util/geom2d.cpp`) is curve-precise — a crossing box entirely INSIDE a circle's disk, not
  touching its circumference, does NOT select it (by design, per that function's own comment,
  fixing a past bug where the filled-disk test over-selected). My first circle test box sat wholly
  inside the circle and was silently not selected; fixed by sizing the box so at least one corner
  is genuinely outside the circle's radius while the center stays inside. Ellipse/Arc candidacy, by
  contrast, use a plain bounding-box overlap test (confirmed by reading `ComputeSelectionFromRect`
  directly) — no such care needed there. (2) ARC's 3 points have no typed-coordinate
  (`CMD x,y`) handler in `ProcessCommandLineSubmit` — only `PICK` (the viewport-pick path,
  `CommitArcThreePoints`) reaches them, unlike LINE/CIRCLE/POLYLINE which all support both. A
  pre-existing ARC gap, out of scope for this task; documented in the transcript itself so a future
  transcript author doesn't lose the same time rediscovering it.
- 2026-08-24 — Full regression: `ctest` — 565/565 passed (1 pre-existing disabled test,
  `dxf-export-stable`, unrelated), including both new STRETCH transcripts and all four
  `StretchGeomTests.cpp` cases. Build clean on all three targets (GoSurvey, gosurvey_headless,
  GoSurveyTests) — no new compiler warnings beyond pre-existing, unrelated ones (checked by diffing
  the warning list against files/lines this task did not touch).

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS (clean build, all three targets, MSVC via vcvars64; no new
      warnings attributable to this task's changes)
- [x] architecture-review  — PASS (no Workshop architectural decision; boundary check §3 stands —
      no new entity kind/dependency/persisted state; `RecomputeArcFromEndpoints` has 3 concrete
      callers — model apply, paper apply, live preview — plus a 4th, the unit test, from day one)
- [x] code-review          — PASS. Self-reviewed the full diff: per-type gating mirrors
      `ApplyTranslationToSelection`'s existing shape exactly (model) and
      `TranslateSelectedPaperEntities`'s one-loop-switch shape exactly (paper), so no new pattern
      was invented where an existing one already fit. Two real bugs were found and fixed during
      this same self-review/test pass (see log): the `ClearSelection` omission, and the initial
      FeatureLine acceptance-text gap (caught before any code existed). Undo granularity (one
      `PushUndoSnapshot` per apply, placed immediately before mutation) matches MOVE's own
      convention, avoiding LENGTHEN's own past ordering bug. `StretchOneArc`'s full-circle guard
      reuses BREAK's own `kTwoPi`/`1e-4f` tolerance constant verbatim rather than inventing a new one.
- [x] dependency-audit     — n/a (no new dependency; `src/util/geom2d.cpp` was already linked into
      the main GoSurvey target, only newly added to `GoSurveyTests`' own source list for the unit
      test — a build-time-only addition, not a runtime dependency change)
- [x] performance-review   — n/a (STRETCH runs once per user apply, not a per-frame hot path; the
      live preview block in `TransformPreview.cpp` runs once per frame only while `NeedDestination`
      is active during an interactive drag, the same cost class MOVE/COPY's existing preview block
      already pays)
- [x] testing              — PASS. Happy path: model Line/Polyline true stretch, whole-translate,
      no-op, window-vs-crossing candidacy contrast, Circle/Ellipse center-in/out, Arc true partial
      stretch + whole-translate, all SAMEFILE-verified against independently hand-built expected
      geometry where the values are rational (lines/polylines/circles/ellipses); the arc formula
      itself is exactly pinned by 4 unit-test cases rather than transcript-approximated. Failure
      mode: the degenerate-chord arc-collapse guard is unit-tested directly (`StretchGeomTests.cpp`)
      rather than transcript-constructed, since hand-picking box/displacement values that drive two
      arc endpoints onto the exact same point is impractical the same way BREAK found full-circle
      arc construction impractical. Paper-space STRETCH is code-reviewed only, not transcript-run —
      pre-existing harness limitation affecting every paper-space path in this whole epic, not new.

## 10. Verification result
- Submitted:  2026-08-24
- Verdict:    PASS (headless + unit tests; manual GUI pass pending the user, same as every prior
  step in this epic)
- Findings:   2 findings, both fixed during self-verification, neither open — (1) FeatureLine
  missing from the Acceptance text, caught before any implementation existed; (2) `StartStretchCommand`
  missing a `ClearSelection` call, caught by the first transcript run failing, root-caused via code
  reading (`ComputeSelectionFromRect`'s add-only accumulation behavior) rather than patched blind.

## 11. Outcome
- Requirements satisfied: REQ-103 step 5 (STRETCH) — Acceptance met: yes, pending only the user's
  manual GUI pass (ribbon/live-preview/click-flow — this project's own no-UI-automation constraint,
  same as every prior step)
- Tests added:            `tests/StretchGeomTests.cpp` (4 Catch2 cases, arc-formula pin);
  `tests/headless/transcripts/stretch-lines-polylines.txt` (Line/Polyline/window-vs-crossing);
  `tests/headless/transcripts/stretch-circle-ellipse-arc.txt` (Circle/Ellipse/Arc)
- Refactors:              `RecomputeArcFromEndpoints` moved from `CadCommands.cpp` into
  `CadCommands.hpp` as an inline, header-only function (for unit-testability without linking the
  whole command layer — not a behavior change, same reasoning several pre-existing pure-math
  extractions in this codebase already follow)
- Technical debt noted:   (a) paper-space STRETCH has no headless transcript coverage — inherited,
  epic-wide limitation of the headless harness itself (paper interaction lives in `CadUi.cpp`'s
  ImGui click-block, unreachable by `ProcessCommandLineSubmit`/`SubmitViewportPick`), not new to
  this task; removal condition: the headless driver gains a paper-space verb. (b) full-circle-sweep
  Arc's STRETCH path (Circle-style center-only rule) is reviewed, not transcript-tested — same
  reasoning and same removal condition as BREAK's identical gap (TASK-097). (c) the point-membership
  test uses plain world/paper-inch XY, not camera-projected, under an orbited 3D model-space camera —
  stated, accepted simplification recorded in REQ-103's own acceptance text, not hidden.
- Docs updated:           `spec/requirements.md` (REQ-103 STRETCH acceptance + FeatureLine
  correction + revision line + traceability row), `spec/project.md` (D-2026-08-24-d)
- Done:                   2026-08-24 (headless); manual GUI pass pending the user
