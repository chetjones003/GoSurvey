# TASK-109 — ARRAY command (rectangular + polar)

- Type:    feature
- Status:  done
- Opened:  2026-08-25
- Owner:   Claude (workshop)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         GitHub issue #87
- Requirements: REQ-304   ← accepted
- Constraints:  none beyond the standing CON-07 (build artifacts stay out of the source tree,
  unaffected by this task)
- Acceptance:   (verbatim from REQ-304)
  1. `ARRAY` is launchable by typed command and offers `[R]ectangular`/`[P]olar` as both a typed
     letter and a clickable command-line token, both invoking the same start-array-type function;
  2. object selection accepts a pre-existing selection or a window/crossing box, matching
     MOVE/COPY's own selection step; Surface/Mesh/PdfUnderlay/survey points are dropped from the
     array selection with a log line naming the count and reason, matching the existing
     MIRROR/MOVE exclusion wording style;
  3. Rectangular: columns, column spacing, rows, row spacing are each settable by typed number or
     interactive cursor distance; confirming produces `columns × rows` total instances positioned
     on a regular grid, cell (0,0) at the original selection's position, with correct spacing along
     both axes for both positive and negative spacing (grows the opposite direction);
  4. Polar: center point accepts click, typed X,Y, and object snap; item count N produces exactly N
     total instances (the original plus N-1 new copies) evenly spaced across the fill angle;
     360° places the last instance at 360°/N before wrapping (no duplicate instance at 0°==360°);
     a partial angle (e.g. 180°) spaces N instances evenly across that arc; Rotate-items = Yes turns
     each copy's orientation with its position, Rotate-items = No keeps every copy at the source
     orientation;
  5. the preview (ghost lines/circles via `TransformPreview`, matching MOVE/COPY's existing preview
     coverage — LineSeg/Circle/Arc/Ellipse/Polyline/FeatureLine) updates immediately as any
     parameter changes and commits no geometry until confirmed;
  6. `CommandInputHint` returns an ARRAY-specific prompt for every phase (select objects, array
     type, columns, column spacing, rows, row spacing, center point, item count, angle to fill,
     rotate behavior), matching the existing per-phase-hint pattern;
  7. ESC at any phase before confirmation cancels with a log line (matching `CancelActiveCommand`'s
     existing per-command messages), discards the preview, and leaves the original geometry and
     selection unchanged — no partial array remains;
  8. confirming an array pushes exactly one undo snapshot for the whole operation; Ctrl+Z after a
     completed array removes every generated instance and leaves the source objects exactly as
     before the command; the source objects are never deleted or modified by ARRAY;
  9. every entity type MOVE/COPY can duplicate (LineSeg, Circle, Arc, Ellipse, Polyline, Annotation,
     FilledRegion, FeatureLine) is duplicated correctly by ARRAY, including types not covered by
     the live preview (Annotation, FilledRegion — consistent with MOVE/COPY's own preview gap).
- Owning subsystem: Commands (`src/commands/CadCommands.{hpp,cpp}`), with the preview/hint/routing
  seams in Viewport (`src/viewport/TransformPreview.cpp`, `src/viewport/ViewportPickPolicy.hpp`,
  `src/ui/CadUi.cpp`'s `CommandInputHint`)

## 2. Scope
- In scope: `ARRAY` command, object selection (reusing the existing modify-command selection
  step), rectangular arrays (columns/rows/column spacing/row spacing), polar arrays (center/item
  count/fill angle/rotate toggle), dynamic preview, dynamic cursor/command-line prompts, clickable
  + keyboard command variants, ESC cancellation, one-undo-step commit, filtering of survey points
  and the pre-existing Surface/Mesh/PdfUnderlay exclusions.
- Out of scope (per issue #87 and D-2026-08-25-k): path arrays, associative/editable arrays,
  advanced 3D arrays, post-creation array property editing, survey-point arraying (N-way ID
  resolution is new scope this issue does not require).
- Smallest change: a new `AppCommandState::Kind::Array` following the exact shape of the five
  sibling modify commands, reusing `DuplicateCadSelectionTranslated`/`DuplicateCadSelectionRotated`
  in a loop (both already exist and already do per-type duplication for every entity kind ARRAY
  needs) rather than writing new duplication logic. Rectangular commits by looping the *existing*
  translate-duplicator once per non-origin grid cell inside one `PushUndoSnapshot`. Polar
  Rotate=Yes loops the *existing* rotate-duplicator once per item. Polar Rotate=No loops the
  *existing* translate-duplicator with a per-item (dx,dy) computed by rotating one fixed anchor
  point (the first selected entity's own representative point — a ~15-line helper in the same
  per-type-switch style already written three times for Translated/Rotated/Reflected) around the
  center and taking the delta — a rigid-body translation, so every entity keeps its own orientation
  for free with no new per-type rotation-suppression code.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change /
  global state / public-API or data-format change / algorithm the spec didn't
  specify?
    - [x] No — proceed. New `Kind` enum value + phase enums + state fields (data, not an
          abstraction), reusing four existing mechanisms verbatim: `DuplicateCadSelection{Translated,
          Rotated}` (duplication), `TransformPreview.cpp`'s `BuildTransformPreview` (ghost preview),
          `CommandInputHint` (per-phase prompt), `RenderClickableCommandHint`'s `[TOKEN]` syntax
          (keyboard+click command variants). No new store, no new persistence, no new dependency, no
          new global. `ViewportClickRouteFor`'s exhaustive switch (`ViewportPickPolicy.hpp`) forces a
          routing decision for every new phase at compile time — the one place a missing case would
          be a silent bug, and it cannot be a silent bug here because it won't compile.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | GoSurvey has no block/attribute entity type — how should ARRAY treat survey points, the closest analog, given their existing duplication path is a one-off ID-conflict modal that doesn't scale to N array instances? | 2026-08-25 | Exclude survey points from the array selection (filtered like `Surface`, logged, not silently dropped) — recorded as D-2026-08-25-k. Re-confirmed directly with the user in the live conversation (D-2026-08-25-k addendum): exclude for now; GoSurvey will gain a block/attribute entity type in the future, at which point this exclusion should be revisited. |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: Rectangular and polar each commit immediately once their last parameter (row
spacing / rotate-answer) is supplied — no separate "confirm" step after all parameters are visible.
- Because:       every existing multi-step modify command in this codebase (MOVE/COPY/ROTATE/
  SCALE/MIRROR) commits on its last required value; none has a distinct "preview, then a separate
  confirm" phase. Matching that keeps ARRAY's shape identical to its five siblings.
- Risk if wrong: the issue's mockup shows "Define parameters → See live preview → Confirm" as
  separate boxes, which could mean the user expects to keep tweaking values after all four/four are
  entered, before a final explicit confirmation.
- Validate by:   the acceptance text only requires the preview to update "as the user changes the
  array parameters" during entry, which this design satisfies; ship this shape and take user
  feedback from the manual GUI pass rather than pre-building an editable-after-entry flow with no
  precedent in the codebase.

ASSUMPTION-2 (superseded during implementation — kept for the record): originally planned as a new
"first selected entity's own point" helper. Research turned up an EXISTING general-purpose
`ComputeSelectionCentroidWorld(st, &cx, &cy)` (CadCommands.cpp) already used by ROTATE's reference
path, covering every relevant type including feature lines. Reused as-is for both the rectangular
grid's spacing-anchor point and the polar Rotate=No anchor — no new helper was needed at all.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: add `AppCommandState::Kind::Array` with its own phase enum (`ArrayPhase`) and an
  `ArrayType` (Rectangular/Polar) enum, following MOVE/COPY/ROTATE's exact state-machine shape.
  Reuse existing selection (`PickSelection`/`SelectionBox` routing), existing per-type duplicators
  in a loop for commit, and existing preview/hint/clickable-variant mechanisms extended with an
  ARRAY branch each.
- Files/functions to touch:
  - `src/commands/CadCommands.hpp`: `Kind::Array` + `KindName` case; `ArrayType`, `ArrayPhase`
    enums; array parameter fields (`arrayType`, `arrayPhase`, `arrayCols`, `arrayRows`,
    `arrayColSpacing`, `arrayRowSpacing`, `arrayCenterX/Y`, `arrayItemCount`,
    `arrayFillAngleDeg`, `arrayRotateItems`, `arrayAnchorX/Y` cached at type-selection time).
  - `src/commands/CadCommands.cpp`:
    - `StartArrayCommand` (mirrors `StartMoveCommand`'s shape: pre-existing-selection fast path,
      else window-select prompt).
    - `DropArrayUnsupportedFromSelection` (mirrors `DropMirrorUnsupportedFromSelection`; calls
      `DropSurfacesFromSelectionForTransform` for Surface, drops Mesh/PdfUnderlay with a log line,
      and clears/logs `st.selectedSurveyPointIndices` — the D-2026-08-25-k decision).
    - `FirstSelectionAnchorPoint` (ASSUMPTION-2 helper).
    - `HandleArrayText` (typed command-line entry per `arrayPhase`: type-choice letters, integers
      for columns/rows/item count, numeric distance for spacings, `ParseStoragePoint` for the
      polar center, degrees for fill angle via the existing `ParseAngleDegreesInternal`, Y/N for
      rotate) — wired into `ProcessCommandLineSubmit`'s command dispatch alongside
      `HandleModifyText`.
    - `CommitArrayRectangular` / `CommitArrayPolar`: one `PushUndoSnapshot`, then loop calling
      `DuplicateCadSelectionTranslated`/`DuplicateCadSelectionRotated` per instance.
    - Command-name dispatch: `primary == "array"` → `StartArrayCommand`.
    - `CancelActiveCommand`: `Kind::Array` case + reset (extend `ResetModifyRotateDraft` or add a
      small `ResetArrayDraft` called alongside it).
    - Viewport click handling (the `st.active == K::...` chain near line 8850-8970): `Kind::Array`
      branch covering `PickSelection` (window-select, same shape as Move/Copy), `Polar_WaitCenter`
      (point commit), `Rect_WaitColumnSpacing`/`Rect_WaitRowSpacing`/`Polar_WaitAngle` (interactive
      click-to-set, same shape as OFFSET's distance-or-click).
  - `src/viewport/ViewportPickPolicy.hpp`: add `Kind::Array` to the exhaustive switch in
    `ViewportClickRouteFor` (`SelectionBox` while `PickSelection`; `SnappedPointPick` for
    `Polar_WaitCenter`/`Rect_WaitColumnSpacing`/`Rect_WaitRowSpacing`/`Polar_WaitAngle`; `Ignore`
    for the typed-only phases: `WaitType`, `Rect_WaitColumns`, `Rect_WaitRows`,
    `Polar_WaitItemCount`, `Polar_WaitRotateAnswer`) and to
    `ViewportUseRawWorldForSelectionRectPick` (`PickSelection` phase, matching Move/Copy/Scale).
  - `src/viewport/TransformPreview.cpp`: `BuildTransformPreview` gains a `K::Array` branch —
    rectangular renders the full current grid (fixed cols/rows/spacings so far entered, live cursor
    value for whichever spacing is being dragged); polar renders the full current ring (fixed
    center/count/angle-so-far, live cursor angle while `Polar_WaitAngle` is active), reusing the
    same per-type LineSeg/Circle/Arc/Ellipse/Polyline/FeatureLine append logic the Move/Copy branch
    already has (looped per instance instead of once).
  - `src/ui/CadUi.cpp`: `CommandInputHint` gains an ARRAY branch (one prompt string per
    `ArrayPhase`, `[R]ectangular`/`[P]olar` and `[Y]es`/`[N]o` as bracket tokens for
    `RenderClickableCommandHint`).
  - `tests/ViewportPickPolicyTests.cpp`: add `K::Array` to `kPickDriven` (at its first prompt,
    `PickSelection` → not `Ignore`) and a phase-by-phase `SECTION` mirroring MIRROR's.
- Test approach:
  - happy path: a headless CLICK/CMD-driven transcript (matching the `regression-80-*` precedent)
    exercising: (a) rectangular — select, `array`, `r`, columns, column spacing (typed), rows, row
    spacing (click), assert the resulting entity count and grid positions; (b) polar 360° — select,
    `array`, `p`, center, item count, `360`, `y`, assert N instances evenly spaced and rotated;
    (c) polar partial angle + Rotate=No — assert positions on the arc with unrotated orientation;
    (d) Ctrl+Z after each removes every generated instance and restores the exact pre-array state.
  - failure mode: ESC at each phase leaves entity count and selection unchanged (no partial array);
    selecting only a Surface/Mesh/PdfUnderlay/survey point reports "nothing to array" without
    crashing; a zero/negative column or row count is refused with a message, not a crash or an
    empty array silently created.
- Steps:
  - [x] step 1 — `Kind::Array` + state fields (`CadCommands.hpp`)
  - [x] step 2 — `StartArrayCommand`, `DropArrayUnsupportedFromSelection`, dispatch wiring, `CancelActiveCommand`
  - [x] step 3 — `HandleArrayText` (typed entry, all phases)
  - [x] step 4 — viewport click routing (`ViewportPickPolicy.hpp` + the click-handling chain in `CadCommands.cpp`)
  - [x] step 5 — `CommitArrayRectangular` / `CommitArrayPolar` (+ `ComputeSelectionCentroidWorld` reused per ASSUMPTION-2, no new helper)
  - [x] step 6 — `BuildTransformPreview` ARRAY branch
  - [x] step 7 — `CommandInputHint` ARRAY branch
  - [x] step 8 — `ViewportPickPolicyTests.cpp` coverage
  - [x] step 9 — headless regression transcript(s) for REQ-304's acceptance list
  - [x] step 10 — build clean, full test suite green, self-verification checklist

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1, D-2026-08-25-k). Tests-first where practical: the
  `ViewportPickPolicyTests.cpp` additions and headless transcript were written alongside each step
  rather than fully after, per the REQ-303/TASK-108 precedent this task follows.

## 8. Implementation log  (append as you work)
- 2026-08-25 — REQ-304 written and accepted; TASK-109 opened; plan above written before any code.
- 2026-08-25 — Steps 1-9 implemented per the plan above. `build/GoSurvey.exe` and `gosurvey_headless`
  built clean; a stale editor/LSP diagnostic ("HandleArrayText is ambiguous") did not reflect the
  actual compiled state — the built binaries post-date the source and ninja reports the build
  current, confirmed by a real MSVC build.
- 2026-08-25 — Verification pass found two bugs in `regression-87-array.txt` itself (not the
  product code): (1) the negative-spacing rectangular sub-test asserted `LINES 8` where the
  correct total is 7 (2 columns × 1 row = 2 total instances = 1 new copy on top of the existing 6
  lines, not 2 new copies) — an authoring off-by-one, fixed. (2) `BOX` selection in this codebase
  is additive (`ComputeSelectionFromRect` merges hits into `st.selection`, never replaces it — the
  headless `ESC` verb's `CancelActiveCommand` is a no-op while idle and does not clear it either;
  only `UNDO`/`REDO` clear `st.selection`). The original single-`NEW` transcript re-used one
  drawing across the rectangular and polar scenarios, so the polar section's first `BOX` silently
  inherited the rectangular section's still-selected line, and a later re-`BOX` after `UNDO`/`REDO`
  would have picked up all coincident array copies too (the 360° test's array center matched its
  own circle's center). Restructured the transcript into four independent `NEW`-scoped scenarios
  (rectangular, polar-360, polar-partial-angle, each with its own ESC-cancel check) and moved the
  polar test circle off its array center so "distributed around the center" is actually exercised
  instead of four coincident copies. Re-ran; `headless.regression-87-array` and the full suite
  (594/594) now pass.
- 2026-08-25 — Q1 (survey-point exclusion) was re-put to the user directly in the live conversation
  (the original exchange happened inside an autonomous pass with no live user turn). Confirmed as
  answered: exclude for now; addendum recorded in `spec/project.md` (D-2026-08-25-k addendum),
  noting a future block/attribute entity type should prompt revisiting this exclusion.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — PASS (MSVC/ninja, vcvars64.bat; `GoSurvey.exe` and `gosurvey_headless` clean)
- [x] architecture-review  — PASS (no Workshop architectural decision; see §3 — reuses 4 existing mechanisms)
- [x] code-review          — PASS (correctness, simplicity, ownership, readability — read every changed
      function against its sibling MOVE/COPY/ROTATE/MIRROR counterpart; found and fixed 2 test-authoring
      bugs, no product-code defects)
- [x] dependency-audit     — n/a (no dependency change)
- [x] performance-review   — n/a (array size is user-typed and small; no hot-path/per-frame code touched
      beyond the existing per-frame preview pattern every sibling command already uses)
- [x] testing              — PASS (594/594 ctest cases green, including the new
      `headless.regression-87-array` transcript covering REQ-304 acceptance 1-9: rectangular
      count/spacing incl. negative spacing, polar 360° incl. no 0==360 duplicate, polar partial-angle
      with Rotate=No, ESC cancellation for both types, one-undo-step removal verified by `.gs` diff)

## 10. Verification result
- Submitted:  2026-08-25
- Verdict:    PASS
- Findings:   2 found and resolved during self-verification, both in the test transcript, not the
  product code — see Implementation log 2026-08-25 (verification pass) entry.

## 11. Outcome
- Requirements satisfied: REQ-304 (Acceptance met: yes — all 9 acceptance points covered by
  `headless.regression-87-array.txt` plus code inspection of the preview/hint/cancel paths)
- Tests added:            `headless.regression-87-array`, `ViewportPickPolicyTests.cpp`
  "ARRAY box-selects first, then routes its spatial phases and ignores its typed-only ones"
- Refactors:              none
- Docs updated:           spec/requirements.md (REQ-304), spec/project.md (D-2026-08-25-k + addendum)
- Manual GUI pass:        not done this session (headless/automated verification only — dynamic
  preview rendering, click-to-drag column/row spacing, and clickable `[R]`/`[P]`/`[Y]`/`[N]` command-
  line tokens should get a human visual check per this project's "GUI hover is not automatable" note)
- Done:                   2026-08-25
```
