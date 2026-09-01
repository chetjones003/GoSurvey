# TASK-169 — Coalesce the solid render path so REQ-100 profile (d) can pass

- Type:    perf
- Status:  in progress
- Opened:  2026-09-01
- Owner:   chetjones003
- Follows: GitHub issue #194 (a REQ-313 / issue #120 Phase 3 follow-up, filed from the PR #193
  pre-merge review). Branch `perf/issue194-batch-solid-draw-calls` from `beta`; lands via its own
  PR to `beta`.

## 1. Authority

- Goal:         GOAL-01 (a CAD/survey editor whose geometry is correct and responsive)
- Requirements: REQ-100 (frame budget — profile (d), B-rep solids), REQ-313 (the seven primitive
  solids), REQ-064 (solids draw in every style), REQ-101 (tolerance — unchanged).
- Constraints:  CON-07 (Windows/MSVC/Ninja build authoritative).
- Decision:     **D-2026-09-01-d** (recorded in `spec/project.md` before implementation). Coalesce
  by resolved appearance, following the `CadSurfaceDisplayGeometry` precedent; keep the
  `(weak_ptr solid, chordTolerance)` staleness key and the immutable-solid early-out.
- Acceptance (from the issue):
  - `BENCH SOLID` at the reference density passes REQ-100's p95 budget on the reference machine, in
    Shaded, under continuous orbit. *(The structural per-solid cost is removed; the p95 number
    itself still needs a GUI session on the reference machine — REQ-100 status.)*
  - The solid `BENCH` report states whether the tessellation cache held during the timed frames
    (expected: 0 regenerations).
  - No change to solid geometry, topology, mass properties, `.gs` output, snapping, or selection.
  - `headless.req313-solid-primitives` and the brep/snap suites stay green; a batch-assembly test
    covers the coalescing (same layer/colour/lineweight merges; a colour or layer difference splits).

## 2. Problem

`RefreshSolidDisplayGeometry` (`src/commands/CadCommands.cpp`) built one `CadSolidDisplayBatch` per
visible solid, each borrowing that solid's own tessellation-cache buffers. `ViewportRenderer::
RenderScene`'s solid block then did, per batch: a CPU vertex transform, one `glBufferData`
(`GL_STREAM_DRAW`) and one `glDrawArrays` for the faces, and the same again for the edges. That is
~40 µs of fixed cost per solid, linear in object count — `BENCH SOLID` measured p95 ~5 ms at 100
solids, 17–20 ms at 400 (the reference density), 35 ms at 800, against the 16 ms budget, while a
single imported mesh of the same 976 K-triangle total passes profile (b). The tessellation cache
itself held; the draw submission was the bottleneck, exactly as issue #194 predicted.

There was also no `solidDisplayRegenCount`, so on a fast machine `BENCH SOLID`'s p95 could not
distinguish a held cache from one silently rebuilding — the gap ADR-036 (e) closed for surfaces.

## 3. Files affected

- `src/util/cadsolid.hpp` — `CadSolidDisplayBatch` now OWNS `triVerts` / `triNormals` / `edgeVerts`
  (`std::vector<float>` members) instead of borrowing cache pointers; doc rewritten.
- `src/commands/CadCommands.hpp` — `AppCommandState` gains `solidDisplayAssemblySig` (the assembly
  early-out fingerprint) and `solidDisplayRegenCount` (real retessellations, twin of
  `surfaceDisplayRegenCount`).
- `src/commands/CadCommands.cpp` — `RefreshSolidDisplayGeometry`: bump `solidDisplayRegenCount` on a
  real (re)tessellation; the assembly pass now builds a signature over its inputs, early-outs when
  unchanged, and otherwise merges visible solids sharing a quantised resolved colour + lineweight
  into shared vertex buffers.
- `src/render/ViewportRenderer.cpp` — the solid face and edge loops read the owned vectors (`b.triVerts`
  … ) instead of the borrowed pointers; the `nullptr` guards drop.
- `src/app/main.cpp` — the `BENCH` regen baseline sums `surfaceDisplayRegenCount +
  solidDisplayRegenCount` (only one profile runs at a time, so the delta is the active cache).
- `src/commands/CadCommands_Bench.cpp` — `FinishFrameBudgetBench` reports HELD / NOT HELD for the
  solid profile, on the console and in `bench-req100.txt`.
- `tests/headless/HeadlessDriver.cpp` — new `EXPECT SOLIDVISIBLE` and `EXPECT SOLIDTESSGEN`
  quantities; `SOLIDBATCHES` now means "coalesced draw calls"; new `LAYERSTATE <name> COLOR <name>`
  verb so a transcript can give two layers different resolved colours.
- `tests/headless/transcripts/req313-solid-primitives.txt` — the "solids are drawn" and post-round-trip
  assertions become `SOLIDVISIBLE n` + `SOLIDBATCHES 1`; the layer-visibility section is rewritten to
  assert the coalescing merge (two boxes, one batch), the split (a third solid on a red layer, two
  batches), the visibility progression via `SOLIDVISIBLE`, and `SOLIDTESSGEN` holding.
- `spec/project.md` — D-2026-09-01-d.
- `spec/requirements.md` — REQ-100 status (profile (d)).

## 4. Approach

The assembly pass copies vertices now (the surface path can borrow because a surface component is
already one buffer; a solid scene is hundreds), so it is gated. A per-frame FNV-1a signature folds
in, for each visible solid in order: the cache buffer `.data()` pointer and sizes, the four resolved
colour floats, the lineweight, plus the global `solidDisplayRegenCount`. Unchanged signature ⇒ the
merged buffers in `solidDisplayGeometry` are still current ⇒ return before the concatenation. An
orbit changes only the camera, so during the timed `BENCH SOLID` frames the merge runs once and the
renderer submits a handful of draw calls per frame.

Coalescing key: resolved RGBA and lineweight, each quantised (`lround(v * 4096)`), linear-searched
against the batches built so far (a real drawing has a few distinct appearances, not hundreds).

## 5. Test approach

- `headless.req313-solid-primitives`: seven same-appearance primitives ⇒ `SOLIDVISIBLE 7` /
  `SOLIDBATCHES 1`, surviving a `.gs` round trip. New section: two boxes on one layer ⇒
  `SOLIDBATCHES 1`; a sphere on a second layer set to `COLOR Red` ⇒ `SOLIDBATCHES 2`; layer
  off / freeze walks `SOLIDVISIBLE` 3→1→0 and `SOLIDBATCHES` 2→1→0; `SOLIDTESSGEN 3` twice in a row
  proves the cache is not retessellating behind the visibility changes.
- Full ctest suite (965 tests) green.
- `BENCH SOLID` p95 on the reference machine: pending a GUI session (REQ-100 status); the per-object
  cost that made the failure structural is removed and the report now states cache-held.

## 6. Architectural-boundary check

No new abstraction, command, dependency, or `.gs` format change. `CadSolidDisplayBatch` changes
ownership model (borrowed → owned) — recorded in D-2026-09-01-d because it is a shared data
structure and because `SOLIDBATCHES`'s meaning in the verification driver changed. The staleness key
and immutable-solid early-out the issue said to keep are unchanged. Snapping and selection read
`solidDisplayCache` directly, not the batches, so they are untouched.
