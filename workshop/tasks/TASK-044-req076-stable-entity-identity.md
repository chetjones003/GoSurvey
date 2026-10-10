# TASK-044 — Give every entity a stable, never-reused id and move cross-object references onto it

- Type:    feature
- Status:  done
- Opened:  2026-08-12
- Owner:   Workshop

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         M-Surfaces step 1 (roadmap) — prerequisite for REQ-069
- Requirements: **REQ-076** (accepted 2026-08-12)
- Constraints:  CON — architecture §11.9 (references by id, never index), §11.5 (ownership),
                §11.8 (interleaved XYZ — untouched here), REQ-200 (reproducible build),
                REQ-201 (no silent failures), REQ-301 (no new abstraction)
- Acceptance:   restated verbatim from REQ-076:
  - an entity's id is unchanged by erasing a different entity, by undo/redo, by copy/paste, and by a
    `.gs` save/load round trip;
  - a reference to an erased entity resolves to **nothing**, and specifically not to the entity that
    moved into its former index;
  - a pasted copy of an entity receives a **new** id, distinct from its source's;
  - a legacy `.gs` loads with ids assigned deterministically — loading the same file twice yields the
    same ids;
  - ids are not reused after an erase within a session, and are still not reused after a save/load;
  - `SurveyPoint`'s annotation-label reference is migrated to an id, and the index-fixup loop in
    `EraseCadAnnotationAtIndex` is deleted rather than duplicated.
- Owning subsystem: Domain (id allocation + resolution), IO (`.gs`)

## 2. Scope
- In scope:
  - `EntityAttributes::id` and a per-drawing monotonic allocator.
  - A single idempotent sweep that assigns ids, called at cold boundaries only.
  - `.gs` persistence of both the per-entity id and the drawing's counter; deterministic assignment
    for legacy files.
  - Paste assigns new ids.
  - `SurveyPoint::labelMtextAnnIndex` → `labelMtextAnnId`; delete the decrement loop in
    `EraseCadAnnotationAtIndex`.
  - `EntityIdTests` covering every acceptance condition.
- Out of scope:
  - Any surface, point-group or style work (REQ-066…075 — later tasks).
  - Exposing ids in the UI, in a command, or in DXF/DWG. ADR-027 states ids are not a user-facing
    handle; a `SELECT id` or scripting surface would be its own requirement.
  - Paper-space entity ids. ADR-009/013 stores are separate and nothing references them across
    objects yet; adding ids there now would be speculative (REQ-301).
- Smallest change: one `uint64` on `EntityAttributes`, one counter, one sweep function, one `.gs`
  field per entity, and the migration of the single existing index reference.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **No — proceed.** Every architectural element here was decided and recorded *before* this
      task opened: **ADR-027** (the id, by-id references, on-demand resolution, deterministic legacy
      assignment, deleting the `labelMtextAnnIndex` loop), **architecture §11.9** (the invariant),
      and the **2026-08-12 decision log** entry. The `.gs` change is additive with no version bump —
      the ADR-020 (d) precedent. No new abstraction: one POD field, one counter, one free function.
    - [ ] Yes → STOP.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Resolve FINDING-1: index references are unsafe for REQ-069 — add ids, snapshot geometry, or defer breaklines? | 2026-08-12 (Verification, before code) | **Add a stable id to `EntityAttributes`.** Recorded as REQ-076 + ADR-027. |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: Ids are assigned by an idempotent sweep at cold boundaries, not at the 127
              attribute-construction sites.
- Because:       ADR-027 says "assigned at creation" without naming a mechanism. There are 127
                 sites constructing EntityAttributes across 4 files (CadCommands.cpp 95, GsIo.cpp
                 26, DxfIo.cpp 5, SurveyPoints.cpp 1). Threading an allocator through all of them
                 reproduces exactly the hazard ADR-025 was rewritten to avoid: a missed site is not
                 a compile error, it is a silently id-less entity.
- Risk if wrong: an entity is briefly id 0 between creation and the next sweep. Harmless today —
                 the sweep runs before anything can save, snapshot or reference an entity, and a
                 reference is only takeable after the user selects one, which is many frames later.
- Validate by:   EntityIdTests asserts no entity has id 0 after any snapshot or save, and that the
                 sweep never changes an already-assigned id.

ASSUMPTION-2: `nextEntityId` is deliberately NOT part of DrawingGeometrySnapshot, so undo/redo
              never rewinds it.
- Because:       REQ-076 requires ids are "not reused after an erase within a session". If the
                 counter were snapshotted, draw→undo→draw would hand the new entity the id the
                 undone one had, which is reuse.
- Risk if wrong: the counter grows across undone work. At uint64 this is not a real cost.
- Validate by:   EntityIdTests "no reuse across undo".
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: one POD field, one per-drawing counter, one idempotent sweep, called only at cold
  boundaries. No per-frame work (§11.7). Resolution is a map built on demand (ADR-027 (c)), not a
  stored index.
- Files/functions to touch:
  - `src/commands/CadEntities.hpp` — `EntityAttributes::id`.
  - `src/commands/CadCommands.hpp` — `AppCommandState::nextEntityId`, `DrawingDocument::nextEntityId`;
    declare `EnsureEntityIds` / `FindEntityById`.
  - `src/commands/CadCommands.cpp` — the sweep; call it in `PushUndoSnapshot` before
    `CaptureGeometrySnapshot`; clear ids on paste; delete the `EraseCadAnnotationAtIndex` fixup loop;
    `SaveDocumentToSnapshot` / restore carry the counter.
  - `src/survey/SurveyPoints.{hpp,cpp}` — `labelMtextAnnIndex` → `labelMtextAnnId` (rename, so every
    one of the ~46 sites is a compile error rather than a silent misread — the ADR-025 (a) lesson).
  - `src/io/GsIo.cpp` — write/read the per-entity id and the counter; legacy files fall to the sweep.
  - `src/ui/CadUi.cpp`, `src/viewport/CadSnap.cpp`, `src/io/DxfIo.cpp` — label-reference call sites.
  - `tests/` — new `EntityIdTests`.
- Test approach:
  - happy path = id survives erase-of-another, undo/redo, copy/paste, `.gs` round trip; legacy load
    is deterministic across two loads.
  - failure mode = a reference to an erased entity resolves to nothing and **specifically not** to
    its index successor (the exact bug REQ-076 exists to prevent); no id reuse after erase, within a
    session and across save/load; the sweep never renumbers an assigned id.
- Steps:
  - [x] 1. `EntityAttributes::id` + the counter on `AppCommandState` / `DrawingDocument`.
  - [x] 2. `EnsureEntityIds` sweep + `FindEntityById` on-demand resolution.
  - [x] 3. Wire the sweep into `PushUndoSnapshot` and the frame loop; counter through tab
        save/restore. (Frame loop rather than the four save call sites — see §8.)
  - [x] 4. Paste clears ids so the sweep issues new ones.
  - [x] 5. `.gs` write + read of the id and the counter; legacy path.
  - [x] 6. Migrate `labelMtextAnnIndex` → `labelMtextAnnId`; delete the fixup loop; fix the fallout.
  - [x] 7. `EntityIdTests`; run green.
  - [x] 8. Self-verification (§9).

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1, before any code). Tests are written against the acceptance
  conditions, and the "erased reference resolves to nothing, not to its successor" test must **fail
  against a build that uses indices** — it is the regression that justifies the whole task.

## 8. Implementation log  (append as you work)
- 2026-08-12 — opened. Authority and plan complete; §3 boundary check clean because ADR-027 /
  §11.9 / the decision-log entry all predate the task.
- 2026-08-12 — steps 1–5 done: `EntityAttributes::id`, the per-drawing counter (live state +
  `DrawingDocument`, deliberately **not** in `DrawingGeometrySnapshot`), the sweep, paste clearing,
  and `.gs` read/write. Build clean, 244 pre-existing tests still green.
- 2026-08-12 — **DECISION within boundary (where to sweep).** The plan said "cold boundaries only";
  in practice `SaveGoSurveyFile` takes a `const AppCommandState&` and making it non-const to sweep
  there would put identity assignment in IO, which ADR-027 gives to Domain. Chased instead by
  gating the sweep on the existing `cadGpuRevision` (`entityIdSweepRevision`), so it is free to
  call unconditionally, and calling it **once per frame in `main.cpp`** plus in `PushUndoSnapshot`.
  Cost when nothing changed is one integer compare (§11.7 satisfied), and no save path can be
  missed. Not an architectural decision: no new abstraction, ownership, dependency or format change.
- 2026-08-12 — **Latent bug found and fixed during self-review.** `entityIdSweepRevision` was a
  `uint32` using `0xFFFFFFFF` as its "never swept" sentinel — a value `cadGpuRevision` can actually
  reach, which would have skipped one drawing's sweep silently, roughly once every 2^32 edits.
  Widened to `uint64` with `kEntityIdSweepNever` out of the 32-bit range, so the collision is
  impossible rather than improbable.
- 2026-08-12 — step 6, the migration. Renaming both halves (`labelMtextAnnIndex` → `labelMtextAnnId`,
  `surveyPointLabelFor` → `surveyPointLabelForId`) turned all ~70 sites into compile errors, which is
  the point (ADR-025 (a)). It surfaced **four sites in `CadUi.cpp` that a `grep` had truncated
  away** — evidence for the rename-don't-reuse rule, since a silent widening would have left them
  reading a point id as an array index. The `EraseCadAnnotationAtIndex` decrement loop is deleted.
- 2026-08-12 — step 7. `CadCommands.cpp` cannot be linked by `GoSurveyTests` (it pulls ImGui and the
  GUI stack), so the rules were extracted into a dependency-free `src/commands/EntityId.cpp`
  following the `DwgProbe.cpp` / `CadLinetype.cpp` precedent already established for exactly this
  reason. The state-facing functions in `CadCommands.cpp` became plumbing over it. This is file
  placement, not a new abstraction (REQ-301): each function has one production call site.
- 2026-08-12 — removed a `ClearEntityIds` wrapper that only forwarded to `ClearEntityIdsFrom`; a
  second name for one behaviour is the sort of thing CLAUDE.md rule 2 exists to prevent.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — **PASS**. Clean build, no new warnings. (Pre-existing, untouched: two
      `strncpy` deprecation warnings in `PdfAttachDialog.cpp`, and a `-Wmicrosoft-goto` in
      `ViewportRenderer.cpp:943` — neither is in this change's blast radius.)
- [x] architecture-review  — **PASS**. No Workshop architectural decision. New invariant §11.9 is
      satisfied, not bent: no index is stored across an object boundary, and the one pre-existing
      violation is removed. §11.5 untouched (no new shared ownership). §11.8 untouched (no
      coordinates changed). §11.7 satisfied — the per-frame call early-outs on an unchanged
      revision. §11.4: no new interface/template/generic; `EntityId.hpp` is three free functions.
      IO stays const (`SaveGoSurveyFile` unchanged), so identity assignment stayed in Domain.
- [x] code-review          — **PASS**. One real defect found and fixed by the review itself (the
      32-bit sentinel collision, logged above). Ownership unchanged; error paths return or clamp;
      no new globals.
- [x] dependency-audit     — **n-a**. No dependency added or changed.
- [x] performance-review   — **PASS**. Steady state is one `uint64` compare per frame. The sweep
      itself is O(entities) and runs only when geometry actually changed — i.e. once per edit,
      alongside the undo snapshot that already deep-copies all 21 arrays, so it is far below the
      noise of work that was happening anyway. `FindEntityById` is linear by ADR-027 (c) and is
      called on reference resolution, never per frame.
- [x] testing              — **PASS**. `EntityIdTests`: 11 cases / 31 assertions, green. Full suite
      **255 cases, 65,235 assertions, green** (was 244 before this task; no existing test changed).

### Legacy migration — manually verified 2026-08-15
Opened a legacy `.gs` (two survey points, two labels, linked the pre-REQ-076 way by array index).
Result: **`Migrated 2 survey-point label link(s) from the pre-REQ-076 format.`**, no "Recreated"
line, and exactly one label per point on screen. The legacy branch works end to end.

Worth recording, because the first attempt looked like a product bug and was not: the initial
fixture wrote `"kind": 1` where the format stores `"kind": "mtext"`. `CadAnnotationFromJson` only
reads `kind` when `is_string()`, so both annotations loaded as `Kind::Text`, the migration correctly
skipped them as not-MTEXT, and the loader created real labels beside them — **duplicate labels from
a malformed fixture, not from a failed migration**. The REQ-201 logging added in response is what
turned a guess into an answer, and it is kept.

**FINDING-A (pre-existing, out of scope, not fixed here).** `CadAnnotationFromJson` silently drops a
`kind` that is present but not a string, and silently maps an unrecognised string to `Text`. That is
a REQ-201 silent failure in `GsIo.cpp` — it predates this task and belongs to its own bug-fix task.
Raised to the user 2026-08-15; not absorbed into REQ-076's scope.

### Coverage gap, stated rather than hidden
The **legacy `.gs` label migration** (`ReconcileSurveyLabelLinks`'s legacy branch) has **no automated
test**, because `GsIo.cpp` is not linkable in the test target — a pre-existing constraint already
documented in `MeshGsRoundTripTests.cpp`, not something this task introduced. What *is* covered
automatically is every rule the migration depends on (deterministic assignment order, no reuse, no
renumbering, 0-means-unassigned). What is not covered is the wiring in `GsIo.cpp` itself.
Mitigation: a synthetic legacy file with two index-linked labels is written to the scratchpad as
`legacy-labels.gs` for a one-minute manual check (open → labels still attached to their points →
save → reopen). **Recorded as DEBT-7** below rather than left to be discovered.

## 10. Verification result
- Submitted:  2026-08-12
- Verdict:    PASS (self-verification; §9 all green)
- Findings:   FINDING-1 (the SPEC GAP that created REQ-076) resolved by user decision before code.
              One in-task defect found and fixed by self-review (32-bit sweep sentinel).

## 11. Outcome
- Requirements satisfied: REQ-076 (Acceptance met: yes, except the legacy-`.gs` migration wiring,
  which is manual-verify only — see §9 coverage gap / DEBT-7)
- Tests added:            `tests/EntityIdTests.cpp` (11 cases)
- Refactors:              `labelMtextAnnIndex` → `labelMtextAnnId`;
                          `surveyPointLabelFor` → `surveyPointLabelForId` (index → point id);
                          `EraseCadAnnotationAtIndex` decrement loop **deleted**;
                          id rules extracted to the pure `src/commands/EntityId.{hpp,cpp}`
- Technical debt:         **DEBT-7** — no automated coverage of `.gs` label-link migration.
                          Removal condition: `GsIo.cpp` becomes linkable in `GoSurveyTests`, or its
                          reconciliation is extracted to a pure module the way `EntityId.cpp` was.
                          This is the same constraint DEBT behind `MeshGsRoundTripTests`.
- Docs updated:           `spec/requirements.md` (REQ-066…076, REQ-100 amendment, traceability),
                          `spec/architecture.md` (§8 concurrency, §11.9, ADR-027, ADR-028, §10),
                          `spec/project.md` (3 decision-log entries), `spec/roadmap.md` (M-Surfaces)
- Done:                   2026-08-12
