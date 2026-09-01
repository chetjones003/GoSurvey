# TASK-167 — Solids in the drawing: entity, commands, persistence, render, snap (issue #146, increment 2 of 2)

## Requirement authority

- **REQ-313** — The B-rep solid kernel and the seven primitive solids (accepted 2026-09-01).
- **ADR-045** and its **addendum** — the kernel, and the document-facing half decided here.
- **D-2026-09-01-b** — the recorded decision behind both.
- **REQ-100** amended: a fourth cost profile, (d) B-rep solids.
- Constraints in force: REQ-064 (visual styles), REQ-084 (d) (isolation), REQ-101 (±0.01 ft),
  REQ-201 (no silent failures), REQ-204 (document invariants), REQ-300, REQ-301, REQ-311/312.
- GitHub issue #146, Phase 3 of #120. Increment 1 is TASK-166.

## What this increment is

Increment 1 delivered a kernel nothing could reach. This is the half a user can reach: create a
solid, see it, select it, erase it, save it, reopen it, snap to it.

## Files affected

New:
- `src/util/cadsolid.hpp` — the store's type, the tessellation cache entry, the display batches.
- `tests/headless/transcripts/req313-solid-primitives.txt` — 129 steps.

Modified:
- `src/util/brep.{hpp,cpp}` — `Tessellation::triFace`, `ClosestPointOnSurface`,
  `ClosestPointOnEdge`, `TessellateEdges`. All four exist because snapping and the wireframe need
  them and the kernel is where the chord rule and the parametrisation are written down.
- `src/commands/CadCommands.{hpp,cpp}` — the three stores, `EntityKind::Solid`,
  `SelectedEntity::Type::Solid`, the seven commands, `SOLIDLIST`, `SolidVisible`,
  `RefreshSolidDisplayGeometry`, extents, the pick funnel, box selection, erase, transform refusals.
- `src/commands/CadCommands_Bench.cpp` — REQ-100 profile (d).
- `src/commands/CadBlocks.cpp` — clear solids when the block editor isolates the model.
- `src/io/GsIo.cpp` — the `solids` section and the `objectSnapSolid` setting.
- `src/io/DxfIo.cpp`, `src/io/LibreDwgCad.cpp` — the exclusion messages.
- `src/io/UserPrefs.cpp` — the snap preference.
- `src/render/ViewportRenderer.{hpp,cpp}` — one new parameter, the three-style draw, two glyphs.
- `src/app/main.cpp` — the per-frame refresh and the render call.
- `src/viewport/CadSnap.{hpp,cpp}` — `Kind::Edge`, `Kind::Face`, the candidate walk, two helpers.
- `src/ui/CadUi.cpp`, `src/ui/CadUiSettings.cpp` — the toggle, the labels, the override menu.
- `src/util/docinvariants.cpp` — the parallel-array and selection-index checks (REQ-204).
- `tests/BrepTests.cpp`, `tests/headless/HeadlessDriver.cpp`.

SPEC: `spec/requirements.md` (REQ-313 increment-2 acceptance + traceability row; REQ-100 profile
(d)), `spec/architecture.md` (ADR-045 addendum).

## The decisions inside the increment

Each is argued in the ADR-045 addendum; the short forms:

- **The store is `double`, not `float`.** The one exception to §11.8, and narrow: that convention is
  for arrays with millions of entries headed for a vertex buffer. Narrowing a handful of B-rep
  vertices would discard the exactness the closed-form volume rests on and buy nothing.
- **One typed line per primitive, orientation from the active UCS.** Exactly what REQ-313's
  acceptance asks for. No interactive placement — that needs a 3D draft preview and is #120 Phase 5.
- **Every style means something.** Hidden writes faces depth-only and then the edges, so it is real
  hidden-line removal rather than wireframe with extra steps.
- **The cache key is `(solid, tolerance)`.** A solid is immutable, so an unchanged pointer means
  unchanged geometry. That is #120's "do not regenerate a solid's render mesh every frame" as a
  property of the code.
- **`.gs` stores the topology, not the recipe** — a Phase 4 boolean has no recipe and must save.
- **One snap preference, not two.** Edge and Face are the two halves of "snap to a solid".
- **Transforms refuse a solid by name.** The Surface rule; silently leaving it behind is the
  outcome REQ-201 exists to prevent.

## Test approach

`headless.req313-solid-primitives` (129 steps) covers what the kernel's own suite cannot: that a
typed command produces the shape asked for, that it reaches the renderer, that it survives a round
trip, and that every way of getting it wrong is refused. `EXPECT SOLIDPROPS` asserts volume, area
and the topology counts **together**, deliberately — they are one claim, and split apart a transcript
could assert the volume of a solid whose topology had changed underneath it.

`BrepTests` grows four cases for the new kernel entry points, including a face-id check that every
triangle's vertices lie on the surface its face claims — the property the face snap depends on.

## Verification

- Build: clean, no new warnings.
- `ctest`: **963/963 green.** No pre-existing test changed.
- REQ-204: `docinvariants` gained the solids parallel-array check and the selection-index case, so a
  desynced attribute array is caught by the fuzz harness like every other kind.

### Negative tests

1. **Dropping `faces` from the `.gs` write** → the round-trip section goes red (`SOLIDS: expected 7,
   got 0`): the reload refuses a solid whose topology will not validate, which is the REQ-201
   behaviour, and the transcript proves the persistence is actually exercised.
2. **Making `SolidVisible` return true unconditionally** → the layer section goes red
   (`SOLIDBATCHES: expected 1, got 2`).
3. **Mislabelling every triangle's face id** → the face-id case goes red by 12.0 feet, which is the
   distance a face snap would have been wrong by.

### What the tests found while being written

The visibility test failed first on something else entirely: `EXPECT SELECTED 2` returned 0, because
**solids were missing from the box-selection walk**. The click funnel had them; the window/crossing
walk did not, so a window drag around a solid selected nothing. Added, with the analytic bounds
rather than the stored vertices — a sphere has two.

## Assumptions

- **ASSUMPTION-1 (validated):** a tessellated ray hit, projected onto the analytic surface, lands on
  the surface. Pinned by the face-id test, which checks every tessellation vertex against the
  surface its face claims.
- **ASSUMPTION-2 (stated, not validated here):** the solid draw path fits REQ-100's budget. The
  `BENCH SOLID` instrument exists to answer it; the number needs the GUI on the reference machine.

## Technical debt / stated boundaries

- **DEBT-1 — REQ-100 profile (d) is unmeasured.** The instrument is delivered; the figure is not.
  Recorded in REQ-100's own status, not only here.
- **DEBT-2 — the three visual styles are verified as batches, not as pixels.** That the right
  geometry reaches the renderer is asserted; that Hidden actually hides and Shaded actually shades
  is GUI verification, the same category REQ-064 established for its own styles.
- **DEBT-3 — no interactive placement, no 3D grips, no transforming a solid.** #120 Phase 5. Every
  transform command refuses a solid by name in the meantime.
- **DEBT-4 — solids are not captured into block definitions**, matching surfaces, tables and feature
  lines. They ARE cleared on block-editor entry, so nothing leaks in.
- Increment 1's DEBT-1..3 (self-intersection, centroid/moments, convex-fan triangulation) stand
  unchanged.

## Status

Both increments complete and verified. **Issue #146's ten acceptance bullets are all addressed**,
with profile (d)'s measurement and the pixel-level style verification named above as the two things a
reviewer has to confirm rather than take on trust.

Per the project convention this goes to review, not done — and the issue is not closed here.

## Review pass (second reading, as someone else's PR)

Five findings, all fixed here rather than left:

1. **`RefreshSolidDisplayGeometry` could mint an entity attribute.** A short attribute array fell
   through to `emplace_back()`, which grows a vector mid-walk (invalidating the loop's own
   references) and creates an attribute — with a fresh id — from a *display refresh*, which is the
   last place that should be doing it. Now a shared default; `EnsureAttrCounts` owns the repair.
2. **`EnsureAttrCounts` did not repair the solid attributes at all**, so the parallel arrays could
   desync and REQ-204 would fire. Added beside the surface and table cases.
3. **Face snapping ray-tested every triangle of every solid, every hover frame.** A few hundred
   solids at a couple of thousand triangles each is most of a million ray-triangle tests per frame —
   REQ-100's budget spent on a cursor near none of them. A padded ray/AABB slab reject now discards a
   whole solid first, the same call the surface pick already makes for its own plan-AABB reject.
4. **A dead `wantSolid` local** with a `(void)` to silence it. Removed.
5. **Edge snapping in a plan view** used the cursor at the datum as its probe with no comment. The
   answer is still always ON the edge and is ranked by plan XY distance like every other kind, but on
   a slanted edge it can favour the lower end — now written down rather than left to be rediscovered.

And the coverage gap the review found: **face and edge snapping had no test at all**, which is
issue #146's acceptance bullet 8. `CadSnapTests [CadSnap][req313]` now drives it through the real
`FindBest` with a pick ray. The face case is the one worth reading: it asserts the returned point is
at exactly radius 10 from the cylinder's axis, and removing the analytic projection returns
**9.9928632694** — the chord's sagitta, precisely. Rejecting every solid from the bounds test turns
the same case red the other way, so the optimisation added in (3) is itself pinned.
