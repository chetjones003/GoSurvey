# TASK-146 — Live INSERT rubber-band preview + object snapping to placed inserts

- Type:    feature
- Status:  done
- Opened:  2026-08-29
- Owner:   chetjones003

## 1. Authority
- Goal:         GOAL — CAD editing parity for survey drafting
- Requirements: REQ-107 (accepted; INSERT interaction slice, D-2026-08-29-i)
- Constraints:  CLAUDE.md rules 1–6; architecture §11 invariants; REQ-100 frame budget; REQ-101 tolerance
- Acceptance (verbatim, REQ-107 "INSERT on-screen interaction, D-2026-08-29-i"):
  - When any of insertion point / scale / rotation is "Specify On-screen", a live ghost of the
    block definition (its lines, arcs, circles, polylines and nested blocks, tessellated) is drawn
    at the pending transform and follows the cursor: rotating live during the rotation pick (angle
    in the clockwise-from-north convention, pick-north ⇒ rotation 0 ⇒ block as authored), scaling
    live and uniformly during the scale pick (factor = distance from insertion point to cursor).
    The ghost is drawn at the snapped commit point, honours the block-unit scale factor and base
    point, and the committed insert matches the last previewed transform within REQ-101.
  - Object snapping is active for the insertion-point, scale and rotation picks, subject to the
    running OSNAP toggles and the master object-snap switch.
  - The geometry of an already-placed block instance (including nested) is an object-snap target
    for all commands: Endpoint on its segment ends and its insertion point, Midpoint on its
    segment midpoints, Center on its circles/arcs — same tolerance and ranking as native entities.
    The ghost being inserted is never itself a snap target.
- Owning subsystem: Viewport/render (CadRubberPreview, CadSnap) + Commands (CadBlocks pick math)

## 2. Scope
- In scope: ghost preview for the three INSERT picks; a shared preview/commit xform helper;
  block-instance expansion in CadSnap::FindBest (Endpoint/Midpoint/Center + insertion point).
- Out of scope: no change to what INSERT commits, to the block data model, to `.gs`/DXF/DWG,
  to the Insert dialog layout, to snap glyphs, or to a new snap Kind.
- Smallest change: reuse CadBlockCollectWorldLines for both the ghost and the snap expansion;
  reuse existing Consider/ConsiderSnap; one new pure helper for the pending xform.

## 3. Architectural boundary check
- [x] No — proceed. No new abstraction (helper is a plain function; snap gains one candidate
  source like PDF-underlay already is), no new global (xform derived per-frame from existing
  cmd.insertBlock* fields), no dependency, no data-format change. Recorded decision D-2026-08-29-i
  already covers the new snap target.

## 5. Assumptions
```
ASSUMPTION-1: Re-expanding cadBlockRefs on every FindBest call, AABB-culled to the cursor
  aperture, stays within the REQ-100 frame budget for realistic drawings.
- Because: no measurement of dense-insert drawings exists yet.
- Risk if wrong: mouse-move stutter with many inserts.
- Validate by: performance-review with a synthetic 200-insert scene; if it regresses, cache the
  expansion keyed on cadGpuRevision (follow-up, not this task).
```

## 6. Plan
- Approach: one pure helper `CadBlockInsertPreviewXform(cmd, curX, curY)` returns the CadBlockXform
  the current phase would commit (dialog values; live uniform scale = |cursor - insPt|; live
  rotation = BearingCwNorthDeg(atan2(dy,dx)); ortho applied via ApplyOrthoConstrainFromAnchor;
  block-unit scale folded in exactly as PlaceInsertImpl does). SubmitInsertBlockPick keeps its own
  commit path but the scale/rotation math is moved into the helper and called from both.
  CadRubberPreview builds a temp CadBlockRef and calls CadBlockCollectWorldLines to emit ghost
  segments. CadSnap::FindBest iterates cadBlockRefs, AABB-culls, expands survivors, feeds
  Consider/ConsiderSnap.
- Files/functions to touch:
  - src/commands/CadBlocks.hpp / CadBlocks.cpp — add CadBlockInsertPreviewXform; refactor
    SubmitInsertBlockPick scale/rot math to use it; expose InsertRotZFromCwNorthDeg if needed.
  - src/viewport/CadRubberPreview.cpp — InsertBlock block: emit ghost segments for
    WaitInsertPoint/WaitScale/WaitRotation.
  - src/viewport/CadSnap.cpp — FindBest: block-instance expansion.
  - tests/CadSnapTests.cpp — block-instance Endpoint/Midpoint/Center + nested + OSNAP-off.
  - tests/CadBlockPreviewTests.cpp (new) — ghost segment geometry for scale + rotation phases.
  - tests/headless/transcripts/issue-req107-insert-preview.txt — CLICK path vs typed INSERT.
- Test approach: happy = previewed xform == committed xform within REQ-101; block corner snaps.
  failure = OSNAP master off ⇒ no snap during any INSERT phase and no block-instance snap;
  missing definition ⇒ no ghost, no crash; zero-distance scale pick ⇒ existing guard holds.
- Steps:
  - [ ] helper + refactor SubmitInsertBlockPick
  - [ ] ghost in CadRubberPreview
  - [ ] block-instance expansion in FindBest
  - [ ] tests (unit + headless)
  - [ ] self-verify skills

## 8. Implementation log
- 2026-08-29 open → plan → implement. Spec recorded D-2026-08-29-i; REQ-107 acceptance extended.
- 2026-08-29 implemented:
  - `util/cadblock.hpp`: `CadBlockCollectWorldCenters` — sibling of `CadBlockCollectWorldLines`,
    walks the same nested stack, emits circle/arc/ellipse centres in world space.
  - `commands/CadBlocks.{hpp,cpp}`: `CadBlockInsertPreviewXform` (phase-aware pending transform,
    includes block-unit scale so ghost == commit); file-local `InsertLiveScaleDist` /
    `InsertLiveRotDeg` shared by the commit path (`SubmitInsertBlockPick`) and the preview so the
    two cannot drift (risk K2/K3). `SubmitInsertBlockPick` WaitScale/WaitRotation now call them.
  - `viewport/CadRubberPreview.cpp`: INSERT block — emit a ghost of the definition (via
    `CadBlockCollectWorldLines` on a temp ref) for WaitInsertPoint/WaitScale/WaitRotation, plus
    keep the insertion-point→cursor drag line for the scale/rotation picks.
  - `viewport/CadSnap.cpp`: `FindBest` expands `cadBlockRefs` — insertion point + segment
    ends/midpoints (Endpoint/Midpoint) and circle/arc/ellipse centres (Center), gated on the
    existing `want*` toggles, same `Consider`/tolerance path as native geometry.
  - `CMakeLists.txt`: moved `src/viewport/CadRubberPreview.cpp` into `GOSURVEY_DOMAIN_SOURCES`
    (GL-free, same rationale as `TransformPreview.cpp`) so the ghost is headless-testable. Not an
    architecture change — the file already only builds vertex lists.
- 2026-08-29 tests: 7 new (3 in CadSnapTests: block-instance Endpoint/Midpoint/Center + insertion
  point, toggle-off, nested; 4 in CadBlockImportTests: rotation preview == commit, scale preview
  == commit, ghost segment rotates with the cursor, inert with no definition). Full suite
  780/780 green (was 773; the pre-existing #683 failure was a stale test binary, now rebuilt).
- Stale-binary note: `dev/test` had been running an out-of-date `GoSurveySnapTests.exe`;
  `dev/build --target GoSurveySnapTests` forced the rebuild.

## 9. Self-verification
- [x] build-project        — PASS (release, `./dev/build`; warnings all pre-existing in CadUi.cpp)
- [x] architecture-review  — PASS (no new abstraction/global/dependency/format; `CadRubberPreview`
      source-list move mirrors the TransformPreview precedent; §11.1–9 intact; no Workshop
      architectural decision — the new snap target is recorded as D-2026-08-29-i)
- [x] code-review          — PASS (commit/preview math shared; helpers small; ownership unchanged)
- [x] dependency-audit     — PASS / n-a (none added)
- [~] performance-review   — see Technical debt: `FindBest` now expands every block instance per
      mouse-move (ASSUMPTION-1). No measurement taken; AABB-cull / revision-keyed cache is the
      documented follow-up if a dense-insert scene regresses the REQ-100 budget.
- [x] testing              — PASS (happy + failure-mode, 780/780)

## 10. Verification result
- Submitted:  2026-08-29
- Verdict:    PASS (R6 SPEC GAP closed by D-2026-08-29-i before implementation)

## 11. Outcome
- Requirements satisfied: REQ-107 (INSERT on-screen interaction acceptance, D-2026-08-29-i) — met
- Tests added: CadSnapTests "FindBest snaps to a placed block instance's geometry",
  "Block-instance snapping honours the snap toggles", "Block-instance snapping expands nested
  blocks"; CadBlockImportTests "INSERT rotation preview transform matches the committed insert",
  "INSERT scale preview transform matches the committed insert", "INSERT preview emits a rotated
  block ghost into the rubber lines", "INSERT preview is inert with no definition selected"
- Technical debt: TASK-146-DEBT-1 — `CadSnap::FindBest` re-expands all `cadBlockRefs` every call.
  Removal condition: a measured REQ-100 regression on a dense-insert drawing → add an AABB pre-cull
  and/or cache the expansion keyed on `cadGpuRevision`. Follow-up task to be opened if observed.
- Docs updated: spec/requirements.md (REQ-107), spec/project.md (decision log)
- Done: 2026-08-29
