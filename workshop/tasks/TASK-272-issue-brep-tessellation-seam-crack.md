# TASK-272 — B-rep tessellation seam crack: torn bolt holes / curved-face joins

- Type:    bug (follow-up, partially fixed)
- Status:  open
- Opened:  2026-09-22
- Owner:   Claude (chetjones003@gmail.com)

## 1. Authority
- Goal:         REQ-313 / ADR-045 (B-rep solid kernel) — tessellation must be a faithful,
  crack-free derived representation of the analytic solid.
- Requirements: REQ-313 (Tessellation acceptance: "every triangle's winding agrees with its
  stored analytic normal"; implicitly, adjacent faces sharing a boundary must sample it
  consistently — no acceptance criterion states this explicitly, which is itself worth closing
  as a SPEC GAP alongside the fix).
- Constraints:  smallest correct fix; must not regress `BrepTests.cpp` (1210 cases, currently
  1208 passing).
- Owning subsystem: Domain (`src/util/brep.cpp`, `Tessellate`).

## 2. Bug report

**Title:** Curved solid faces render with jagged/torn edges and inconsistent shading where a
curved wall meets a flat face (bolt holes, bored holes, any hole through a flat plate).

**Observed:** In the GUI, a flange's bolt holes and a plain extruded/press-pulled circle both
show ragged, torn-looking rims instead of clean circles, with shading artifacts at the seam
(screenshot: "2in Flange" model, GitHub issue TBD — filed from a user report, no issue number
assigned yet).

**Root cause (confirmed with evidence — see `RequireMeshWatertight` in `tests/BrepTests.cpp`):**
Two independent bugs, both in `brep::Tessellate` (`src/util/brep.cpp`):

1. **Fixed.** The `f.loops.size() == 2` annular-face tessellator (a hole in an otherwise-simple
   flat face) built the hole's rim by ray-casting NEW points at angles merged in from the outer
   loop, instead of reusing the loop's own sampled edge points. Those interpolated points never
   matched the neighboring curved wall's own vertices. **Fix:** route `loops.size() > 1` (was
   `> 2`) through the existing `TessellateGeneralLoopFace` general even-odd tessellator, which
   already samples every loop by walking its own edges directly
   (`SegmentsForEdge`/`EdgePointAt`) — no interpolation. This also fixed several *pre-existing*,
   previously-undetected winding/inverted-normal bugs in the old annular path (see commit).

2. **Partially fixed.** `SegmentsForArc`'s segment-count floor (`kMinFullCircleSegments = 128`)
   was applied per edge/face span via a step function (`span >= ~half-turn → full floor`). A
   circle that happens to be split into pieces (a primitive's wall is always built as two
   half-turn faces; a Phase 4 boolean can leave the same circle as one full 2π edge) got the
   SAME absolute floor per piece, so a split representation got 2x the total vertex budget of an
   unsplit one for the identical circle — and the two disagreed in COUNT wherever they met.
   **Fix:** scale the floor by span (`kMinFullCircleSegments * span / kTwoPi`), keeping the
   total per-turn budget constant regardless of how many pieces a circle is cut into. This
   roughly halved the crack count on the reproduction case (536 → 280 of ~1328 mesh edges).

**Residual bug (NOT fixed, open):** Even with segment COUNTS matching, the wall's per-vertex
angle comes from the surface's own `f.uStart..f.uEnd` parametrization, while a neighboring flat
face's hole-loop boundary comes from walking the shared boundary EDGE's own parametrization
(`EdgePointAt`). These are two independent phase references. Confirmed via a topology dump
(`brepdebug`-tagged scratch test, since removed) on `BooleanSubtract(box, throughCylinder)`: the
wall face's `u = 0` and its own bounding rim edge's `t = 0` sample points ~π apart in angle —
a genuine phase offset, not just a count mismatch. Two attempts at a direct fix (deriving the
wall's angle sequence from its own rim edge, with and without an empirical winding-direction
correction) both caused new regressions elsewhere (wrong tessellated volumes, inverted normals
on previously-correct cylinder/cone/sweep cases) because correctly restricting the fix to ONLY
genuinely-unclipped, non-partial, full-rim faces is more subtle than it looks — partial-turn
walls (a milled notch, a sliced cylinder) share the same code path and were incorrectly caught
by both attempts. Both were reverted; see git history around this task's commit for the exact
(reverted) diffs if a future attempt wants a starting point.

## 3. Reproduction (regression test, already in tree and RED)

`tests/BrepTests.cpp`: `RequireMeshWatertight(t)` (new helper) checks every triangle edge in a
tessellation is shared by exactly two triangles. Two call sites currently fail:

- `"Tessellation agrees with the analytic figures and winds outward"` / case "cone": 128 of 384
  mesh edges cracked (a plain cone, apex fan vs. wall seam — likely the SAME root cause,
  unexplored).
- `"Curved B1: ... SUBTRACT drills a round hole through the box (B2a)"`: 280 of 1328 mesh edges
  cracked (the flange/bolt-hole repro).

Run: `.\build\GoSurveyTests.exe "[brep]"` after `./dev/build`.

## 4. Next steps for whoever picks this up

- The wall vertex generation (`SurfaceKind::Cylinder`/`Cone` case in `Tessellate`) needs to
  derive its angle sequence from geometry that is GUARANTEED to match a neighboring loop's own
  edge sampling, but ONLY for a true full-rim, unclipped wall — the existing `isect`/`coneCut`/
  `cut` guards were not sufficient (a milled notch/slice is none of those but is still a
  partial-turn face using the same code path). Needs a positive test — e.g. "this face's loop is
  exactly bottom-rim-arc + seam + top-rim-arc + seam, and both arcs sweep the full declared
  `f.uEnd - f.uStart`" — rather than a list of exclusions.
- Consider instead asking whether the BOOLEAN construction (not the tessellator) should be
  keeping the wall's `uStart`/rim-edge phase in sync in the first place — a kernel-level fix
  might be more robust than patching the tessellator around it.
- The cone-apex crack is a separate, unexplored case; confirm whether it's the same class of bug
  before assuming the same fix applies.
- This is a SPEC GAP candidate: REQ-313's Tessellation acceptance criteria don't explicitly
  require adjacent-face seam agreement (only per-triangle winding/normal agreement and
  volume/area convergence). Worth a recorded decision to add an explicit watertightness
  acceptance criterion, with `RequireMeshWatertight` promoted from a debugging aid to a
  documented requirement check.
