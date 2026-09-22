# TASK-272 — B-rep tessellation seam crack: torn bolt holes / curved-face joins

- Type:    bug
- Status:  RESOLVED for the reported bug (bolt holes / flat-face holes). A separate,
  unrelated crack at a plain cone's apex remains open — see "Remaining open item" below.
- Opened:  2026-09-22
- Resolved: 2026-09-22
- Owner:   Claude (chetjones003@gmail.com)

## 0. Resolution summary

Root cause was NOT the wall-vs-cap phase mismatch first suspected (disproven — see §2), nor
purely the cross-band resolution mismatch (real bug, fixed, but insufficient alone) — it was a
genuine topology pinch in `TessellateGeneralLoopFace`'s scanline/band decomposition at a hole's
own top/bottom extremum, where the interval count changes between adjacent bands in a way no
per-band column count can reconcile.

**Fix:** stopped routing planar faces-with-holes through the scanline/band tessellator entirely.
Replaced it with an exact construction for `SurfaceKind::Plane` (a plane has no curvature, so no
chord-tolerance banding is needed at all): bridge every hole into the outer loop as a zero-width
slit (`BridgeHoleIntoOuter`), producing one simple polygon, then ear-clip it (`EarClip`). This has
no bands and so nothing to pinch.

That surfaced a second, previously-invisible bug in `EarClip` itself: on a degenerate/thin input
(exactly what a bridge slit produces) its ear-finding could fail to find any clippable ear and
silently fall back to a one-point fan over the remainder — self-overlapping for a non-star-shaped
remainder, and invisible to a total-signed-area check (the shoelace identity gives a
self-overlapping decomposition the same total area as a valid one), so it only ever showed up as a
wrong integrated volume, much later. Fixed by making `EarClip`'s ear/containment tests
tolerance-based (scaled to the ring's own extent) instead of exact-zero comparisons, explicitly
handling a query point that coincides with a triangle corner (a bridge's duplicated point), making
it refuse outright rather than silently degrade if a pass still finds no ear, and filtering any
residual zero-area (degenerate) triangles from its output.

**Verified:** the bolt-hole/box-with-hole reproduction goes from 536 cracked mesh edges to 0. Full
suite 1209/1210 passing (the one failure is the separate cone-apex item below). See commit
`89b7939` on `fix/brep-tessellation-seam-crack`.

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

(§§2 above, down through here, is the historical trail of diagnosis and two abandoned fix
attempts, kept because it records real dead ends — see §0 for the fix that actually landed.)

## 3. Reproduction (regression test, in tree)

`tests/BrepTests.cpp`: `RequireMeshWatertight(t)` checks every triangle edge in a tessellation is
shared by exactly two triangles. As of the §0 fix, one call site still fails:

- `"Tessellation agrees with the analytic figures and winds outward"` / case "cone": 256 of 768
  mesh edges cracked. This is a plain `MakeCylinder`/cone primitive with NO holes — it never
  touches `TessellateGeneralLoopFace`, `EarClip`, or `BridgeHoleIntoOuter` at all, so it is a
  different bug from the one this task fixed, not a residual of it. Likely candidate: the apex-fan
  triangulation (`SurfaceKind::Cone` with `r1 == 0`) disagreeing with the wall's own rim sampling
  at the apex point, a structurally similar "everything meets at one point" situation to the pinch
  bug this task DID fix, but in a different code path — unconfirmed, not investigated.
- The bolt-hole/flange repro (`"Curved B1: ... SUBTRACT drills a round hole through the box
  (B2a)"`) is now CLEAN (0 cracked edges, was 536).

Run: `.\build\GoSurveyTests.exe "[brep]"` after `./dev/build`.

## 4. Next steps for whoever picks up the cone-apex item

- Confirm it's actually a `SurfaceKind::Cone` apex issue (not a cylinder's own wall/cap seam) by
  checking `t.triFace` on the cracked edges of a plain `MakeCone`-only repro, the same way this
  task's earlier (superseded) sections diagnosed the bolt-hole crack — that technique is proven
  useful, unlike the fixes those sections tried.
- This is a SPEC GAP candidate: REQ-313's Tessellation acceptance criteria don't explicitly
  require adjacent-face seam agreement or intra-face watertightness (only per-triangle
  winding/normal agreement and volume/area convergence). Worth a recorded decision to add an
  explicit watertightness acceptance criterion, with `RequireMeshWatertight` promoted from a
  debugging aid to a documented requirement check — it would have caught the bolt-hole crack this
  task fixed far earlier than a user-reported screenshot did.
