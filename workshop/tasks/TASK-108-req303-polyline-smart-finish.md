# TASK-108 — REQ-303: POLYLINE click-to-close and Enter-to-end (GitHub issue #80)

- Type:    feature
- Status:  done — implemented, self-verified; manual GUI pass (hover-glyph feedback) pending
- Opened:  2026-08-25
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-303** (new, this task — Commands/Viewport), Acceptance (1)-(7).
- Constraints:  REQ-301 (no new abstraction — reuse the Endpoint snap kind and the existing
                minimum-vertex gates rather than inventing new ones); REQ-039 (paper-space parity,
                already delivered by TASK-107 and inherited here, not reimplemented).
- Acceptance:   see REQ-303 in `spec/requirements.md` — click-to-close success/refusal,
                Enter-to-end success/refusal, CLOSE/END keywords unaffected, paper-space parity,
                start-point snap gated by OSNAP-Endpoint and only while POLYLINE/3DPOLY is drawing.
- Owning subsystem: Commands (`CommitPolylineDraft`, `SubmitViewportPickImpl`,
                `ProcessCommandLineSubmit`) / Viewport (`CadSnap::FindBest`).

## 2. Feature request (issue #80, filed by chetjones003)

Summary: POLYLINE requires typing `CLOSE`/`END` to finish. Desired: clicking the polyline's own
start point closes it (same as `CLOSE`), and pressing Enter finishes it open (same as `END`) —
without removing the typed keywords. Full acceptance criteria list is in the issue; condensed into
REQ-303's Acceptance above.

## 3. Clarifying questions asked before implementation

Two genuine product decisions, asked via structured choice before any code was written:

1. **Start-point snap visual distinction** — reuse the existing Endpoint glyph (log-line-only cue)
   vs. a new dedicated snap kind/glyph. **User chose reuse.** Recommendation followed: smallest
   change, no renderer/Kind-enum work, matches the issue's own ask to reuse the existing snap
   system.
2. **Early click-on-start-point behavior** — silently add a normal vertex there (not yet a close
   target) vs. always offer it as a close-attempt that refuses with `CLOSE`'s existing message.
   **User chose always-offer-refuse-if-early.** This is the one that shaped the implementation:
   it means the click path can just call into `CommitPolylineDraft`/the existing gates rather than
   adding a new minimum-vertex rule of its own.

## 4. Design (grounded in the actual code, read before writing anything)

- `StartPolylineCommand` (:14678) sets `active=Polyline`/`polylinePhase=NeedFirstPoint`.
  `SubmitPolylineVertex` (:20510, unchanged by this task) accumulates into the space-agnostic
  `polylineDraftVerts` scratch buffer. `CommitPolylineDraft` (:20428, `static`) is the single
  function that finishes the command, already paper-aware since TASK-107, already gating minimum
  vertices (`nvert<3` refuses closed, `nvert<2` refuses open). The typed `CLOSE`/`CL`/`END`
  keywords (`ProcessCommandLineSubmit`, :21725/:21733) already gate on `polyDraftSegments==0` with
  their own message before calling it.
- **Click-to-close**: `ViewportClickRouteFor` already routes `K::Polyline` to `SnappedPointPick`
  (unchanged), meaning the click's `(wx,wy)` arriving at `SubmitViewportPickImpl`'s `K::Polyline`
  block is already the OSNAP-resolved commit point (`CadUi.cpp`'s `commitX/commitY =
  viewportSnapPickLocalX/Y = snap.x/y`, traced through `CadSnap::Hit` with no arithmetic at any
  hop). So: (a) `CadSnap::FindBest` gets one new `Consider(..., cmd.polyFirstX, cmd.polyFirstY,
  Kind::Endpoint, ...)` call, gated on `commandActive && objectSnapEndpoint &&
  active==Polyline && polylinePhase==NeedNextPoint`, right after the existing polyline-vertex
  Endpoint loop; (b) `SubmitViewportPickImpl`'s `K::Polyline` block gets an exact-equality check
  (`wx==polyFirstX && wy==polyFirstY`) before falling through to `SubmitPolylineVertex` — on a
  match it replicates the typed-`CLOSE` gate verbatim (`polyDraftSegments==0` → refuse with
  `CLOSE`'s own message; else `CommitPolylineDraft(st, true, log)`, whose own `nvert<3` check
  supplies the second refusal tier for exactly one segment). Exact float equality is deliberate,
  not fragile — it is the same design already used everywhere a snap `Hit` is trusted verbatim.
  A raw typed coordinate landing on the start point (e.g. typing `0,0` again) is **not** affected —
  this only intercepts the viewport-click path, so typed input keeps behaving exactly as before.
- **Enter-to-end**: `ProcessCommandLineSubmit`'s `line.empty()` block already has a per-`active`-Kind
  dispatch (LINE's own blank-Enter sits right there, :20762, and — unlike this feature — *restarts*
  a new chain rather than exiting). Added a `K::Polyline && polylinePhase==NeedNextPoint` case
  immediately after it, replicating typed-`END`'s exact gate (`polyDraftSegments==0` → refuse with
  `END`'s own message; else `CommitPolylineDraft(st, false, log)`, which exits the command).
- **3DPOLY** shares this entire state machine (`polylineDraft3d` only changes vertex labeling/Z
  handling) so both interactions apply to it automatically — checked with a headless case, not
  assumed.
- **Paper-space parity** falls out of TASK-107 for free: both new call sites resolve through the
  same `CommitPolylineDraft`, which already branches on `ActivePaperGeometryTarget`.

## 5. Architectural boundary check (workflow.md §4)

- New abstraction / layer / dependency / ownership change / global / public API / data-format
  change?
    - [x] **No — proceed.** No new `CadSnap::Kind` (user's own choice in §3.1). No new
      minimum-vertex rule (user's own choice in §3.2 lets the click/Enter paths reuse
      `CommitPolylineDraft`'s and the typed keywords' existing gates verbatim). One new `Consider()`
      call in `FindBest`, one new intercept block in `SubmitViewportPickImpl`, one new case in
      `ProcessCommandLineSubmit`'s existing per-Kind blank-Enter dispatch — all inside functions
      that already own this exact responsibility.

## 6. Plan

- `src/commands/CadCommands.cpp`:
  - forward-declare `CommitPolylineDraft` (was previously only ever called after its own
    definition) so `SubmitViewportPickImpl`, earlier in the file, can call it.
  - `SubmitViewportPickImpl`'s `K::Polyline` block: intercept a click matching `polyFirstX/Y`.
  - `ProcessCommandLineSubmit`'s blank-Enter dispatch: add the `K::Polyline` case.
- `src/viewport/CadSnap.cpp`: `FindBest` — one new `Consider()` call for the draft's start point.
- Tests: `tests/headless/transcripts/regression-80-polyline-smart-finish.txt` — click-close
  (too-early ×2 tiers, then success), Enter-end (too-early, then success), CLOSE/END keyword
  regression, 3DPOLY parity, paper-space parity (reusing TASK-107's `LAYOUT`/`PAPERPOLYLINES` driver
  additions). Proven to fail against the pre-task code (stashed the two `.cpp` changes, rebuilt,
  ran — failed at the first click-close assertion) and pass after (restored, rebuilt, ran — 68
  steps green).

## 7. Steps
- [x] clarifying questions asked and answered (§3)
- [x] design grounded in the actual click-routing/snap/command-dispatch code (§4)
- [x] regression transcript written, proven to fail against the pre-task code
- [x] implementation
- [x] full suite + full app build

## 8. Implementation log

- 2026-08-25 — read `ViewportClickRouteFor`, `SubmitViewportPickImpl`, `CadSnap::FindBest`/
  `Consider`, and `CadUi.cpp`'s `commitX/commitY` derivation end to end before writing any code, to
  confirm the exact-equality intercept design is sound (traced the float from `FindBest`'s
  `Consider()` call through `Hit` to `commitX/commitY` with no arithmetic at any hop).
- 2026-08-25 — wrote `regression-80-polyline-smart-finish.txt` against the not-yet-existing feature.
  Built `gosurvey_headless` with only TASK-107's changes present (stashed this task's two `.cpp`
  diffs) and ran it: **fails** at step 4, "no log line contains: POLYLINE CLOSE — need at least one
  segment after the start point" (the click added a vertex instead of attempting a close).
- 2026-08-25 — implemented: forward declaration, `SubmitViewportPickImpl`'s intercept,
  `ProcessCommandLineSubmit`'s blank-Enter case, `CadSnap::FindBest`'s new `Consider()` call.
  Restored the stash, rebuilt; transcript now **passes** (68 steps, 39 log lines).
- 2026-08-25 — full regression: reconfigured CMake (new transcript needed a fresh glob), 541/541
  unit tests, 53 headless tests registered / 52 run+pass (1 pre-existing disabled), `GoSurvey.exe`
  (full app) rebuilt clean — no new warnings from either changed file.

## 9. Self-verification

- [x] build-project       — PASS. `gosurvey_headless`, `GoSurveyTests`, `GoSurvey` all clean; no new
      warnings from `CadCommands.cpp` or `CadSnap.cpp`.
- [x] architecture-review — PASS. No new abstraction/kind/rule — see §5. Both user decisions in §3
      were specifically the ones that kept this from needing one.
- [x] code-review         — PASS. Click and Enter paths each reuse an existing gate verbatim rather
      than re-deriving the minimum-vertex logic a second and third time.
- [x] dependency-audit    — PASS. None added.
- [x] performance-review  — n/a. One extra `Consider()` call per frame only while POLYLINE/3DPOLY is
      actively drawing (the same cost class as every other Endpoint candidate already gathered);
      one extra branch per click/blank-Enter, both O(1).
- [x] testing             — PASS. New transcript proven red-before/green-after; full suite green.
      **Not covered by this session**: the start-point Endpoint glyph's on-screen hover appearance —
      this project's GUI hover/visual feedback is not automatable (established precedent, see
      REQ-039's own manual-pass acceptance method) and needs the user's own manual pass.

## 10. Verification result

- Submitted: 2026-08-25
- Verdict:   **PASS** (automated scope). Manual GUI pass outstanding, not blocking — same precedent
             REQ-302's own increments used (self-verify, ship, then a manual pass confirms or finds
             GUI-only issues).
- Findings:  none outstanding in the automated scope.

## 11. Outcome

COMPLETION REPORT — TASK-108 — 2026-08-25
- Requirements satisfied:  REQ-303 (Acceptance (1)-(7) met in the automated scope; (7)'s "obeys the
                           OSNAP-Endpoint toggle" and the visual half of the start-point cue are
                           implemented but need the user's manual confirmation)
- Summary:                 POLYLINE (and 3DPOLY) now closes on a viewport click at its own start
                           point and finishes open on a blank Enter, both reusing
                           `CommitPolylineDraft` and the typed `CLOSE`/`END` keywords' existing
                           minimum-vertex gates rather than adding new ones. Paper-space parity is
                           inherited from TASK-107 with no additional code.
- Tests:                   `headless.regression-80-polyline-smart-finish` (new), proven to fail
                           against the pre-task binary. Full suite: 541/541 unit, 52/52 headless
                           (53 registered, 1 pre-existing disabled).
- Verification verdict:    PASS (automated scope; findings resolved: none)
- Assumptions:             none — both open product decisions were resolved by asking the user
                           before writing code (§3), not assumed.
- Architectural decisions: none made by Workshop.
- Dependencies:            none added
- Technical debt noted:    none.
- Build:                   reproducible, clean on target platform (MSVC, ninja, vcvars64)
- Docs updated:            `spec/requirements.md` (REQ-303 + status table), `spec/project.md`
                           (D-2026-08-25-j), this log
- Done:                    2026-08-25 (automated scope); awaiting user's manual GUI pass
