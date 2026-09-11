# TASK-157 — Per-viewport active UCS and UCSFOLLOW isolation (issue #155)

- Type:    feature
- Status:  self-verify (implemented + self-verified PASS 2026-08-31; awaiting review-team sign-off)
- Opened:  2026-08-31
- Owner:   chetjones003 (with Claude)

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         GOAL — 3D model space / coordinate-system service (ADR-025, REQ-154 family).
- Requirements: **REQ-155 (accepted 2026-08-31, D-2026-08-31-c)** — Per-viewport active UCS and
                UCSFOLLOW. Split from REQ-154 / GitHub issue #126 once REQ-061 (issue #175) removed
                the blocker. Restate its Acceptance conditions in §6 before coding.
- Constraints:  CON-07 (additive `.gs`); REQ-101 (coordinate tolerance); ADR-025 (absolute Z,
                paper-space stores stay 2D); ADR-009/013 (paper-space sheet stores stay 2D).
- Acceptance (from GitHub issue #155, none yet promoted to spec):
  - "Per-viewport camera exists (REQ-061) or multi-viewport model space is defined." — **met**
    by REQ-061 / issue #175 / TASK-156 (PR #176). This is the only satisfied item.
  - "Each viewport carries its own active UCS + UCSFOLLOW."
  - "Changing the UCS while one viewport is active does not alter another viewport's frame."
  - "`UCSFOLLOW=1` re-plans only the active viewport."
  - "Named UCS definitions remain shared per drawing; only the *active* selection is per viewport."
  - "Regression test: two viewports, different UCSs, geometry typed in each lands at the
    frame-implied world coordinates."
- Owning subsystem: Commands (the UCS frame + coordinate entry), Renderer (grid/plan re-plan),
  IO (`.gs`). Same owners as REQ-154.

## 2. Scope
- In scope (once unblocked):
  - Move the *active* UCS selection (`activeUcs`, `ucsPrevious`, `ucsFollow`) from per-document-tab
    state (`SnapshotDocument` / `RestoreDocumentFromSnapshot` in `src/commands/CadCommands.cpp`)
    to per-viewport state.
  - An "active viewport" concept for model space so coordinate entry, ORTHO, the grid, and the
    readout resolve against the active viewport's UCS.
  - `UCSFOLLOW=1` re-plans only the active viewport's camera.
  - Additive `.gs` persistence of the per-viewport active-UCS selection; legacy files load with
    the drawing's single UCS applied to every viewport.
- Out of scope:
  - Named UCS / named view *definitions* — they stay per drawing (issue #155 says so explicitly).
  - Paper-space viewport camera orientation — done in TASK-156.
  - Polar-tracking-follows-UCS (separate: PR #174 / issue #154).
- Smallest change: unknown until the scope decision below is made — the shape of "active viewport"
  depends on whether model space gets multiple simultaneous viewports at all.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **Was Yes → escalated as SPEC GAP → RESOLVED.** ADR-025 (e·2) (D-2026-08-31-c) now
          settles the ownership question and the `.gs` format addition: the per-viewport frame is a
          *name* selection field on `Viewport` (already owned by `PaperLayout`), definitions stay
          single-owned on `AppCommandState`, persisted additively. No open architectural decision
          remains. Proceed with planning.
- Why it was architectural (now decided by ADR-025 (e·2)):
  1. **Ownership change.** The active UCS moves from one owner (the document tab) to a new owner
     (a viewport / an "active viewport"). Which resource owns the active frame is an
     `architecture.md` ownership question, not a Workshop call.
  2. **Open scope question.** "Multiple simultaneous model-space viewports" is explicitly open
     (`spec/requirements.md`, REQ-084 note; REQ-061 status). Today there is exactly one model view
     per drawing, so "per-viewport active UCS" in *model* space has no meaning until that scope is
     decided. The issue's AC-4 / AC-6 (per-viewport UCSFOLLOW re-plan; geometry typed in each of
     two viewports) cannot be built or tested without it.
  3. **New state + data-format change.** A per-viewport active-UCS selection is new persisted
     state in `.gs`.
- Escalation record:
  - **SPEC GAP proposal filed:** `workshop/tasks/TASK-157-spec-gap-proposal.md` — proposes new
    **REQ-155**, an ADR-025 (e·2) amendment, and decision-log entry **D-2026-08-31-c**. Recommended
    scope: per-viewport active UCS on paper-space viewports + floating model space (REQ-036);
    `VPORTS`-style split model space explicitly deferred; the per-viewport frame is stored as a
    *name* into the per-drawing `ucsNamed` (definitions stay single-owned).
  - **Decision needed (to be recorded in `spec/project.md` §9):** see the proposal's §7 —
    Q1 scope, Q2 name-vs-value, Q3 UCSFOLLOW per-viewport-vs-per-drawing, Q4 model view isolation.
  - Status stays **blocked** until D-2026-08-31-c is recorded and REQ-155 is `accepted`.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Model-space multi-viewport scope — full `VPORTS` split, or floating-mspace-only? | 2026-08-31 | **floating-mspace-only** (D-2026-08-31-c); `VPORTS` split deferred. |
| Q2 | Owner of the active UCS selection after it leaves the document tab | 2026-08-31 | **`Viewport`** (owned by `PaperLayout`); definitions stay on `AppCommandState` (D-2026-08-31-c). |
| Q3 | How is an ad-hoc, unnamed UCS built while floating persisted to the viewport? | 2026-08-31 | **ANSWERED (user, 2026-08-31): store the resolved `ucs::Ucs` VALUE on the `Viewport`** (`Viewport::activeUcs`, default World). A name cannot represent an ad-hoc frame; AutoCAD `UCSVP` stores the full frame. Ownership unchanged: named definitions still live only in `ucsNamed`. |
| Q4 | Does the value form need a spec / ADR-025 (e·2) wording tweak? | 2026-08-31 | **DONE.** `spec/project.md` D-2026-08-31-c, `spec/requirements.md` REQ-155 + item-3, `spec/architecture.md` ADR-025 (e·2) all amended 2026-08-31 from "a name" to "a `ucs::Ucs` frame value". Not a scope change. |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: A paper-space `Viewport` stores its per-viewport frame as a resolved `ucs::Ucs` VALUE
              (`Viewport::activeUcs`, default = World == identity), not a name string.
- Because:       ADR-025 (e·2) as written says "a name into ucsNamed", but that cannot represent an
                 ad-hoc UCS a user builds while floating inside the viewport (UCS 3-point, ZAxis,
                 rotate). AutoCAD's UCSVP is a full per-viewport frame.
- Risk if wrong: if the reviewer insists on name-only, ad-hoc-UCS-while-floating is either refused
                 with a prompt ("name this UCS to keep it on the viewport") or discarded on exit —
                 a smaller feature but still meets every REQ-155 acceptance bullet as written.
- Validate by:   Q3/Q4 above. **VALIDATED 2026-08-31** — user chose the value form; spec amended.
```
```
ASSUMPTION-2: REQ-155 AC-4 ("UCSFOLLOW=1 re-plans only the active viewport's camera") is satisfied
              by writing the floating `Viewport`'s cam{Azimuth,Elevation,Roll}Deg (REQ-061) — the
              paper overlay and PDF plot already honour those.
- Because:       whether the *interactive floating view* renders through a rotated viewport camera
                 is REQ-061's own documented deferred follow-up ("draw-inside-a-viewport assumes a
                 plan camera"), not REQ-155's.
- Risk if wrong: AC-4's on-screen effect looks inert until that REQ-061 follow-up lands, though the
                 stored state (and the plot) is correct and the test passes.
- Validate by:   the reviewer confirming AC-4 is a state/plot assertion, not an interactive-view one.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)

### 6.1 Authority restated
- REQ-155 (accepted, D-2026-08-31-c). Acceptance bullets, verbatim, mapped to tests in §6.4:
  - two viewports A (rotated frame) / B (World): a point typed in each floating view lands at the
    frame-implied world coords, asserted on the **stored** coordinate → `T1`.
  - `UCSFOLLOW=1`, change A's frame while floating A → A's camera re-plans, B's camera + the
    drawing model-view camera + all stored geometry byte-identical → `T2`.
  - change A's frame while nothing floats → no camera, no coordinate changes → `T3`.
  - pre-REQ-155 `.gs` loads with both viewports on the drawing frame, renders identically;
    post-REQ-155 `.gs` round-trips the per-viewport selection byte-identically, still carries every
    key an older build needs → `T4` (extends `GsIoViewportCameraTests`).
  - the coordinate readout inside a floating viewport names that viewport's frame → `T5`.
- Owning subsystems (from `spec/architecture.md`, ADR-025 (e·2)): **Commands** (frame swap on
  float enter/exit, UCSFOLLOW re-plan), **IO** (`.gs`), **Renderer/UI** (grid + readout already
  read `st.activeUcs`, so they need *no change* under the swap approach — see 6.2).

### 6.2 Approach — swap `st.activeUcs` on floating enter/exit (the BEDIT-store-swap precedent)
Every consumer of the active frame — `CadActiveUcsStorage`, `CadActiveWorkPlane`, `SetActiveUcs`,
the `ID` readout (`CadCommands.cpp:10263`), ORTHO/POLAR (`ApplyOrthoConstrainFromAnchor` /
`ApplyPolarConstrainFromAnchor`), the grid (`main.cpp:1249`), `CadUcsViewAzimuthOffsetDeg` — reads
the single field `AppCommandState::activeUcs`. So **do not thread a new parameter through ~30
sites.** Instead:

- `EnterFloatingModelSpace` (`CadCommands.cpp:1293`): stash `cmd.activeUcs` → a new
  `cmd.drawingActiveUcsStash` (session-only, like `blockEditorSnapshot`), then load the floating
  `Viewport::activeUcs` into `cmd.activeUcs`. `BumpCadGpuCache` already called.
- `ExitFloatingModelSpace` (`CadCommands.cpp:1310`): harvest `cmd.activeUcs` back into that
  `Viewport::activeUcs`, restore `cmd.activeUcs` from the stash.
- While floating, `SetActiveUcs` / `UCS` / `UCSNAMED` restore all just work — they mutate
  `cmd.activeUcs`, which *is* the viewport's frame for the duration. `ucsNamed` is the shared
  drawing list (definitions stay shared — REQ-155). `ucsPrevious` while floating: keep it on the
  same stack (simplest; a stale entry is harmless) — note in the log, revisit only if it bites.
- `ApplyPlanViewOf` (`CadCommands_Ucs.cpp:231`): add a branch — if `InFloatingModelSpace(st)`,
  write `CurrentViewport(st)->cam{Azimuth,Elevation,Roll}Deg` directly (no `CadStartViewAnimation`;
  a paper viewport does not animate) instead of the drawing camera. This is the whole of AC-4.
- `Viewport::cameraIsPlan()` (REQ-061) stays the parity guard; a World frame re-plans to
  az0/el90/roll0 so a viewport left in World stays bit-identical to pre-REQ-155.

### 6.3 Files / functions to touch
| File | Change |
|---|---|
| `src/commands/PaperSpace.hpp` | `Viewport` gains `ucs::Ucs activeUcs{}` (default = World). `#include "util/ucs.hpp"` — **confirmed dependency-safe** (plan review): `util/ucs.hpp` pulls only `ray3d.hpp` + `<cmath>`, a pure util-tier value type below `commands/`; the inline-12-floats fallback is not needed. |
| `src/commands/CadCommands.hpp` | on `AppCommandState`, beside the `blockEditor*` fields: `ucs::Ucs drawingActiveUcsStash;` + `bool floatingUcsSwapActive = false;`. New accessor `inline const ucs::Ucs& CadDrawingScopedUcs(const AppCommandState& st)` → `st.floatingUcsSwapActive ? st.drawingActiveUcsStash : st.activeUcs` (the drawing-scoped frame, correct even while a viewport frame is live in `activeUcs`). |
| `src/commands/CadCommands.cpp` | `EnterFloatingModelSpace` — stash `activeUcs` → `drawingActiveUcsStash`, set `floatingUcsSwapActive`, load `L.viewports[vpIdx].activeUcs` → `activeUcs`, `SetActiveUcs`-style cache bump. `ExitFloatingModelSpace` — harvest `activeUcs` → that viewport, restore from stash, clear the flag. **Plan-review finding F1:** `SnapshotDocument` (`:80`) writes `doc.activeUcs = cmd.activeUcs` — change to `CadDrawingScopedUcs(cmd)` so a tab-switch while floating snapshots the *drawing's* frame, not the viewport's. `RestoreDocumentFromSnapshot` needs no change (you cannot tab-switch into a floating state). |
| `src/commands/CadCommands_Ucs.cpp` | `ApplyPlanViewOf` — floating branch writes `CurrentViewport(st)->cam{Az,El,Roll}Deg` directly (no animation), else the existing drawing-camera path. |
| `src/io/GsIo.cpp` | **Plan-review finding F1:** `BuildRoot` (`:1321`) writes `st.activeUcs` as the drawing UCS — change to `CadDrawingScopedUcs(st)` so a save while floating records the drawing frame. For the per-viewport frame: reuse the existing `writeUcs` lambda (`:1315`) — write `vo["ucs"]` beside the `cam*` keys (`:743`) only when `!ucs::IsWorld(v.activeUcs)`; read back in `ApplyDocumentFromJson` (`:1670`) with an identity default. Additive, no `kGsFormatVersion` bump. |
| `docs/gs-file-format.txt` | document the new per-viewport `ucs*` keys (additive, optional). |
| `tests/ViewportCameraTests.cpp` or new `tests/ViewportUcsTests.cpp` | `T1`, `T2`, `T3`, `T5`. |
| `tests/GsIoViewportCameraTests.cpp` | `T4` (round-trip + legacy-load). |
| `spec/requirements.md` | mark REQ-155 verification row `done (TASK-157)` on PASS; note the value-vs-name resolution of Q3/Q4. |

### 6.4 Test approach (happy path + failure mode, headless — no GL)
- `T1` (happy): build layout, 2 viewports; `viewports[0].activeUcs` = a Z-rotated + translated
  frame, `[1]` = World. `EnterFloatingModelSpace(…,0)`; feed `"10,0"` through the same
  coordinate-entry path REQ-154's tests use; assert the **stored** local coord == frame-implied
  value within REQ-101. `ExitFloatingModelSpace`; `Enter…(…,1)`; same input lands on the World axis.
- `T2` (failure mode / isolation): `ucsFollow=true`; capture `viewports[1].cam*` and
  `st.viewportAzimuthDeg/El/Roll` as bytes; enter float 0, `SetActiveUcs(rotated)`; assert
  `viewports[0].cam*` changed, `viewports[1].cam*` and the drawing camera bytes **unchanged**, and
  every entity coordinate unchanged.
- `T3`: not floating; `SetActiveUcs`; assert no `Viewport::cam*` and no coordinate moved.
- `T4`: extend the round-trip test — set `viewports[i].activeUcs`, save, load, compare frames;
  strip the keys → every viewport loads World; a pre-REQ-155 fixture (no keys) loads World.
- `T5`: enter float 0 with a rotated frame; run the `ID` command path; assert the emitted readout
  string names the frame (not "World").
- `T6` (plan-review finding F1 — persistence isolation): set `viewports[0].activeUcs` to a rotated
  frame, drawing `activeUcs` = World; `EnterFloatingModelSpace(…,0)`; **(a)** save `.gs`, reload,
  assert the reloaded drawing `activeUcs` is World (not the viewport frame) and `viewports[0]`
  round-tripped its frame; **(b)** `SnapshotDocument` then `RestoreDocumentFromSnapshot` for that
  tab, assert `cmd.activeUcs` still resolves to the drawing frame via `CadDrawingScopedUcs`.

### 6.5 Steps
- [x] confirm Q3/Q4 (value vs name) — **value**, spec amended 2026-08-31.
- [x] plan review (workflow step 3) — see §7.1. Verdict PASS with finding F1 folded in.
- [ ] `Viewport::activeUcs` field + `.gs` read/write + `docs/gs-file-format.txt`; `T4` green.
- [ ] `CadDrawingScopedUcs` accessor + F1 fixes at `SnapshotDocument` and `BuildRoot`; `T6` green.
- [ ] `EnterFloatingModelSpace`/`ExitFloatingModelSpace` stash-swap; `T1`, `T3`, `T5` green.
- [ ] `ApplyPlanViewOf` floating branch; `T2` green.
- [ ] self-verify (§9): `./dev/test` full suite green, unchanged pass count + the new cases.
- [ ] submit for verification review (build-project → architecture-review → code-review →
      dependency-audit → performance-review → testing).

### 6.6 Explicitly NOT in this task
- `VPORTS`-style simultaneous model-space viewports (deferred, D-2026-08-31-c).
- Making the *interactive* floating view render through a rotated viewport camera — REQ-061's own
  deferred follow-up (ASSUMPTION-2).
- Per-viewport `UCSFOLLOW` *flag* — stays one per-drawing 0/1 (D-2026-08-31-c Q3); only its effect
  is scoped.
- Any UI beyond what exists: the "View" combo in the Viewports window (REQ-061) is camera-only; a
  per-viewport UCS picker in that window is a nice-to-have, not a REQ-155 acceptance item — leave
  it unless the reviewer asks (a viewport's frame is set by entering its floating mspace and
  running `UCS`, which is the AutoCAD workflow).

## 7. Workflow-specific notes
- Feature: pre-flight — REQ-155 accepted, acceptance bullets restated (§6.1), owning subsystems
  confirmed (Commands + IO; Renderer/UI unchanged by the swap approach). Tests-first: `T4` then
  `T6` then `T1`/`T3`/`T5` then `T2`, each written to fail against `master` before its step.
- Q3/Q4 (value vs name) — resolved: value form, spec amended 2026-08-31.

## 7.1 Plan review (workflow step 3 — verification of the plan, before code)
Reviewed the §6 plan against `spec/architecture.md` §11 + ADR-025 (e·2) + REQ-155.

- **Layering ✓** — `Viewport` including `util/ucs.hpp` is a downward edge (util tier, pure, only
  `ray3d` + `<cmath>`). No new upward dependency. `render/` untouched.
- **Ownership ✓** — `Viewport::activeUcs` has one owner (`Viewport`, owned by `PaperLayout`);
  `drawingActiveUcsStash` is session-only on `AppCommandState`, the same shape as
  `blockEditorSnapshot` (D-2026-08-29-h precedent). Named definitions still solely on `ucsNamed`.
- **State/data flow — finding F1 (folded into §6.3/§6.4, non-blocking with the fix).** The
  swap makes `AppCommandState::activeUcs` mean "the viewport's frame" while floating. Two sites
  *persist* that field as the drawing's frame and would mis-record the viewport frame:
  `SnapshotDocument` (tab-switch snapshot) and `GsIo::BuildRoot` (`.gs` save). Fix: a
  `CadDrawingScopedUcs(st)` accessor returning the stash while floating; both sites call it. Undo
  is unaffected — `DrawingGeometrySnapshot` is geometry-only, carries no UCS. Added test `T6`.
- **Abstraction ✓** — no new interface/template; `CadDrawingScopedUcs` is one free inline function
  with 2 present-day call sites.
- **Boundaries ✓** — no `gl*` outside Renderer; `ApplyPlanViewOf`'s floating branch writes plain
  data on `Viewport`. No previously-rejected approach reintroduced (TASK-008's reverted GL scissor
  pass is unrelated; this is the accepted store-swap pattern).
- **Scope ✓** — plan implements exactly REQ-155's six bullets; `VPORTS` split correctly excluded;
  ASSUMPTION-2 (AC-4 = stored-state + plot assertion, not interactive-view) is consistent with
  REQ-061's own deferred-follow-up text and is called out for the reviewer, not guessed past.

```
AUDIT VERDICT — TASK-157 plan — 2026-08-31
- Layering ✓  Ownership ✓  State ✓ (with F1 fix)  Abstraction ✓  Boundaries ✓
- Blocking findings: none
- Non-blocking: F1 (persistence isolation) — fix + T6 folded into the plan
- Outcome: PASS — cleared to implement §6.5 steps
```

## 8. Implementation log  (append as you work)
- 2026-08-31 — opened as a follow-up to TASK-156 / PR #176 (issue #155 review). REQ-061 prerequisite
  (issue #155 AC-1) is satisfied by TASK-156; the remaining 5 acceptance items had no accepted
  requirement and an open scope question. Opened in **blocked: SPEC GAP** per CLAUDE.md workflow §1
  and §4.
- 2026-08-31 — SPEC GAP resolved. `spec/project.md` §9 records **D-2026-08-31-c**;
  `spec/requirements.md` adds **REQ-155** (accepted) + verification row and rewrites REQ-154's
  "Not in this requirement" item 3; `spec/architecture.md` adds **ADR-025 (e·2)**. Status → **plan**.
- 2026-08-31 — plan written (§6). Approach: swap `AppCommandState::activeUcs` on floating-mspace
  enter/exit (BEDIT-store-swap precedent) so the ~30 existing frame consumers need no change;
  `Viewport` gains an `activeUcs` field; `ApplyPlanViewOf` gets a floating branch for AC-4. One
  pre-flight question open (Q3/Q4).
- 2026-08-31 — Q3/Q4 answered (user): viewport frame is a `ucs::Ucs` **value**, not a name.
  D-2026-08-31-c + REQ-155 + ADR-025 (e·2) amended to match.
- 2026-08-31 — plan review done (workflow step 3, §7.1). Verdict **PASS**, no blocking findings.
  One non-blocking finding F1 (save / tab-snapshot while floating would mis-record the viewport
  frame as the drawing frame) — fix (a `CadDrawingScopedUcs` accessor) + test `T6` folded into §6.
- 2026-08-31 — **implemented** (§6.5). Files:
  - `src/commands/PaperSpace.hpp` — `Viewport::activeUcs` (`ucs::Ucs`, default World); `#include "util/ucs.hpp"`.
  - `src/commands/CadCommands.hpp` — `AppCommandState::{drawingActiveUcsStash, floatingUcsSwapActive}`;
    `CadDrawingScopedUcs(st)` accessor.
  - `src/commands/CadCommands.cpp` — `EnterFloatingModelSpace` swaps in the viewport frame,
    `ExitFloatingModelSpace` folds it back + restores; `SaveDocumentToSnapshot` uses
    `CadDrawingScopedUcs` and patches the floating viewport into `doc.paperLayouts` (F1);
    `RestoreDocumentFromSnapshot` clears the swap flag.
  - `src/commands/CadCommands_Ucs.cpp` — `ApplyPlanViewOf` floating branch writes
    `CurrentViewport(st)->cam{Az,El,Roll}Deg` only (AC-4).
  - `src/io/GsIo.cpp` — `UcsFrameToJson`/`UcsFrameFromJson` free helpers; per-viewport `"ucs"`
    key (additive, `!IsWorld` gate); `BuildRoot` writes the drawing frame via `CadDrawingScopedUcs`
    and the floating viewport's live frame via a `liveViewportUcs` lambda (F1).
  - `docs/gs-file-format.txt` — the new per-viewport `ucs` key.
  - `tests/ViewportUcsTests.cpp` (new, in `GoSurveySnapTests`) — T1–T6.
- 2026-08-31 — self-verify: `./dev/test` → **888/888 pass** (was 882; +6 REQ-155). `[req155]` direct
  run: 40 assertions, 6 cases, all green. Build clean, no new warnings.

## 9. Self-verification  (verification/skills/)
- [x] build-project        — PASS (`./dev/build` clean; `./dev/test` 888/888)
- [x] architecture-review  — PASS (§7.1 + post-impl: `util/ucs.hpp` is a downward include; two new
      `AppCommandState` fields mirror the `blockEditor*` session-state precedent; no new abstraction;
      additive `.gs` key per ADR-020(d)/ADR-025(g)/D-2026-08-31-c; no Workshop architectural decision)
- [x] code-review          — PASS (swap is symmetric enter/exit + guarded against double-enter;
      persistence goes through `CadDrawingScopedUcs`/`liveViewportUcs` so a save/tab-switch mid-float
      records the right frame; broken frame on load falls back to World, REQ-201)
- [x] dependency-audit     — PASS / n-a (no new dependency)
- [x] performance-review   — PASS / n-a (one pointer compare per viewport on save; nothing per-frame)
- [x] testing              — PASS (T1–T6 map 1:1 to REQ-155's acceptance bullets; T1/T5 fail against
      pre-change code — no swap ⇒ `10,0` resolves in World not the viewport frame)

## 10. Verification result
- Submitted:  2026-08-31 (self-verified; ready for review-team sign-off)
- Verdict:    self-verify PASS. SPEC GAP (2026-08-31) resolved by D-2026-08-31-c.
- Findings:   F1 (persistence isolation) — fixed in `SaveDocumentToSnapshot` + `BuildRoot`, covered by T6.
- Known limitation / debt: `UCS Previous` stack is shared drawing+viewport while floating — a
  `UCS Previous` after exiting could restore a viewport frame as the drawing frame. No data loss.
  Follow-up: scope `ucsPrevious` per floating session, or push/pop it on enter/exit. Not a REQ-155
  acceptance item.

## 11. Outcome
- Requirements satisfied: REQ-155 (Acceptance met: yes — T1–T6).
- Tests added:            `tests/ViewportUcsTests.cpp` — T1 coord-entry-in-viewport-frame,
                          T2 UCSFOLLOW re-plans only active viewport, T3 field independence,
                          T4 `.gs` round-trip + legacy-loads-World, T5 readout frame, T6 save-while-floating.
- Refactors:              `GsIo` UCS-frame JSON extracted to `UcsFrameToJson`/`UcsFrameFromJson`
                          free helpers (was a `BuildRoot`-local lambda + a read-side lambda).
- Docs updated:           `docs/gs-file-format.txt`; `spec/` (REQ-155, ADR-025 (e·2), D-2026-08-31-c);
                          `spec/requirements.md` REQ-155 verification row → mark `done (TASK-157)` on sign-off.
- Done:                   pending review-team sign-off + a GUI spot-check (float a viewport, `UCS`,
                          confirm the grid/crosshair rotate in that viewport only).

## 12. Completion report (draft — final on sign-off)
```
COMPLETION REPORT — TASK-157 — 2026-08-31
- Requirements satisfied:  REQ-155 (Acceptance met: yes — ViewportUcsTests T1–T6)
- Summary:                 Each paper-space Viewport carries a ucs::Ucs active frame; entering
                           floating model space (REQ-036) swaps it into AppCommandState::activeUcs
                           so coordinate entry / grid / ORTHO / readout / UCSFOLLOW all resolve
                           against the viewport's frame; UCSFOLLOW re-plans only that viewport's
                           REQ-061 camera. Additive .gs key; legacy files load all-World.
- Tests:                   T1 coord-entry-in-viewport-frame, T2 UCSFOLLOW isolation, T3 field
                           independence, T4 .gs round-trip + legacy load, T5 readout frame,
                           T6 save-while-floating records the drawing frame. 888/888 suite green.
- Verification verdict:    self-verify PASS (findings: F1 fixed + T6). Review-team sign-off pending.
- Assumptions:             ASSUMPTION-1 (value not name) — validated by user + spec amendment.
                           ASSUMPTION-2 (AC-4 = state/plot, not interactive view) — holds; the
                           rotated interactive floating view is REQ-061's own deferred follow-up.
- Architectural decisions: none by Workshop. D-2026-08-31-c (spec) settled ownership + format.
- Dependencies:            none added.
- Technical debt noted:    ucsPrevious stack is shared drawing+viewport while floating (no data
                           loss); follow-up = scope it per floating session. `PLAN` while floating
                           now plans the viewport camera (a sensible consequence, not in scope).
- Build:                   clean on Windows/MSVC (./dev/build, ./dev/test), no new warnings.
- Docs updated:            docs/gs-file-format.txt; spec/{requirements,architecture,project}.md.
```
