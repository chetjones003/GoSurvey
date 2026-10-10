# TASK-227 — REQ-103 step 8: EXPLODE decomposes a polyline into its segments

- Type:    fix (fills a deferred acceptance / SPEC GAP)
- Status:  done — PR against issue #390
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #390

## 1. Authority

- REQ-103 step 8 (EXPLODE) — acceptance criteria written 2026-09-08, D-2026-09-08-h.
- REQ-057 (Z is in the data, must be preserved).
- REQ-316 / ADR-047 (per-vertex bulge — a curved segment stays a real arc).
- REQ-325 / ADR-053 (a bulge segment's plane; the ad-hoc-frame → canonical-frame arc build reused
  here from `DxfIo` split-on-export and the sweep/FILLET paths).
- REQ-201 (no silent drop — unsupported selected kinds are reported).
- REQ-076 / ADR-027 (exploded entities get fresh stable ids).

## 2. Problem

`CadBlocksTryIdleCommand`'s `explode` handler built its work list by filtering `st.selection` to
`SelectedEntity::Type::BlockRef` only. Every other selected kind — a Polyline included — was
dropped before anything ran, so `EXPLODE` on a polyline did nothing. Found while investigating
issue #373 / REQ-325.

## 3. Scope

IN:
- `ExplodeSelectedPolylines(AppCommandState&, log)` in `CadCommands.cpp` (non-static, declared in
  `CadCommands.hpp`): for each selected Polyline, emit one `userLinesFlat` LINE per straight
  segment and one `userArcs` ARC per bulge segment (flat via `BulgeArc`; tilted via the REQ-325
  ad-hoc-frame construction), each carrying a `DuplicatedEntityAttrs` copy of the polyline's
  attributes (fresh id). Closed polylines also emit the closing segment. Source polylines removed
  via `ErasePolylineByIndex` (high index first). Non-polyline, non-block selected kinds reported
  by name (REQ-201). Model space only (paper store is a separate `PaperLayout`).
- `explode` handler in `CadBlocks.cpp`: after the unchanged block-ref loop, call the new helper,
  then one `st.selection.clear()` / `EnsureEntityIds` / `BumpCadGpuCache`, and a combined
  "N block reference(s), M polyline(s)." message (or "nothing to explode").
- Headless transcript `tests/headless/transcripts/issue390-explode-polyline.txt`.
- Spec: REQ-103 EXPLODE acceptance; D-2026-09-08-h in the decision log; REQ-325 note updated.

OUT (each its own future increment):
- Exploding a Dimension, MTEXT, Hatch boundary, Mesh, Table, or Solid.
- ARRAY (REQ-103 step 7).
- Paper-space polyline explode.

## 4. Test approach

Headless transcript, 5 sections:
1. Closed rectangle (`RECT`) → 4 LINEs, 0 polylines; one UNDO restores it.
2. Open 3DPOLY with per-vertex Z → 2 LINEs, `EXPECT LINEXYZ` proves each keeps its own Z.
3. straight + 90° bulge segment → 1 LINE + 1 ARC (`EXPECT ARCPOINTS` on the segment endpoints);
   DWG round-trip `SAMEFILE`.
4. polyline + circle selected → circle reported "not decomposable", polyline still explodes.
5. only a circle selected → "nothing to explode", drawing unchanged.

## 5. Verification

- `./dev/build` — clean.
- `./dev/test` — full ctest suite 100% (1358 pre-existing + `headless.issue390-explode-polyline`).
- Acceptance criteria (REQ-103 EXPLODE): all met — see PR description.

## 6. Notes / tech debt

- The tilted-bulge → `CadArc` construction is now a 4th copy of the same ~30-line ad-hoc-frame →
  canonical-frame math (`DxfIo` `buildTiltedSegmentArc`, `CadCommands.cpp` sweep builder, the
  render/pick paths). A shared `geom2d`/`ucs` helper would be the right consolidation but is an
  architecture change touching several TUs; deferred, matching how the codebase currently keeps
  per-site copies.
