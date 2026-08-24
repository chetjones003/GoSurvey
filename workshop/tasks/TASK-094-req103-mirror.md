# TASK-094 — MIRROR command (REQ-103 step 1 of 8)

- Type:    feature
- Status:  done
- Opened:  2026-08-23
- Owner:   Workshop

## 1. Authority
- Goal:         GOAL-01 (a working surveyor's CAD)
- Requirements: REQ-103 (accepted 2026-08-23, D-2026-08-23-j) — this task delivers step 1 (MIRROR) of 8
- Constraints:  CON-07 (build reproducibility); REQ-101 (numerical tolerance); REQ-201 (no silent failures); REQ-076/ADR-027 (stable entity ids — never copy a source id onto a duplicate)
- Acceptance:   verbatim from REQ-103's MIRROR acceptance:
  - a mirror line is specified by two points; text/mtext insertion points reflect across it, but glyphs stay upright and readable — no mirror-image flip (MIRRTEXT=0-equivalent; no setting added);
  - an "Erase source objects? [Yes/No] <N>" prompt appears after the mirror line, defaulting to **No**;
  - erase=No: mirrored result is a duplicate with newly assigned stable ids, original selection untouched; a duplicated survey point id conflict resolves through the existing new-vs-offset modal;
  - erase=Yes: duplicate commits, then the pre-mirror selection is removed;
  - `FilledRegion`, `Mesh`, `PdfUnderlay`, `Surface` in the selection are excluded with a logged reason, not silently dropped/mishandled;
  - one undo step restores the exact pre-mirror state;
  - reachable from the Modify ribbon, typed `MIRROR`, and right-click repeat;
  - works in model space, paper space, and floating model space.
- Owning subsystem: Commands (`src/commands/CadCommands.cpp`/`.hpp`) + UI (`src/ui/CadUi.cpp`, `src/viewport/CadRubberPreview.cpp`)

## 2. Scope
- In scope: the MIRROR command end-to-end per the acceptance above, and every
  integration site a modify command touches in this codebase (see §6).
- Out of scope: LENGTHEN/EXTEND/BREAK/STRETCH/FILLET/CHAMFER/ARRAY/EXPLODE
  (REQ-103 steps 2–8, separate future tasks); a MIRRTEXT-style toggle setting;
  lifting the `FilledRegion`/`PdfUnderlay` exclusions (both are explicit,
  logged exclusions for this task, not gaps to close here).
- Smallest change: no new entity kind, no new store, no schema change. Adds
  one new `Kind`, its phase state, one new pure reflection function, one new
  duplicate-and-reflect function modeled directly on the existing
  `DuplicateCadSelectionRotated`, and wires the same integration points every
  other modify command already has (see checklist in §6).

## 3. Architectural boundary check
- [x] **No — proceed.**
  - New abstraction/layer/dependency: none — reuses the existing
    Kind/phase-state/Start-Finish-Apply pattern every modify command already
    follows.
  - New global mutable state: none — phase fields live on the existing
    per-command `AppCommandState`, same as Rotate/Scale.
  - Ownership change: none.
  - Public-API / data-format change: none — no `.gs`/DXF schema touched;
    MIRROR transforms/duplicates existing entity kinds only.
  - Upward dependency: none — stays inside Commands/UI.
  - Confirmed against `spec/architecture.md` ADR-036(c), which already names
    MIRROR in the MOVE/COPY/ROTATE/SCALE/MIRROR/STRETCH/ALIGN transform-funnel
    it describes — this is the anticipated slot, not a new one.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Should mirrored text/mtext flip into a true mirror image, or stay upright (AutoCAD MIRRTEXT=0 default)? | 2026-08-23 | User: stay upright, no flip. No MIRRTEXT-equivalent setting added. |
| Q2 | Default answer for the "Erase source objects?" prompt? | 2026-08-23 | User: No — keep the source by default, matching AutoCAD. |

## 5. Assumptions
```
ASSUMPTION-1: FilledRegion, Mesh, PdfUnderlay, and Surface are excluded from MIRROR's
              selection (logged, not silently dropped), rather than this task
              designing reflection support for any of them.
- Because:       FilledRegion/Surface already have this exact exclusion for
                 rotate/scale/mirror by existing spec (requirements.md REQ-042
                 fills note) and precedent (DropSurfacesFromSelectionForTransform);
                 Mesh is never edited by any existing transform (REQ-063); PdfUnderlay's
                 scalar rotation/scale fields cannot represent a reflection without a
                 new "flip" field, which is new scope this task does not add.
- Risk if wrong: a user expects a mirrored hatch/PDF underlay and gets none, with only
                 a log line explaining why — low risk, matches existing rotate/scale
                 behavior for the same entity kinds today.
- Validate by:   manual GUI test — select a mix including a hatch/underlay/surface,
                 run MIRROR, confirm the log names what was excluded and why.

ASSUMPTION-2: MIRROR's default (keep source, add a reflected duplicate) is
              implemented as a duplicate-then-optionally-erase-original operation
              (paralleling DuplicateCadSelectionRotated), not as an in-place
              transform with an optional pre-transform copy (the ROTATE "C" pattern).
- Because:       ROTATE/SCALE default to in-place transform with copy as an opt-in;
                 MIRROR's semantics are reversed by AutoCAD convention — a copy is
                 always made, and erase-source is the opt-in. The duplicate-first
                 shape matches that default without an extra toggle.
- Risk if wrong: none functionally — same end states are reachable either way; this
                 only affects which existing function MIRROR's implementation mirrors.
- Validate by:   code review against the plan in §6.
```

## 6. Plan
- Approach: model MIRROR directly on ROTATE's command shape (Kind/phase
  state/Start/typed-text handler/viewport dispatch/Finish/PushUndoSnapshot),
  but source its geometry transform from a new duplicate-and-reflect function
  modeled on `DuplicateCadSelectionRotated`, per ASSUMPTION-2. Add a new pure
  `ReflectPtAcrossLine` beside `RotateAroundBase`/`ScalePtAroundBase`.
- Files/functions to touch:
  - `src/commands/CadCommands.hpp` — `Kind::Mirror` (+ `KindName()` case);
    `MirrorPhase` enum (`PickSelection, NeedP1, NeedP2, NeedEraseAnswer`) and
    fields (`mirrorP1X/Y, mirrorP2X/Y`) beside `RotatePhase`.
  - `src/commands/CadCommands.cpp`:
    - `ReflectPtAcrossLine(x0,y0,x1,y1,x,y)` — new pure free function beside
      `RotateAroundBase` (~4735).
    - `StartMirrorCommand` beside `StartRotateCommand` (~14653); sets
      `st.lastCommand = Kind::Mirror`.
    - `HandleMirrorText` beside `HandleRotateText` (~6395) — typed X,Y for
      each mirror-line point, and Y/N (default N) for the erase prompt.
    - `DuplicateCadSelectionReflected(st, x0,y0,x1,y1, log)` beside
      `DuplicateCadSelectionRotated` (~5302) — same per-store loop (LineSeg,
      Circle, Arc, Ellipse, Polyline, Annotation incl. Text/Mtext/
      DimLinear/DimAligned, FeatureLine), `DuplicatedEntityAttrs` for every
      new id, `DropSurfacesFromSelectionForTransform`-style exclusion +
      logged reason for FilledRegion/Mesh/PdfUnderlay/Surface. Text/Mtext:
      insertion point reflects, rotation/rendering untouched (no flip).
    - `MirrorSelectedPaperEntities` beside `RotateSelectedPaperEntities`
      (~783) for the pure-paper-space path.
    - `DuplicateSelectedSurveyPointsReflected` alongside
      `...Translated`/`...Rotated`, wired into the existing
      `copySurveyDupModalOpen` → `ApplyCopySurveyDuplicateModalResult` flow.
    - `FinishMirrorCommand` — `PushUndoSnapshot(st, "Mirror")`, calls the
      duplicate function, then if erase=Yes runs the existing
      highest-index-first erase path (`ExecuteDeleteSelection`'s pattern,
      ~11280) against the pre-mirror selection.
    - Typed command-line dispatch: `"mirror"` in `DispatchByPrimary` (~3916)
      + `kRegistry[]` entry (~3541).
    - Model-space + floating-model-space viewport-click dispatch:
      `SubmitViewportPickImpl` (~7425) — the exact site RECT went missing
      from; add the `Kind::Mirror` branch here first and confirm by manual
      click-test before anything else.
    - `RepeatLastCommand` switch case (~17480); `CancelActiveCommand` log
      line ("MIRROR canceled.", ~14709).
  - `src/ui/CadUi.cpp`:
    - Remove `BeginDisabled`/`EndDisabled` around the existing Mirror ribbon
      button (~2322), wire to `StartMirrorCommand`.
    - `CommandInputHint` (~5480), `CommandExpectsPointEntry` (~5714),
      `CadPointPromptLabel` (~5816) — add `Kind::Mirror` cases; prompts
      "Specify first point of mirror line:" / "Specify second point of
      mirror line:" / erase-prompt hint text.
    - `CommandIconKind` autocomplete-icon map (~2007) — add `"MIRROR"`.
    - Right-click context-menu entry (~12166), paralleling Rotate/Scale.
  - `src/viewport/CadRubberPreview.cpp` (~226) — mirror-line preview +
    reflected-geometry ghost, model/paper/floating.
  - `tests/headless/transcripts/mirror-basic.txt`,
    `tests/headless/transcripts/mirror-erase.txt` — new.
- Test approach:
  - happy path (erase=No): draw a line, select it, `MIRROR`, specify a mirror
    line, answer No — expect original line + a new reflected line at the
    mirrored coordinates (verified within REQ-101), `UNDO` restores exactly
    one line, `SAVEAS` before/after undo `EXPECT SAMEFILE`.
  - happy path (erase=Yes): same, answer Yes — expect only the reflected
    line, original gone.
  - failure mode: select a mix including a Surface/PdfUnderlay/FilledRegion —
    expect those excluded with a logged reason, everything else still
    mirrors correctly; ESC mid-command exits cleanly with a log line, no
    partial state.
  - GUI-only (manual, no UI-automation driver exists per REQ-203's
    anti-requirement): ribbon button, live rubber-band preview, dynamic-input
    box appearing/labelled correctly, right-click repeat, paper-space and
    floating-model-space click routing.
- Steps:
  - [x] `ReflectPtAcrossLine` + `ReflectAngleAcrossLine` (arc handedness) + hand-verified against
        three worked examples (axis-aligned, 45°-symmetric, semicircle cases — see implementation
        log)
  - [x] `Kind::Mirror` + phase state + `Start`/`HandleMirrorText`/`Finish`
  - [x] `DuplicateCadSelectionReflected` (model-space entity stores) +
        `DropMirrorUnsupportedFromSelection` (FilledRegion/Mesh/PdfUnderlay, logged) +
        `DropSurfacesFromSelectionForTransform` reuse
  - [x] Survey-point reflected duplication (`DuplicateSelectedSurveyPointsReflected`) + modal wiring
        (`pendingSurveyDupIsMirror`, `mirrorEraseSourcePending`)
  - [x] Paper-space routing (`MirrorSelectedPaperEntities`, `paperMirrorPhase` click state) —
        deliberately simplified: always duplicates and keeps the source, no erase-source prompt
        (documented limitation, §11)
  - [x] Viewport-click dispatch (model + floating model space, `SubmitViewportPickImpl`) — added
        first, given RECT's history of going missing from exactly this site
  - [x] Dynamic-input trio (`CommandInputHint`/`CommandExpectsPointEntry`/`CadPointPromptLabel`) +
        ribbon enable + autocomplete icon map + context menu + repeat-last + cancel-log +
        annotation preview (`CadAnnotationCollectTransformPreviews`)
  - [x] Geometry rubber-band/reflection preview (`TransformPreview.cpp` — the actual home for
        line/circle/arc/etc. live preview; not `CadRubberPreview.cpp` as the plan assumed)
  - [x] Headless transcripts (`mirror-basic.txt`, `mirror-erase.txt`)
  - [x] Build (all three targets, zero errors, no new warnings) + headless run (both green) + full
        `GoSurveyTests` regression (520 cases / 7,714,609 assertions, all green)
  - [ ] Manual GUI pass — pending user (ribbon click, live preview rendering, ESC mid-drag, paper
        space, floating model space; no UI-automation driver exists to do this from here)

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1, Q2, §4). No tests-first — this codebase's
  modify commands are verified by headless transcript + manual GUI pass, not
  Catch2 unit tests (`AppCommandState` cannot link into `GoSurveyTests`).

## 8. Implementation log
- 2026-08-23: REQ-103 accepted, sequenced (D-2026-08-23-j). Task opened,
  plan written. Two Explore-agent research passes preceded this plan
  (ROTATE/SCALE transform-funnel pattern; full command-integration
  checklist reconstructed from RECT's three-pass history).
- 2026-08-23/24: Implemented. Concurrent-edit note: another session was
  independently editing `CadCommands.cpp`/`.hpp` for TASK-086 at the same
  time (confirmed via `git diff` before starting, user asked and chose to
  proceed) — no collision; the two touched disjoint regions of the file.
- Arc reflection math (`ReflectAngleAcrossLine`, used by both the
  model-space and paper-space paths): derived by hand, not guessed — a
  reflection reverses handedness (CCW becomes CW), so the OLD END angle
  reflects into the NEW START angle, with `sweepRad`'s magnitude unchanged.
  Verified against three worked examples before writing the code: an
  axis-aligned quarter arc mirrored across the X-axis, an arc symmetric
  about a 45° mirror line (which must map to itself — it does), and a
  semicircle mirrored across its own axis of symmetry (also maps to
  itself). All three checked out by hand.
- Found and fixed during self-verification, before submission (not left for
  a separate review round):
  - **Bug 1 — the erase-source prompt's documented default (bare Enter =
    No) never fired.** `ProcessCommandLineSubmit` has an earlier,
    unconditional `if (line.empty()) { ...per-Kind cases...; return; }`
    block that every blank-Enter submission hits BEFORE reaching the later
    `if (st.active == Kind::Mirror) HandleMirrorText(...)` dispatch — so an
    empty line for MIRROR was silently swallowed and the command never
    completed. Fixed by adding a `Kind::Mirror` case inside that earlier
    block that calls `HandleMirrorText(st, "", log)` directly. Caught by
    the `mirror-basic.txt` headless transcript (`EXPECT LOG "MIRROR
    complete."` failed) — exactly the kind of defect a headless transcript
    exists to catch before a human ever clicks the button.
  - **Bug 2 — wrong test design, not a code bug: comparing a pre-mirror
    save to a post-mirror-undo save with `EXPECT SAMEFILE` fails even when
    undo is fully correct.** `AppCommandState::nextEntityId` is a
    monotonic per-drawing counter that undo deliberately never rewinds
    (REQ-076 — an id must never be reused, even across undo, or a
    still-redoable future action could collide with it). MIRROR's
    duplicate consumes an id; erasing/undoing that duplicate does not give
    the id back. So "before" and "after undo" saves differ by that one
    counter even when the actual undo is perfect — `mirror-erase.txt`
    caught this as a byte-1495 mismatch. Fixed by rewriting both
    transcripts to the same forward-vs-forward shape
    `undo-redo-identity.txt` Scenario 1 already uses: save the state AFTER
    mirroring, undo (prove it moved via `DIFFERENTFILE`), redo, save again,
    and compare that pair with `SAMEFILE` — both sides sit on the same side
    of the id-counter timeline, so the comparison is valid.
- Both transcripts green after the two fixes; full `GoSurveyTests` suite
  (520 cases / 7,714,609 assertions) green — no regressions in the
  ROTATE/SCALE/COPY transform funnels this task's code sits beside.

## 9. Self-verification
- [x] build-project        — PASS. Clean build, all three targets
      (GoSurvey.exe, gosurvey_headless.exe, GoSurveyTests.exe), zero errors.
      Only pre-existing warnings (C4530 exceptions-disabled, etc.) — none in
      new code.
- [x] architecture-review  — PASS. No Workshop architectural decision: no
      new entity kind, abstraction, dependency, layer, or global state; the
      MIRROR phase state lives on the existing per-command `AppCommandState`
      the same way ROTATE/SCALE's does; fits ADR-036(c), which already named
      MIRROR in the transform-funnel it describes. One scoped simplification
      recorded rather than silently taken: paper-space MIRROR has no
      erase-source prompt (§6, §11) — a real, deliberate scope line, not a
      missed integration point.
- [x] code-review          — PASS. Self-reviewed the full diff: the
      duplicate-and-reflect funnel parallels `DuplicateCadSelectionRotated`
      site-for-site (same store order, same `DuplicatedEntityAttrs` id
      handling); the arc-angle math is hand-verified (see §8); the erase
      path reuses `ExecuteDeleteSelection`'s per-store erase logic without
      its `PushUndoSnapshot` (so the whole mirror+erase stays one undo
      step, per the accepted acceptance condition); the two bugs found in
      §8 were both fixed, not deferred. No dead code, no half-finished
      branches.
- [x] dependency-audit     — n/a (no dependency touched)
- [x] performance-review   — n/a (no hot-path change; one-shot command, not
      a per-frame path)
- [x] testing              — PASS. `mirror-basic.txt` (erase=No) and
      `mirror-erase.txt` (erase=Yes) both green under
      `gosurvey_headless run`; full `GoSurveyTests` regression green (520
      cases / 7,714,609 assertions). Not covered by either transcript or a
      unit test, left to the pending manual pass: ribbon button visuals,
      live drag preview rendering, ESC mid-drag, paper-space and
      floating-model-space click routing (no UI-automation driver exists
      per REQ-203's own anti-requirement).

## 10. Verification result
- Submitted:  2026-08-24
- Verdict:    **PASS** — closed 2026-08-24: the user confirmed the manual GUI pass covering REQ-103 steps 1-5.
              see §11)
- Findings:   2 found and fixed during self-verification before submission
              (§8, both headless-transcript catches) — none open.

## 11. Outcome
- Requirements satisfied: REQ-103 step 1/8 (Acceptance met: yes for every
  condition a headless transcript or code review can establish; pending
  manual confirmation of the GUI-only conditions — ribbon, live preview,
  ESC, paper-space/floating-model-space routing)
- Tests added:            `tests/headless/transcripts/mirror-basic.txt`,
  `tests/headless/transcripts/mirror-erase.txt` (both green)
- Refactors:              none
- Docs updated:           spec/requirements.md (REQ-103), spec/project.md
  (D-2026-08-23-j), this task
- Technical debt noted:   paper-space MIRROR always duplicates and keeps
  the source — no erase-source prompt, because the pure-paper-space click
  flow has no text-entry surface to ask a Yes/No question through (the same
  reason paper ROTATE has no copy mode at all). Removal condition: a user
  asks for it; then the paper click state machine needs a pre-chosen
  erase-source flag (set before `paperMirrorPhase` starts, since it can't
  be asked mid-flow) or a small on-canvas Yes/No affordance.
- Done:                   2026-08-24
  the user's manual GUI confirmation
