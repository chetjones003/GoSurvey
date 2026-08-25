# TASK-095 — LENGTHEN command (REQ-103 step 2 of 8)

- Type:    feature
- Status:  done
- Opened:  2026-08-24
- Owner:   Workshop

## 1. Authority
- Goal:         GOAL-01 (a working surveyor's CAD)
- Requirements: REQ-103 (accepted 2026-08-23, D-2026-08-23-j; LENGTHEN's acceptance written 2026-08-24, D-2026-08-24-a) — this task delivers step 2 (LENGTHEN) of 8
- Constraints:  CON-07 (build reproducibility); REQ-101 (numerical tolerance); REQ-201 (no silent failures)
- Acceptance:   verbatim from REQ-103's LENGTHEN acceptance:
  - eligible entities are Line, open Polyline, and a non-full-circle Arc; Circle, Ellipse, closed Polyline, a full-circle Arc, and every non-length-bearing entity kind are refused with a stated reason;
  - DElta/Percent/Total/DYnamic each resolve to one target length, applied to the end nearest the pick, holding the other end fixed, within REQ-101 tolerance;
  - DYnamic mode previews live as the cursor moves and commits on click/typed length;
  - a target length that would collapse an entity to ~0 length, or push an Arc past a full circle, is rejected with a message;
  - mode persists across repeated picks; the command loops back to "select object" after each apply until Enter/Esc; one undo step per individual apply;
  - reachable from ribbon, typed `LENGTHEN`/`LEN`, right-click repeat; works on model-space and native paper-space Line/Arc/open-Polyline entities.
- Owning subsystem: Commands (`src/commands/CadCommands.cpp`/`.hpp`) + UI (`src/ui/CadUi.cpp`, `src/viewport/TransformPreview.cpp`)

## 2. Scope
- In scope: LENGTHEN end-to-end per the acceptance above, and every integration site a modify
  command touches in this codebase (checklist established by TASK-094/MIRROR, §6).
- Out of scope: EXTEND/BREAK/STRETCH/FILLET/CHAMFER/ARRAY/EXPLODE (REQ-103 steps 3–8); an
  Angle sub-option for DElta on arcs (AutoCAD offers it, this task ships length-delta only —
  smallest sufficient change); a "hover to report current length" info mode.
- Smallest change: no new entity kind, no new store, no schema change. Adds one new `Kind`, its
  phase/mode state, per-type length/endpoint free functions written for reuse by EXTEND/FILLET,
  and the same 14-site integration checklist TASK-094 established.

## 3. Architectural boundary check
- [x] **No — proceed.**
  - New abstraction/layer/dependency: none — reuses the OFFSET/TRIM/SCALE-shaped command pattern.
  - New global mutable state: none — phase/mode fields live on the existing per-command `AppCommandState`.
  - Ownership change: none.
  - Public-API / data-format change: none — no `.gs`/DXF schema touched; LENGTHEN edits existing
    entity kinds in place, creates nothing new.
  - Upward dependency: none — stays inside Commands/UI.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| (none) | Design decisions (eligible entities, near-end math, sub-mode unification via `newLength`, per-apply undo granularity) were resolved during research/planning per REQ-103's own written acceptance rather than requiring a user round-trip — precedent (TRIM/OFFSET/SCALE) was decisive enough that no ambiguity remained. | 2026-08-24 | n/a |

## 5. Assumptions
```
ASSUMPTION-1: LENGTHEN excludes FeatureLine entirely (not offered as an eligible type), matching
              AutoCAD (which has no feature-line concept) and REQ-103's LENGTHEN acceptance text,
              which lists it among the refused non-length-bearing kinds.
- Because:       every precedent found (TRIM, OFFSET, grip editing) treats length-changing
                 operations as a Line/Arc/Polyline concern only; feature lines are a distinct
                 REQ-087 store with their own elevation-editor workflow, not a drafting primitive.
- Risk if wrong: a user wants to lengthen a feature line's terminal segment — low risk, easy
                 follow-up REQ if asked for; feature lines are edited through their own elevation
                 editor (REQ-088) today, not general modify commands.
- Validate by:   ask if a user requests it.

ASSUMPTION-2: DYnamic mode's live preview is a straight-line/arc-angle projection of the cursor
              onto the entity's own direction (clamped to a minimum positive length), not a
              snapped/ortho-aware drag.
- Because:       matches CadScalePreviewFactor/CadOffsetAppendLivePreview's existing "plain
                 cursor-to-value" precedent; adding ORTHO/snap awareness to a length-only drag is
                 new scope beyond what any sub-mode needs (ORTHO already constrains direction
                 elsewhere; LENGTHEN never changes direction, only length along a FIXED direction).
- Risk if wrong: none functionally — a snap-aware version would compute the same length value
                 whenever the projected point happens to be snap-worthy anyway.
- Validate by:   manual GUI test.
```

## 6. Plan
- Approach: model LENGTHEN's outer shape on OFFSET's single-entity pick-dispatch-commit loop
  (repeat-until-Enter, not MIRROR/ROTATE/SCALE's selection-then-transform), its sub-mode switch on
  SCALE's `HandleScaleText`, and its nearest-endpoint math on TRIM's Line tie-break extended to Arc.
- Files/functions to touch:
  - `src/commands/CadCommands.hpp` — `Kind::Lengthen`; `LengthenMode` enum
    (`Delta, Percent, Total, Dynamic`); `LengthenPhase` enum
    (`WaitSelectOrMode, WaitDeltaValue, WaitPercentValue, WaitTotalValue, WaitDynamicTarget`);
    fields for the current mode + its value, and the pending picked entity/near-end for Dynamic
    mode.
  - `src/commands/CadCommands.cpp`:
    - `ArcLengthOf(r, sweepRad)`, `PolylineOpenLengthOf(st, pi)` — new pure free functions,
      written for reuse by EXTEND/FILLET (REQ-103's own stated reason LENGTHEN ships before them).
    - `NearestLineEnd`/`NearestArcEnd`/`NearestPolylineEnd` — squared-distance-to-computed-endpoint
      tie-break, one shared mental model across all three types.
    - `ApplyNewLengthToLine`/`ApplyNewLengthToArc`/`ApplyNewLengthToPolylineEnd` — the per-type
      apply, holding the fixed end still; Arc case holds `r` fixed and changes only
      `startRad`/`sweepRad` (deliberately NOT reusing `ApplyEntityGripPoint`'s Arc case, which
      re-fits radius — wrong model here, see ASSUMPTION discussion in the plan).
    - `StartLengthenCommand`, `HandleLengthenText` (mode-letter dispatch + per-mode value phases),
      `SubmitLengthenViewportPick` (pick → determine near end → compute `newLength` → apply →
      loop back to `WaitSelectOrMode`).
    - Native paper-space equivalent — new, no existing paper LENGTHEN to extend (unlike MIRROR,
      which had ROTATE's paper path to model); same per-type math against `PaperLayout`'s
      `paperLines`/`paperArcs`/`paperPolyVerts`.
    - Full 14-site integration checklist (Kind/KindName, typed dispatch + `kRegistry` entry
      `{"lengthen","len",...}`, model+floating-model-space viewport-click dispatch in
      `SubmitViewportPickImpl` — the RECT-history site — paper-space click routing,
      `lastCommand`+`RepeatLastCommand` case, `CancelActiveCommand` log line, one
      `PushUndoSnapshot` per apply).
  - `src/ui/CadUi.cpp` — dynamic-input trio (`CommandInputHint`/`CommandExpectsPointEntry`/
    `CadPointPromptLabel`), footer hint, new ribbon button (Modify section — LENGTHEN has no
    existing disabled stub, unlike MIRROR), autocomplete icon map entry, context menu entry.
  - `src/viewport/TransformPreview.cpp` — DYnamic-mode live preview, `CadOffsetAppendLivePreview`'s
    shape (single entity, non-mutating, cursor-driven).
- Test approach:
  - happy path: DElta extends and shortens a Line; Total sets an Arc's length directly; Percent
    scales an open Polyline's terminal segment; each verified by `EXPECT` count/log plus the
    forward-vs-forward undo/redo `SAMEFILE` shape `mirror-basic.txt` established (LENGTHEN edits
    in place and allocates no new entity id per apply, so a plain pre-vs-post-undo `SAMEFILE`
    should actually hold here, unlike MIRROR's counter-timeline complication — confirm this by
    running it, don't assume).
  - failure mode: a Circle/closed-Polyline/full-circle-Arc pick is refused with a logged reason,
    geometry unchanged; a DElta that would collapse a Line to ~0 length is rejected, geometry
    unchanged.
  - GUI-only (manual, no UI-automation driver exists): ribbon button, DYnamic live preview,
    mode-switch mid-loop, paper-space and floating-model-space click routing.
- Steps:
  - [x] `ArcLengthOf`/`PolylineOpenLengthOf` + `NearerToFirstPoint` (shared nearest-end tie-break,
        computed inline per type — matches this codebase's own no-shared-endpoint-helper idiom)
  - [x] `Kind::Lengthen` + phase/mode state + `StartLengthenCommand`/`HandleLengthenText`
        (DE/P/T/DY mode dispatch, one phase chain per mode, modeled on `HandleScaleText`)
  - [x] Per-type apply functions (`ApplyLengthenToLine`/`ToArc`/`ToPolylineEnd`), holding the fixed
        end, rejecting collapse — one undo snapshot pushed inside each, right before the mutation
        (not by the caller — fixed a real ordering bug found during self-testing, see §8)
  - [x] Model-space + floating-model-space viewport-click dispatch
        (`HandleLengthenViewportPick`, wired into `SubmitViewportPickImpl` — the RECT-history site)
  - [x] Paper-space routing (`ApplyLengthenToPaperEntity`, `paperLengthenPhase`) — new, no existing
        paper command to extend (unlike MIRROR); deliberately simplified, same reasoning MIRROR's
        paper path already documents: no mode/value prompt in paper space, reuses whatever the
        model-space command last configured, refuses Dynamic mode and an unset value outright
  - [x] Dynamic-input trio (`CommandInputHint` only — `CommandExpectsPointEntry` correctly needs no
        `Lengthen` case, since picking an object is not a coordinate prompt, matching OFFSET's own
        precedent) + new ribbon button (icon, name string, Modify-ribbon placement — LENGTHEN had
        no existing disabled stub, unlike MIRROR) + autocomplete icon map + context menu +
        repeat-last + cancel-log
  - [x] DYnamic-mode live preview (`TransformPreview.cpp`, Line/Polyline/Arc all covered)
  - [x] Headless transcripts (`lengthen-delta.txt`, `lengthen-arc-and-refusals.txt`)
  - [x] Build (all three targets, zero errors, no new warnings) + full headless corpus (35/35, the
        2 new plus all pre-existing) + full `GoSurveyTests` regression (520 cases / 7,714,609
        assertions, all green)
  - [ ] Manual GUI pass — pending user (ribbon click, DYnamic live-drag preview, mode-switch
        mid-loop, paper-space and floating-model-space click routing; no UI-automation driver
        exists to do this from here)

## 7. Workflow-specific notes
- Feature: pre-flight design settled during planning (§4). No tests-first — this codebase's modify
  commands are verified by headless transcript + manual GUI pass, not Catch2 unit tests
  (`AppCommandState` cannot link into `GoSurveyTests`).

## 8. Implementation log
- 2026-08-24: REQ-103 LENGTHEN acceptance written (D-2026-08-24-a). Task opened, plan written.
  One Explore-agent research pass preceded this plan (TRIM/SCALE/OFFSET precedent stitching, arc
  grip-drag caveat, confirmed absence of any arc-length/polyline-cumulative-length helper).
- 2026-08-24: Implemented. The Arc apply math (holding `r` fixed, changing only `startRad`/
  `sweepRad` by `Δθ = copysign((newLength-currentLength)/r, sweepRad)`) was verified by hand against
  three cases before being trusted in code: extending from the start of a CCW arc (end must stay
  fixed — confirmed algebraically), extending from the end of a CW (negative-sweep) arc (confirmed
  the sign convention holds), and the headless transcript's own 3-point semicircle (center (10,0),
  r=10, sweepRad=-π — hand-derived from the picked points, then matched by the running code's log
  output before the transcript was trusted).
- Found and fixed during self-verification, before submission:
  - **Bug 1 (real, in my own new code) — undo pushed AFTER the mutation, not before.** First draft
    had each viewport-pick/text-entry call site call `ApplyLengthenTo*` and only push
    `PushUndoSnapshot` afterward if it returned true — meaning the "undo" snapshot captured the
    POST-edit state, so undoing would have restored to the same (already-edited) state, a no-op
    undo. Caught by code review before any test ran (comparing against OFFSET's `CommitOffsetLine`,
    which pushes undo immediately before the mutation, after validation). Fixed by moving
    `PushUndoSnapshot` inside each `ApplyLengthenTo*` function, positioned after every rejection
    check but before the actual array writes — matching `CommitOffsetLine`'s exact shape.
  - **Bug 2 — a linkage error from an anonymous-namespace/global-scope mismatch, not a logic bug.**
    `SubmitViewportPickImpl` sits inside a single large anonymous namespace spanning
    `CadCommands.cpp:4140-8993`; `HandleLengthenViewportPick` was defined further down, OUTSIDE
    that namespace (beside `StartOffsetCommand`, its structural precedent). A `static` forward
    declaration placed inside the namespace (to let `SubmitViewportPickImpl` call it) referred to a
    different, never-defined entity than the real external-linkage definition below — MSVC caught
    it at link/compile time as "declared but not defined". Fixed by making the function
    non-`static` and header-declared instead (`CadCommands.hpp`), the same resolution `StartMirrorCommand`
    already needed for a different reason (cross-TU use by `CadUi.cpp`) — here it's cross-scope
    within the same TU, not cross-TU, but the fix is identical.
  - **Bug 3 — test-transcript design, not a code bug: assumed LENGTHEN stayed active across
    UNDO/REDO.** `DoUndo`/`DoRedo` (pre-existing, unrelated to this task) both explicitly set
    `st.active = Kind::None` — undo cancels any in-progress command, which is correct existing
    behavior (an active command could otherwise hold stale entity references after a geometry
    change). `lengthen-delta.txt`'s second DElta application tried to continue the same command
    after an UNDO/REDO round trip without re-issuing `LENGTHEN`, and failed to find the expected
    log line. Fixed by re-issuing `CMD LENGTHEN` after the round trip — `lengthenMode`/
    `lengthenDeltaValue`/etc. are NOT part of the undo snapshot and persisted correctly across it,
    confirming REQ-103's "mode persists across repeated picks" acceptance condition the way it was
    actually meant (persists across picks within a still-active command, not across a cancelling
    UNDO).
- Both transcripts green after the three fixes; full headless corpus green (35/35 — 2 new plus
  every pre-existing transcript, one long-standing unrelated test left `Disabled`); full
  `GoSurveyTests` suite (520 cases / 7,714,609 assertions) green — no regression in the
  TRIM/OFFSET/grip code this task's functions sit beside.

## 9. Self-verification
- [x] build-project        — PASS. Clean build, all three targets, zero errors. Only pre-existing
      warnings (C4530 exceptions-disabled, etc.) — none in new code.
- [x] architecture-review  — PASS. No Workshop architectural decision: no new entity kind,
      dependency, layer, or global state; phase/mode fields live on the existing per-command
      `AppCommandState`; the length/endpoint free functions are additive utilities beside
      TRIM/OFFSET's, written for reuse by EXTEND/FILLET per REQ-103's own stated sequencing. One
      scoped simplification recorded rather than silently taken: paper-space LENGTHEN has no
      mode/value prompt or Dynamic-mode support (§6) — a real, deliberate scope line matching
      MIRROR's paper-path precedent, not a missed integration point.
- [x] code-review          — PASS. Self-reviewed the full diff: the Arc math is hand-verified
      (§8); the undo-ordering bug and the namespace-linkage bug were both found and fixed before
      submission, not deferred; the per-type apply functions mirror OFFSET's `Commit*` shape
      (validate, then push undo, then mutate); no dead code, no half-finished branches; the paper-
      space simplification is logged with a stated reason at the call site, not silent.
- [x] dependency-audit     — n/a (no dependency touched)
- [x] performance-review   — n/a (no hot-path change; one-shot per-pick command, not a per-frame
      path)
- [x] testing              — PASS. `lengthen-delta.txt` (DElta extend + shorten on a Line, full
      forward/undo/redo round trip via plain `SAMEFILE` — valid here since LENGTHEN edits in place
      and allocates no new entity id, unlike MIRROR) and `lengthen-arc-and-refusals.txt` (Total
      mode on a hand-verified 3-point arc, plus the Circle/closed-Polyline refusal path) both green;
      full headless corpus green (35/35); full `GoSurveyTests` regression green. Not covered by
      either transcript or a unit test, left to the pending manual pass: ribbon button visuals,
      DYnamic mode's live drag preview rendering, mid-loop mode switching, paper-space and
      floating-model-space click routing (no UI-automation driver exists per REQ-203's own
      anti-requirement).

## 10. Verification result
- Submitted:  2026-08-24
- Verdict:    **PASS** — closed 2026-08-24: the user confirmed the manual GUI pass covering REQ-103 steps 1-5.
- Findings:   3 found and fixed during self-verification before submission (§8: an undo-ordering
              bug, a namespace-linkage build error, and a test-transcript design error) — none open.

## 11. Outcome
- Requirements satisfied: REQ-103 step 2/8 (Acceptance met: yes for every condition a headless
  transcript or code review can establish; pending manual confirmation of the GUI-only conditions —
  ribbon, DYnamic live preview, mode-switch mid-loop, paper-space/floating-model-space routing)
- Tests added:            `tests/headless/transcripts/lengthen-delta.txt`,
  `tests/headless/transcripts/lengthen-arc-and-refusals.txt` (both green)
- Refactors:              none
- Docs updated:           spec/requirements.md (REQ-103), spec/project.md (D-2026-08-24-a), this task
- Technical debt noted:   paper-space LENGTHEN has no mode/value prompt and no Dynamic-mode
  support (§6, same class of limitation as MIRROR's paper-space erase-source gap). Removal
  condition: a user asks for it; then the paper click flow needs either a pre-chosen mode/value set
  before `paperLengthenPhase` starts, or a small on-canvas text-entry affordance neither MIRROR nor
  LENGTHEN currently has.
- Done:                   2026-08-24
  confirmation
