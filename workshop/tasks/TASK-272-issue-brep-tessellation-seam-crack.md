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

**Wrong lead, corrected:** an earlier version of this task theorized the residual crack was a
wall-vs-cap phase mismatch (the cylindrical wall's `f.uStart` parametrization disagreeing with
the flat cap's hole-loop edge parametrization) and reports two reverted fix attempts on that
theory. **That diagnosis was wrong.** A `triFace`-tagged crack-location dump (temporary scratch
test, since removed) on the same `BooleanSubtract(box, throughCylinder)` repro showed every
cracked mesh edge touched only ONE triangle, and that triangle's `triFace` was always the SAME
flat cap face (e.g. face 0, the bottom plate) — **the crack is entirely INSIDE one face's own
triangulation**, not between two faces. The wall is not involved.

**Root cause 3 (partially fixed — see below):** the culprit is
`TessellateGeneralLoopFace`, the general even-odd polygon-with-holes tessellator every
`loops.size() > 1` planar face now routes through (root cause 1's fix). It decomposes the face
into horizontal "bands" between consecutive hole/outer-loop vertex v-coordinates
(`vBreaks`), and grids each band independently with its own column count
(`nCols = max(that band's own bottom-row need, that band's own top-row need)`, each computed
from `GeneralLoopChordSegments`). Nothing ties one band's column count to its neighbor's, even
though two adjacent bands SHARE one horizontal row (band *i*'s top = band *i+1*'s bottom) — so
that shared row can get a different column count on each side, a T-junction crack inside the
same face.

*Fix applied (real, but insufficient alone):* a two-pass resolution — gather every band's own
column-count need first, then two relaxation sweeps (forward, then backward) propagate the max
column count through every run of bands whose interval topology matches its neighbor (same
number of inside/outside crossings), so a whole connected run converges on one shared count. This
can only ever raise resolution, never lower it, and is confirmed safe (full suite still
1208/1210, only the two known `RequireMeshWatertight` failures, no new regressions). **It does
not fix the reproduction case's crack count (536 of 2608 edges, unchanged before/after)** — see
why below.

**Residual bug (NOT fixed, open) — the real one:** the crack in the repro is concentrated at the
hole's top/bottom **extrema** (e.g. the circle's bottommost point, y = -radius). At exactly that
v-coordinate the hole interval pinches to a single point: the band just below it has ONE interval
(no hole yet — `xs.size() == 2`), the band just above has TWO (`xs.size() == 4`, the hole has
opened up). This is a genuine, unavoidable topology CHANGE between neighboring bands, so the
fix above correctly (and necessarily) skips propagating resolution across it — sharing a column
count between two bands that don't structurally correspond would be wrong, not a fix. Whatever
`TessellateGeneralLoopFace` does at this pinch point itself is where the actual crack lives;
that code path is unexplored. Likely candidates: the pinched band's own degenerate-interval
handling (`u0R <= u0L && u1R <= u1L` skip, or a band whose `xs0.size() != xs1.size()` is silently
dropped entirely via the `continue` a few lines up — dropping a band drops geometry, which would
show up as a HOLE in the mesh, not a crack, so more likely it's the row directly adjacent to the
pinch that under- or over-resolves relative to the extremum point itself).

## 3. Reproduction (regression test, already in tree and RED)

`tests/BrepTests.cpp`: `RequireMeshWatertight(t)` (new helper) checks every triangle edge in a
tessellation is shared by exactly two triangles. Two call sites currently fail:

- `"Tessellation agrees with the analytic figures and winds outward"` / case "cone": 256 of 768
  mesh edges cracked (a plain cone — unexplored; may or may not be the same pinch-point class of
  bug, since a cone's apex is a comparable "everything meets at one point" case).
- `"Curved B1: ... SUBTRACT drills a round hole through the box (B2a)"`: 536 of 2608 mesh edges
  cracked (the flange/bolt-hole repro; all inside the flat cap faces, at the hole's top/bottom
  extrema — see above).

Run: `.\build\GoSurveyTests.exe "[brep]"` after `./dev/build`. A `triFace`-based crack-location
dump (print, for each cracked edge, `t.triFace[i/3]` of its one touching triangle, and its `y`
coordinate) is the fastest way to re-derive the evidence above; it was a temporary scratch
`TEST_CASE` removed from the tree, not a kept helper.

## 4. Next steps for whoever picks this up

- Start in `TessellateGeneralLoopFace`, specifically the band whose `v0` or `v1` lands exactly on
  (or a hair inside of) a hole's y-extremum. Compare what that band's grid actually looks like
  (dump its `nRows`/`nCols`/`xs0`/`xs1`) against the band on the other side of the pinch.
- Do NOT reintroduce the wall-vs-cap phase theory from the earlier version of this task — that
  was checked directly (via `triFace`) and ruled out. The wall is never involved in this crack.
- This is a SPEC GAP candidate: REQ-313's Tessellation acceptance criteria don't explicitly
  require adjacent-face seam agreement or intra-face watertightness (only per-triangle
  winding/normal agreement and volume/area convergence). Worth a recorded decision to add an
  explicit watertightness acceptance criterion, with `RequireMeshWatertight` promoted from a
  debugging aid to a documented requirement check.
