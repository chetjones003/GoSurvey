# Plan — GitHub Issue #475: INSERT a 3D-solid block (3D placement + fitting library)

## Goal

Make `INSERT` work for a block whose content is a B-rep solid so a user can build 3D pipe models by inserting fittings (flanges, elbows, tees, reducers, valves) from a library — the same workflow `.sat`/block part libraries use in mechanical CAD.

Issue is the consuming side of #473 (real ACIS SAT schema + standalone `.sat` import, shipped). #473 made `.sat` drop its solid on the origin and be positioned with `MOVE` **because `INSERT` cannot place a 3D solid today**. The authoring side (`BEDIT` solid round-trip + connection points) is described in the issue's comment and is part of this plan because its data model is shared with INSERT.

## Success Criteria

- `INSERT` picks a **real 3D point** (including Z) with object snap to solid sub-objects (face/edge/vertex) and to the active UCS plane, not just `(x, y, 0)`.
- A placed block reference whose definition contains `CadBlockContent::solids` **renders its solids** (transformed) and they participate in view framing, selection filtering, and display order the same way `CadBlockCollectWorldLines` already does for 2D geometry.
- An inserted solid respects **uniform scale and 3D orientation** (X/Y/Z rotation or align-to-face); non-uniform scale is refused/flagged per `brep::Scale` being uniform-only.
- Block units no longer silently multiply a `.sat` fitting by 12; the user can set/confirm the scale.
- `BEDIT` round-trips `content.solids` (and connection points) so a fitting can be authored with the existing solid tools (`BOX`/`CYLINDER`/`EXTRUDE`/`REVOLVE`/`FILLET`/`CHAMFER`/Booleans) and `BSAVE`'d into the definition.
- A minimal fitting library (folder of `.sat`/block files) is browsable from the Insert dialog with a preview, reusing `LoadBundledBlockLibraryImpl` / `BLOCKLIB`.
- Out of scope stays out: parametric/catalog-driven regeneration (Civil 3D Parts Catalog / Plant 3D `AcPp*`, see #369/ADR-026) is not built.

## Context And Current Facts

**Issue source:** `https://github.com/chetjones003/GoSurvey/issues/475` (open, 1 comment). `gh` CLI is unauthenticated in this environment; body fetched via `curl https://api.github.com/repos/chetjones003/GoSurvey/issues/475` on 2026-09-11. Tracker file (`TRACKER.md`) has no entry for #475 yet.

**What shipped before this:**
- REQ-320/ADR-051: `CadBlockContent::solids` (`vector<CadSolidPtr>`) + `solidAttrs` round-trips through `.gs`/WBLOCK/BLOCKIMPORT; `src/util/cadblock.hpp:138`, `src/commands/CadBlocks.cpp:197` `c->solids = st.cadSolids`.
- REQ-107/ADR-043: `BEDIT` in-place editing; REQ-107 acceptance D-2026-08-29-i: live INSERT ghost + snap to placed inserts via `CadBlockCollectWorldLines`.
- REQ-318/ADR-049: `solidpick::PickSubObject` + `PickSubObjectAcrossSolids` — face/edge/vertex identity for UCS Object on planar solid faces (#156), available to reuse for INSERT.
- REQ-322: `MOVE` is 3D for solids (`brep::Translate`).
- #473/D-2026-09-10-c: `ImportSatFileToScratch` re-bases a standalone `.sat` onto the origin and **drops it as a loose `cadSolids` entry**, not via `INSERT`, with a block definition kept alongside for future INSERT.

**Owners today — measured, not inferred:**

| Area | Current behavior | Location |
|------|------------------|----------|
| INSERT pick | `SubmitInsertBlockPick(AppCommandState& st, float wx, float wy, ...)` — 2D only, `st.insertBlockZ` always 0 | `src/commands/CadBlocks.hpp:33`, `src/commands/CadBlocks.cpp:952` |
| INSERT commit | `InsertDialogXform` / `CadBlockPlaceInsert` builds `CadBlockXform{ x=insertBlockX, y=insertBlockY, z=insertBlockZ, sx*sy*sz, rotZ=InsertRotZFromCwNorthDeg(rotDeg) }` — `rotX`/`rotY` never set | `src/commands/CadBlocks.cpp:730-738` |
| Block world | `CadBlockCollectWorldLines` / `CadBlockCollectWorldCenters` / `CadBlockCollectWorldAnnotations` — no `CadBlockCollectWorldSolids` | `src/util/cadblock.hpp:449,589,637` |
| Block render | `ViewportRenderer.cpp:2023`, `render/` — expands block refs via `CadBlockCollectWorldLines` only; no solid path | `src/render/ViewportRenderer.cpp:2023` |
| Units | `CadBlockUnitsScale(blockUnits, CadDrawingInsUnitsName(drawingInsUnits))` folded into preview/commit `sx/sy/sz`; `.sat` header `num_mm_units` is unreliable (Civil 3D wrote 25.4 for foot model) | `src/commands/CadBlocks.cpp:942`, `src/commands/CadBlocks.cpp:355-358` |
| BEDIT load | `LoadBlockPrimitivesIntoDrawing` loads lines/circles/arcs/ellipses/polys/texts and **clears `st.cadSolids`** | `src/commands/CadBlocks.cpp:212-243` |
| BEDIT save | `HarvestDrawingPrimitivesIntoContent` writes those same arrays and **never touches `st.cadSolids`** | `src/commands/CadBlocks.cpp:261-287` |
| Base point bake | `CadBlockShiftContent` moves lines/circles/arcs/ellipses/polys/texts/nested but **skips `solids` and `meshes`**; `CadBlockBakeBasePoint` calls it | `src/util/cadblock.hpp:380-437` |

**Architecture constraints (from `spec/architecture.md`):**
- Downward-only layering: `Commands → Renderer → Entities/Domain → IO → Platform`. No `gl*` outside Renderer/Platform. B-rep kernel (`src/brep/`) is Domain.
- Ownership: `CadSolidPtr = shared_ptr<const brep::Solid>` — immutable payload, replace-on-edit (REQ-322). Adding shared mutable state across a block ref would violate §11 invariant 5.
- Concurrency: UI thread owns `AppCommandState`; no hot-path allocations.
- Interleaved XYZ, stable-id refs — not directly in scope but any new connector reference must use an id, not an index.

**Related requirements / ADRs:**
- REQ-320 (ACIS solid import), REQ-107 (blocks), REQ-318 (sub-object pick), ADR-049/051/052, REQ-154 (UCS), REQ-154 amendment (UCS Object on planar solid face).

## Constraints And Non-goals

**Constraints:**
- No new abstraction without ≥2 concrete uses (`spec/architecture.md` §11.4).
- No third-party ACIS/geometry kernel (REQ-300, REQ-320 acceptance).
- `brep::Scale` is uniform-only — non-uniform block scale must be refused, ignored, or warned, not approximated.
- B-rep solids are immutable `shared_ptr<const Solid>` — INSERT must not mutate a definition's solid; it transforms a copy or renders a transformed view.

**Non-goals (explicit out of scope for the epic's first increment, per issue):**
- Parametric / catalog-driven fittings that regenerate from a size parameter (Civil 3D Parts Catalog / Plant 3D `AcPp*` model — #369, ADR-026).
- Free-form/spline/sphere/torus surface kinds still deferred (#300) — any fitting needing them is refused by name until that lands.
- SAB binary ACIS (#301).
- A full connector-constraint solver; connector snap is point+direction coincidence, not a mates/constraints engine.

## Key Decisions

### D1 — Superset vs slice: treat #475 as an epic with 5–6 sub-issues, not one PR

**Recommendation:** split into sub-issues/PRs. One PR that rewrites INSERT's pick, adds a solid render path, extends BEDIT, adds connectors, and ships a library cannot be reviewed or bisected.

**Alternatives rejected:**
- Single PR — review risk, no bisectability, violates workshop rule "smallest correct solution."
- Do everything behind a feature flag in one branch — still unreviewable.

**Sub-issue sketch:**
1. **3D insert point** (pick + `SubmitInsertBlockPick` 3D).
2. **Render block-ref solids** (collect + draw + explode + framing).
3. **3D orientation** (X/Y/Z rotation + align-to-face).
4. **BEDIT solid round-trip + 3D base point** (load/harvest + `CadBlockShiftContent` for solids/meshes).
5. **Connection points** (data model + BEDIT authoring + INSERT snap).
6. **Fitting library + units UX** (Insert dialog library pane, preview, block-units choice).

Order matters: 1→2→3 can ship without 4/5; 4 is independent of 1/2; 5 depends on 4 + 2/3.

### D2 — How a block reference's solids appear: render transformed vs materialise as drawing solids

**Recommendation:** **render transformed** (linked reference). Introduce `CadBlockCollectWorldSolids` that walks `defs`/`ref.xf` (like `CadBlockCollectWorldLines`), applying `CadBlockXformPoint` + `brep::Translate`/`Rotate`/`Scale` to produce world-space `Solid` views for the renderer. Keep the definition's `CadBlockContent::solids` as the single source of truth.

**Why not "explode on INSERT" (materialise `content.solids` as new `cadSolids` entries)?**
- Exploding is already supported (`INSERT Explode` checkbox → `CadBlockPlaceInsert` with `explode=true`), but the default reference semantics exist so "editing a definition updates every insert" (REQ-107 acceptance). Materialising silently copies geometry and breaks that promise; a later BEDIT would not update prior inserts.
- A transformed-view render preserves instancing and keeps undo/edit as pointer replacement.

**Trade-off:** renderer must handle instanced solids (per-instance transform). This is the correct cost — the same one meshes already pay.

**Invariant check:** the rendered world solid is a **new `shared_ptr<const Solid>` per frame/pick**, not a mutation of the definition's payload.

### D3 — 3D pick mechanism

**Recommendation:** replace the 2D `SubmitInsertBlockPick(float wx, float wy)` path for INSERT with a 3D pick that:
- Takes the viewport cursor ray (`util/ray3d` already used for orbit snap in #372) + active `ucs::Ucs` plane and sub-object hit from `solidpick::PickSubObject`.
- Snaps to: solid face centre / edge midpoint / vertex, plus the existing endpoint/center candidates already expanded via `CadBlockCollectWorldLines`/`Centers` (REQ-107 D-2026-08-29-i), plus the active UCS XY plane as fallback (matching `MOVE 3D`'s typed `X,Y,Z` path in REQ-322).
- Stores the result in `insertBlockX/Y/Z` (Z already exists, just never set from pick).

**Alternatives rejected:**
- Keep 2D pick + prompt for Z — possible but defeats osnap to a pipe-end face centre, which is the primary use case.
- Projected 2D fallback only (like pre-#372 CENTRE logic) — known to lose affordances under orbit (see #372).

**Naming:** the new function should be `SubmitInsertBlockPick3D(AppCommandState&, float wx, float wy, float wz, ...)` or widen the existing signature to `(float wx, float wy, float wz, ...)`. Keep one entry point so the headless `INSERT` / `PICK` transcript path and the UI click path agree (REQ-107/REQ-119 lesson: one renderer, one handler).

### D4 — Orientation: Z-only today, X/Y/Z needed for pipes

**Recommendation:** two increments:
- **4a (immediate):** keep `CadBlockXform.rotZ` for the Insert dialog's angle field and for backward compat, but add `rotX`/`rotY` fields already present in the struct (`src/util/cadblock.hpp:31`) and wire them through `InsertDialogXform` / `SubmitInsertBlockPick` 3D rotation pick. `CadBlockXformPoint` already applies `rotZ→rotY→rotX` in order (`src/util/cadblock.hpp:197-220`), so adding values there is sufficient.
- **4b (align-to-face):** an "Align to face" pick: pick a planar face of a target solid (via `solidpick`), take its outward normal (honouring `Surface::inward` as #156 does), and compose `ucs::FromNormal` into the insert's `rotX/rotY/rotZ`. This mirrors REQ-154's UCS Object alignment and reuses the same math.

**Rejected:** inventing a new Euler order — keep the existing Z→Y→X order already in `CadBlockXformPoint` to avoid breaking existing block refs.

### D5 — Units

**Recommendation:** do not trust `.sat` header `num_mm_units`. Keep the `CadBlockUnitsScale` path but surface it:
- In the Insert dialog, show **Block units** (default `unitless` from `def.units`, or the `.sat` file's stored units) and **Drawing units** (`drawingInsUnits` / REQ-022 `$INSUNITS`), with a computed scale factor and an explicit **Scale** override (reuse the existing Uniform Scale checkbox + `sx/sy/sz` fields). User confirms before commit.
- For `.sat` import, keep `scratch.drawingInsUnits = 0` (`CadBlocks.cpp:358`) and let INSERT's dialog be where the user chooses the scale, rather than guessing at import time.

**Rejected:** auto-scaling from header — measured wrong (25.4 for foot model) and would silently resize fittings.

### D6 — BEDIT wiring

**Recommendation:** minimal change:
- `LoadBlockPrimitivesIntoDrawing` gains `st.cadSolids = c.solids; st.cadSolidAttrs = c.solidAttrs;` (and same for meshes if desired — `CadBlockShiftContent` already skips them, so include them for consistency).
- `HarvestDrawingPrimitivesIntoContent` gains `c->solids = st.cadSolids; c->solidAttrs = st.cadSolidAttrs;`.
- `CadBlockShiftContent` gains loops for `solids` (apply `brep::Translate` per solid) and `meshes` (translate vertices). `CadBlockBakeBasePoint` then works for 3D fittings.

**Rejected:** separate 3D BEDIT mode — unnecessary; the existing isolated model-store swap (ADR-043) already isolates correctly.

### D7 — Connection points

**Recommendation:** add a lightweight struct on `CadBlockDefinition`:

```cpp
struct CadBlockConnection {
  std::string name;     // e.g. "P1", "FLANGE_A"
  float x=0, y=0, z=0;  // local point in block space
  float nx=0, ny=0, nz=1; // outward direction (unit)
  std::string nominalSize; // e.g. "4\"" — tag only, not parametric
};
std::vector<CadBlockConnection> CadBlockDefinition::connections;
std::vector<CadBlockConnection> CadBlockContent::connections; // if content-scoped; prefer definition-scoped
```

- Author in BEDIT via `BCONNECT` command: pick a face (sub-object), take its centre + normal as `pos`/`dir`, prompt for name/size.
- Store in `.gs` and DWG-trailer block format (same plumbing as `attrDefs`/`parameters`).
- Consume in INSERT: when a fitting is being placed, its connector set is shown; picking a connector on the target solid (or another block ref's world connector) snaps `insertBlockX/Y/Z` to the target connector and rotates the insert so its connector direction is anti-aligned (connector-to-connector). One ADR covers both authoring and consuming (issue comment: "the data model is shared").

**Rejected:** putting connectors on `CadBlockContent` only — they are definition metadata, not geometry; but either is acceptable if serialization already walks `content`.

## Recommended Approach

Phase by sub-issue, smallest shippable first. No new abstraction without two uses; no global state; no `gl*` outside Renderer.

**Foundation (no new REQ needed yet, just the epic):**
- File an ADR for INSERT's 3D semantics and the connection-point data model (extends ADR-043 + whatever ADR INSERT's 3D work produces). Amend REQ-320/REQ-107 or add a narrow REQ for "INSERT places a solid block" — the spec gap is real (CLAUDE.md §5): the issue explicitly asks whether a block-stored solid is instanced or materialised, which is a product decision.

**Increment 1 — 3D insert point (unblocks pipe modeling):**
- Widen `SubmitInsertBlockPick` to 3D (or add `SubmitInsertBlockPick3D`) and make INSERT's viewport pick route (`ViewportPickPolicy`, `CadUi.cpp:14374` click path, `HeadlessDriver.cpp:721`, `CadCommands.cpp:35118` typed path, `tests/CadBlockImportTests.cpp` call sites) supply `wz` from `solidpick` / `ray3d` / active UCS. Store in `insertBlockZ`.
- Hoist the typed `INSERT` point parse to accept `X,Y,Z` and `@dx,dy,dz` (reuse `ParseStoragePoint` 3D path from REQ-322).
- Tests: headless transcript `INSERT 3d-pick onto solid face centre → Z correct`; typed `INSERT 10,20,5` → `insertBlockZ == 5`.

**Increment 2 — Render block-ref solids:**
- Add `CadBlockCollectWorldSolids(defs, ref, out)` that recurses `CadBlockXform` + `brep::Transform` (translate/rotate/scale uniform). Renderer (`ViewportRenderer.cpp`, `render/`) draws them; `ViewportPickPolicy`/`CadSnap`/`PdfPlot`/`ExplodeRef` gain a solid branch. `ComputeWorldExtents` includes them so `ZOOMEXTENTS` frames correctly.
- Non-uniform `sx/sy/sz` for a solid ref: refuse/log per REQ-201 (or clamp to uniform with a message) — `brep::Scale` is uniform-only.
- Tests: `CadBlockTests` world-solid collect; render capture not pixel-identical (solid block vs exploded); headless `INSERT` then `EXPLODE` → `cadSolids` count.

**Increment 3 — 3D orientation:**
- Wire `rotX`/`rotY` through `InsertDialogXform` + dialog (`CadUi_InsertBlock.cpp`) + `CadBlockXformPoint` (already handles them) + typed `INSERT` rotation parse (or a separate `INSERT` orient flag). Add "Align to face" pick (sub-object face → `ucs::FromNormal`).
- Tests: `INSERT` with `rotX=90°` → `CadBlockCollectWorldSolids` bound check; align-to-face transcript.

**Increment 4 — BEDIT solid round-trip + 3D base point:**
- Patch `LoadBlockPrimitivesIntoDrawing` / `HarvestDrawingPrimitivesIntoContent` / `CadBlockShiftContent` / `CadBlockBakeBasePoint` as in D6.
- 3D base point pick in BEDIT (osnap to solid face/edge/vertex) for `def.baseZ`.
- Tests: `BEDIT → solid creation → BSAVE → close → INSERT → world solid position`; base-point rebake test.

**Increment 5 — Connection points:**
- Add `CadBlockConnection` to `CadBlockDefinition`, serialization (`GsIo`/`DwgIo`), `BCONNECT`/`BCONNECTEDIT` commands, INSERT connector snap (connector-to-connector point+direction).
- Tests: `BEDIT BCONNECT` → save/reload → `INSERT` connector snap → coincidence + anti-alignment within REQ-101.

**Increment 6 — Fitting library + units UX:**
- Insert dialog library pane: lists `resources/blocks/*.sat` + `*.dwg`/`*.dxf` block defs with previews (reuse `CadRubberPreview` ghost + `LoadBundledBlockLibraryImpl` filtering — today it excludes `.sat` from the sweep for a reason, so add an explicit opt-in for the fittings folder). Preview reuses `CalBlockCollectWorldSolids` ghost.
- Block-units selector in dialog as in D5.
- Ship a minimal `samples/fittings/` or `resources/blocks/fittings/` folder with a few hand-validated solids (flange, elbow, tee) — or document where to put user-supplied `.sat` files.

## Work Plan

| # | Title | Files / subsystems | Depends on | Est. risk |
|---|-------|--------------------|------------|-----------|
| 1 | **3D INSERT point** — widen `SubmitInsertBlockPick` to 3D, wire `solidpick`+`ray3d`+UCS fallback, typed `X,Y,Z` | `src/commands/CadBlocks.*`, `src/commands/CadCommands.*`, `src/viewport/ViewportPickPolicy.*`, `src/viewport/CadSnap.*`, `src/ui/CadUi.*`, `src/headless/HeadlessDriver.cpp`, `tests/CadBlockImportTests.cpp` | REQ-318 | Low–Med |
| 2 | **Render block-ref solids** — `CadBlockCollectWorldSolids` + renderer hook + explode/framing/snap | `src/util/cadblock.hpp`, `src/render/ViewportRenderer.cpp`, `src/viewport/CadRubberPreview.cpp`, `src/viewport/CadSnap.cpp`, `src/io/PdfPlot.cpp`, `src/io/GsIo.*`, `src/commands/CadBlocks.cpp:ExplodeRef` | 1 (for Z-correct placement) | Med |
| 3 | **3D orientation** — `rotX/rotY` wiring + align-to-face | `src/util/cadblock.hpp:CadBlockXformPoint`, `src/commands/CadBlocks.cpp:InsertDialogXform`, `src/ui/CadUi_InsertBlock.cpp`, `src/brep/*` | 2 | Med |
| 4 | **BEDIT solid round-trip + 3D base point** | `src/commands/CadBlocks.cpp:LoadBlockPrimitives*`, `Harvest*`, `src/util/cadblock.hpp:CadBlockShiftContent` | 2 (for testing round-trip) | Low |
| 5 | **Connection points** — data model + `BCONNECT` + INSERT snap | `src/util/cadblock.hpp:CadBlockDefinition`, `src/io/GsIo.*`, `src/io/DxfIo.*`/`DwgIo.*` if persisted there, `src/commands/CadBlocks.cpp`, `src/viewport/CadSnap.cpp` | 4 + 2 + 3 | Med–High |
| 6 | **Fitting library + units UX** | `src/commands/CadBlocks.cpp:LoadBundledBlockLibraryImpl`, `src/ui/CadUi_InsertBlock.cpp`, `resources/blocks/`, `samples/` | 5 (connectors) or can ship after 2 | Low |

Each increment is its own branch `feat/issue475-<slice>` → PR → `beta` (per `CLAUDE.md` git workflow). No direct-to-`master`.

## Validation Plan

- **Existing suite stays green:** `ctest` via `./dev/test` (or `ctest` from the Windows build) — 400+ tests; `CadBlockTests`, `CadBlockImportTests`, `req322-move-3d` are the most sensitive.
- **Per-increment headless transcripts** (like `tests/headless/transcripts/regression-*.txt` and `headless.issue473-sat-blockimport`):
  - 1: `INSERT` 3D pick at `(10,20,5)` → `insertBlockZ==5`, solid world bounds Z correct; typed `@dx,dy,dz` variant.
  - 2: block with one solid → `INSERT` → `CadBlockCollectWorldSolids` returns 1 world solid with correct transform; renderer draws it (manual check + `benchscene` if needed); `EXPLODE` → `cadSolids` +1.
  - 3: `rotX=90` insert → world solid bounds rotated; align-to-face → direction anti-aligned within REQ-101.
  - 4: `BEDIT` new def → `BOX` → `BSAVE` → `INSERT` → world solid present and `WBLOCK` round-trips.
  - 5: two fittings each with one connector → `INSERT` connector snap → connector points coincident within REQ-101 (±0.002 ft after D-2026-09-08-i), directions anti-aligned.
- **Manual checks:** orbit + insert on a tilted solid face (the #372 lesson: test under orbit, not just plan view); Insert dialog preview matches commit within REQ-101; non-uniform scale attempt → refused/logged per REQ-201.

**Highest-risk validation:** increment 2's `CadBlockCollectWorldSolids` — a wrong `CadBlockXformPoint` order or a missed `brep::Transform` field silently misplaces the solid while still validating. The defence is geometry-asserting tests (bounds/face centres), not just `Validate` pass.

## Risks / Rollback

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| `SubmitInsertBlockPick` 3D breaks existing 2D INSERT transcripts/tests | High | Keep 2D path as `wz=0` default; update `CadBlockImportTests.cpp:369-533` + `HeadlessDriver` in same PR |
| `CadBlockXformPoint` Euler order confusion (Z→Y→X) | Med | Keep existing order, add unit tests for composed `rotX/Y/Z` + translation |
| Non-uniform scale silently ignored vs refused — user confusion | Med | Refuse by name (`INSERT — non-uniform scale not supported for solid blocks`) per REQ-201 |
| `.sat` units guessing reintroduced | Low | D5: explicit chooser, no silent header scaling |
| `BEDIT` solid clear-on-enter already clears `cadSolids` — existing drawings with loose solids lose them on accidental BEDIT | Med | Gate `LoadBlockPrimitivesIntoDrawing` solid-load on the def's content only; model solids stay cleared per ADR-043 isolation, but warn if `st.cadSolids` not empty on enter (opt.) |
| Connector direction ambiguity (inward vs outward) | Low | Follow #156 rule: honour `Surface::inward` via `brep::` normal; store explicit `nx/ny/nz` so import doesn't need to recompute |

**Rollback:** each increment is isolated to its files; revert the branch. No data-format change that isn't behind a new `connections` field (additive, empty on old files) and no `.gs` version bump until connectors ship — so rollback leaves old files readable.

## Open Questions

1. **Materialise vs linked reference — final call.** Issue frames it as an ADR-level choice; this plan recommends linked (render transformed) but the decider should record it explicitly before increment 2 starts. (Blocked on: REQ/ADR decision.)
2. **Connection point storage location:** `CadBlockDefinition::connections` vs `CadBlockContent::connections`? Definition-level is cleaner (metadata, not geometry), but `content` already serializes geometry — check `GsIo`/`DwgIo` block serialization to pick the path with fewer plumbing changes.
3. **Units for user-supplied `.sat` fittings:** should `resources/blocks/fittings/` carry a per-file `.units` sidecar, or rely solely on the dialog override? Proposal: dialog override only for now; sidecar only if users report repeated per-file pain.
4. **Reuse vs new REQ number.** REQ-320 already covers ACIS solid blocks; does INSERT-3D amend REQ-107/REQ-320 or take a new REQ (e.g. REQ-33x)? Proposal: new REQ for "INSERT a solid block" and a narrow amendment to REQ-107 for the render/snap half.
5. **Brownfield vs greenfield fitting source:** will the first fittings come from hand-modeled GoSurvey solids (`BEDIT` path) or from more Civil 3D `ACISOUT` `.sat` exports? Answer determines whether to prioritize BEDIT solid round-trip (4) before library browsing (6).

---
*Sources:* issue #475 body + comment via `curl https://api.github.com/repos/chetjones003/GoSurvey/issues/475` (2026-09-11); `src/util/cadblock.hpp` (CadBlock structs, CadBlockXformPoint, CadBlockShiftContent); `src/commands/CadBlocks.cpp` (SubmitInsertBlockPick, InsertDialogXform, LoadBlockPrimitivesIntoDrawing, ImportSatFileToScratch); `src/commands/CadBlocks.hpp`; `spec/requirements.md` (REQ-107/REQ-320/REQ-322); `spec/architecture.md` (layering/ownership); `spec/project.md` decision log (D-2026-09-10-c).

