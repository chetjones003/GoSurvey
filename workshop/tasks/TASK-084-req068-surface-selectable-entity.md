# TASK-084 — Make a TIN surface a selectable entity

- Type:    feature
- Status:  plan
- Opened:  2026-08-21
- Owner:   Workshop

## 1. Authority

- Goal:         GOAL-05 (terrain modelling — M-Surfaces)
- Requirements: **REQ-068** (accepted) — the selection/erase/undo half, never implemented.
                **REQ-076** (accepted) — stable entity identity, which surfaces do not currently have.
                **REQ-084** (accepted) — object isolation, whose gate is keyed on that id.
                **REQ-201** (accepted) — a refusal is stated, never silent.
- Constraints:  CON-07 (build reproducibility), architecture §11.5 (shared payload, never deep-copied),
                §11.9 (reference by id, never by index).
- Authority for the architectural shape: **ADR-036 (a), (b), (c)** + decision **D-2026-08-21-a**.

### Acceptance (restated verbatim from the spec)

REQ-068:
- "surfaces participate in layers, visibility, selection, erase, undo and view extents"
- "erasing a surface is undoable in one step, and the restored surface is the same triangulation"
- "a surface on a frozen or off layer is not drawn, and one on a non-plottable layer is not plotted"
- "**an edit unrelated to the surface — drawing a line — does not copy the triangulation**: the undo
  snapshot shares the payload, asserted on the shared pointer rather than by inspection"

REQ-076:
- "an entity's id is unchanged by erasing a different entity, by undo/redo, by copy/paste, and by a
  `.gs` save/load round trip"
- "a reference to an erased entity resolves to **nothing**, and specifically not to the entity that
  moved into its former index"
- "a legacy `.gs` loads with ids assigned deterministically — loading the same file twice yields the
  same ids"
- "ids are not reused after an erase within a session, and are still not reused after a save/load"

REQ-084 (d): "an isolated-out entity is invisible, so it must not answer a click either."

- Owning subsystem: **Domain** (`commands/`) for identity + selection; **UI** (`ui/CadUi.cpp`) for the
  pick/hover/Properties surfaces; **Renderer** for the highlight buffer. No new layer.

## 2. Scope

- **In scope:** `EntityKind::Surface`; `SelectedEntity::Type::Surface`; surfaces enter the id sweep;
  click-pick, box-select, hover highlight, selection highlight, DELETE, Properties, SELECTSIMILAR,
  the selection-cycling list, isolation gating; explicit refusal with a stated reason from every
  transform command and from the clipboard.
- **Out of scope:** surface styles (TASK-085), analysis/banding (TASK-086), contour extraction
  (REQ-071, deferred by the user), moving/rotating/scaling a surface (ADR-036 alternative (5),
  declined), component-level selection (REQ-070 forbids it), DXF/DWG export of surfaces (REQ-068
  excludes it, already implemented).
- **Smallest change:** one appended `EntityKind`, one appended `SelectedEntity::Type`, and a case at
  each of the twelve funnels ADR-036 (c) enumerates. No new store — `cadSurfaces` and
  `cadSurfaceAttrs` both already exist and already round-trip `.gs`.

## 3. Architectural boundary check  (workflow.md §4)

- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **Yes → escalated and RESOLVED before planning.** A tenth `EntityKind` and a tenth
      `SelectedEntity::Type` are a public-API change, and filling a surface's `id` is an additive
      data-format change. Escalated as a SPEC GAP, decided by the user, and recorded as **ADR-036 (a)
      (b) (c)** + decision **D-2026-08-21-a** on 2026-08-21. The Workshop is implementing that
      decision, not making it.
- No new dependency. No new global. No new layer. No ownership change — `cadSurfaces` keeps its one
  visible owner.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | What does clicking a surface select — the whole object, the whole object plus transform support, or the component under the cursor? | 2026-08-21 | **The whole surface object.** Transform support declined (would be discarded by the next rebuild — ADR-036 alt (5)); component selection declined (REQ-070 forbids it). |
| Q2 | Should `SurfaceRebuildAsync` keep keying on `surfaceName` once surfaces have ids? | 2026-08-21 | Not asked — not a judgement call. The name key is a latent defect (a rename mid-rebuild orphans the result); ADR-036 (a) makes it the id. Recorded here so the change is not mistaken for scope creep. |

## 5. Assumptions

```
ASSUMPTION-1: Appending EntityKind::Surface to the END of kEntityKindsInSweepOrder leaves every
              existing drawing's id assignment bit-identical.
- Because:    REQ-076 requires a legacy .gs to load with deterministic ids, and AssignMissingEntityIds
              walks the arrays in sweep order. Appending cannot change what the first nine arrays get.
- Risk if wrong: a legacy drawing reloads with different ids than it had, breaking every stored
              breakline/boundary reference in it — REQ-069's dangling-reference guarantee inverted.
- Validate by: a test that loads a committed legacy .gs twice and asserts identical ids, plus one
              that loads a .gs saved BEFORE this change and asserts the non-surface ids are unchanged.
```

```
ASSUMPTION-2: Picking a surface by proximity to its triangle EDGES is sufficient, and a pick in the
              middle of a large triangle need not hit.
- Because:    REQ-068 says surfaces "participate in selection" without defining the pick region, and
              every other pickable non-filled entity in the codebase picks by proximity to its
              drawn geometry.
- Risk if wrong: on a sparse TIN with very large triangles, the interior of a triangle is unclickable,
              which reads as "the surface is not selectable here."
- Validate by: ask the user if it shows up in use. Interior picking is cheap to add later
              (SurfaceElevationAt already does point-in-triangle) and needs no data change, so this is
              deliberately deferred rather than guessed at.
```

## 6. Plan

### Approach

A surface joins the **Mesh tier** — display-only. Every funnel gains a case; every command that would
have to write geometry refuses with a stated reason. Nothing is added to the geometry snapshot: both
`cadSurfaces` and `cadSurfaceAttrs` are already captured and restored.

### Files / functions to touch

| File | Change |
|---|---|
| `commands/CadCommands.hpp` | `SelectedEntity::Type::Surface = 10` (appended); `EntityKind::Surface` (appended); declare `PickSurfaceAt`. |
| `commands/CadCommands.cpp` | `kEntityKindsInSweepOrder` += `Surface`; `AttrsForKind` += case; `PickClosestCadEntity` enumerates surface triangle edges through the existing `consider` funnel (so isolation gating is inherited, not re-implemented); `ComputeSelectionFromRect`; `ExecuteDeleteSelection` erase block mirroring the mesh block; `SelectSimilarToCurrentSelection`; `CopySelectionToClipboard` refusal; `SurfaceRebuildAsync::surfaceName` → `surfaceId`. |
| `ui/CadUi.cpp` | `CadSelectedEntityIdOf` case; hover pick; selection + hover highlight buffers; Properties panel read-only surface block (name, style, layer, colour, point/triangle count, elevation range, state); selection-cycling label; transform-command refusals. |
| `viewport/TransformPreview.cpp` | Surface is excluded from transform previews, explicitly. |
| `util/docinvariants.cpp` | `cadSurfaceAttrs.size() == cadSurfaces.size()` is already enforced at `CadCommands.cpp:9602`; add it as a real invariant so it is checked rather than repaired. |
| `tests/` | `SurfaceSelectionTests`. |

### Test approach

- **Happy path:** a pick within tolerance of a surface triangle edge returns
  `SelectedEntity::Type::Surface` with the right index; DELETE removes it and one UNDO restores it
  with the **same `shared_ptr`** (REQ-068's payload-sharing acceptance, asserted on the pointer).
- **Failure modes:**
  - a surface on an **off** or **frozen** layer does not answer a click;
  - a surface in the REQ-084 hidden set does not answer a click;
  - erasing a *different* entity leaves the surface's id unchanged (REQ-076);
  - a legacy `.gs` loaded twice yields identical ids, and the non-surface ids match a pre-change save;
  - MOVE with a surface in the selection **says why it refused** rather than silently no-op'ing.

### Steps

- [ ] 1. Append the two enum members; add `AttrsForKind` + sweep-order cases. Build — every
      non-exhaustive switch that now warns is a funnel to visit, which is the point of doing this first.
- [ ] 2. Pick + box-select + hover, all through the existing `consider` / `hits` funnels.
- [ ] 3. Highlight buffers; Properties block; selection-cycling label.
- [ ] 4. Erase + undo; `SelectSimilar`.
- [ ] 5. Explicit refusals: clipboard, MOVE/COPY/ROTATE/SCALE/MIRROR/STRETCH/ALIGN, grips.
- [ ] 6. `SurfaceRebuildAsync` re-keyed to the stable id.
- [ ] 7. `SurfaceSelectionTests` + a headless transcript.
- [ ] 8. Self-verification (§9).

## 7. Workflow-specific notes

- Feature: pre-flight answered (Q1). **Tests before the refusals** — a refusal that does nothing and a
  refusal that is missing are indistinguishable at runtime, which is ADR-035 (g)'s stated risk and the
  reason ADR-036 (c) enumerates the funnels as an acceptance condition rather than a review habit.

## 8. Implementation log

- 2026-08-21 opened; authority confirmed (REQ-068 selection was never implemented); ADR-036 +
  D-2026-08-21-a recorded before planning.

## 9. Self-verification
- [ ] build-project
- [ ] architecture-review
- [ ] code-review
- [ ] dependency-audit — n/a (no dependency added)
- [ ] performance-review — pick cost over a 200k-triangle surface against the REQ-100 budget
- [ ] testing

## 10. Verification result

### Plan review — 2026-08-21 (workflow §3, before implementation)

```
REVIEW VERDICT — TASK-084 plan — 2026-08-21
- Outcome:   PASS (plan stage; implementation not yet reviewed)
- Domains:   arch ✓   quality ✓   deps ✓   perf ✓
- Findings:  0 blocking, 0 advisory
```

Confirmed at plan stage:
- Authority is real and pre-existing — REQ-068 requires selection and it was never built. No invented
  requirement (§7.2 clear).
- Invariant 9 is satisfied and, more than that, **repaired**: this task is what gives surfaces the
  stable id that TASK-085's cache and REQ-084's isolation gate both need.
- Invariant 5 is untouched: nothing here writes through the `shared_ptr<const CadTin>`.
- **ASSUMPTION-2 (edge-proximity picking) is accepted as a deliberate deferral, not a gap.** REQ-068
  does not define the pick region, interior picking needs no data change to add later, and
  `SurfaceElevationAt` already has the point-in-triangle test if it turns out to be wanted. Recorded
  so it is a decision on the record rather than a limitation someone discovers.

- Implementation review: **not yet submitted.**

## 11. Outcome
- —
