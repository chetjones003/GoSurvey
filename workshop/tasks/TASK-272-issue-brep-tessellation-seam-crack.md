# TASK-272 — B-rep tessellation seam crack: torn bolt holes / curved-face joins

- Type:    bug
- Status:  **Originally reported bug (torn bolt holes / flange-plate holes): RESOLVED and
  verified on the user's actual reported file.** A second, separate crack (§5, the NURBS
  loft/sweep transition-piece boundary) is now **RESOLVED** too (see §5.5). One more, unrelated
  crack remains OPEN — a plain cone's apex crack (§6, unconfirmed root cause, lower priority).
- Opened:  2026-09-22
- Owner:   Claude (chetjones003@gmail.com)

## 0. Read this first if you are picking this up cold

- The bolt-hole/flange-plate crack that was originally reported is fixed. Do not re-open that
  investigation — §§1–4 below are historical record (including two wrong leads and two reverted
  fix attempts), kept because they document real dead ends worth not repeating, not because the
  bug is still open.
- The NURBS loft/sweep boundary crack (§5) — a small transition/fillet solid near the flange's
  bore, distinct from the flange plate itself — is also fixed now; see §5.5 for the fix actually
  applied (edge-exact boundary sampling + cross-face shared-edge resolution unification).
- The only thing still open is §6, the plain-cone apex crack — unrelated to both §1 and §5 (no
  holes, no NURBS faces involved), unconfirmed root cause, lower priority.
- Temporary diagnostics are still live in the tree (§7) — clean these up once §6 is also closed,
  since the file-based log is still useful for diagnosing it.

## 1. The originally reported bug (RESOLVED)

**Title:** Curved solid faces render with jagged/torn edges and inconsistent shading where a
curved wall meets a flat face (bolt holes, bored holes, any hole through a flat plate).

**Observed:** In the GUI, a flange's bolt holes and the flange plate's own body showed ragged,
torn-looking rims instead of clean circles, with shading artifacts at the seam (screenshots: a
"2in Flange" pipe-fitting part, from a user report during the issue #486 pipe/fitting epic — no
GitHub issue number was assigned before this task started).

**Final root cause (see §§2–4 for how this was actually found — the short version below is what
turned out to be true, not the path taken to find it):**

`brep::Tessellate`'s planar-face-with-holes path used to route through
`TessellateGeneralLoopFace`, a scanline/band decomposition tessellator. At a hole's own top/bottom
extremum, the number of scanline intervals genuinely changes between two adjacent bands (no hole
yet, then the hole opens) — a real topology pinch that no per-band column-count fix can reconcile,
because the two bands' boundary rows are structurally different shapes, not just differently
resolved.

**Fix:** stop routing planar faces-with-holes through the band tessellator. A plane has no
curvature, so its hole-bearing boundary can be triangulated EXACTLY: bridge every hole into the
outer loop as a zero-width slit (`BridgeHoleIntoOuter`, in `src/util/brep.cpp`), producing one
simple polygon, then ear-clip it (`EarClip`). No bands, so nothing to pinch.

That single change then needed four follow-on fixes before it was actually correct on real
multi-hole parts (each found and fixed via a fast synthetic repro added to `BrepTests.cpp`, not
by iterating against the full GUI app):

1. **`EarClip` itself had a latent bug**, exposed by bridging's thin/degenerate input: when a full
   pass found no clippable ear, it silently fell back to a one-point fan over the remaining
   vertices — self-overlapping for a non-star-shaped remainder, and invisible to a total-signed-
   area sanity check (the shoelace identity gives a self-overlapping decomposition the same total
   area as a valid one), so it only showed up as a wrong integrated VOLUME, much later, not as an
   obviously-wrong shape. Fixed: tolerance-based ear/containment tests (scaled to the ring's own
   extent, not exact-zero comparisons), explicit handling of a query point that coincides with a
   triangle corner (exactly what a bridge's duplicated point produces), refusing outright instead
   of silently degrading when a pass truly finds no ear, and filtering any residual zero-area
   triangles from the output.
2. **The bridging direction was a fixed +X ray** for every hole. Fine for one hole; for several
   holes around a circular boundary (an actual bolt circle), a hole on the far side has to cast
   across the shape's interior and can cross another hole or an earlier bridge — `EarClip` then
   correctly refuses a self-intersecting input, but because that refusal used to propagate as an
   outright face failure, **the whole part stopped rendering** (a strictly worse regression).
   Fixed: bridge direction now radiates outward from each hole's own position (away from the outer
   ring's centroid) instead of a fixed direction — like spokes, which do not cross each other for
   a bolt-circle-shaped arrangement.
3. **A real duplicate-point bug in the bridge splice itself**: the closing hole loop already
   starts AT the hole's own extremal point (`mi`), so pushing that point once explicitly before
   the loop inserted it a THIRD time total (the explicit push, the loop's own first point, and the
   closing point), leaving a zero-length edge right at the bridge start. Harmless for one hole;
   compounding once per hole on a multi-hole face was enough to defeat `EarClip`'s ear-finding
   entirely, independent of tolerance (confirmed: even escalating the tolerance 10x six times over
   did not help, which is what pointed at a logic bug rather than float precision). Found via a
   direct O(n²) self-intersection check on the bridged polygon coming back clean (0 crossings) —
   proving the polygon was topologically valid and the bug had to be inside `EarClip`/the splice
   itself, not the bridging geometry.
4. **Fall back, never refuse.** `BridgeHoleIntoOuter`'s visibility assumption (no reflex vertex in
   the way) is not provably true for every real hole arrangement. Rather than trust it
   unconditionally, any failure anywhere in the bridge+ear-clip attempt now falls back to the
   pre-existing `TessellateGeneralLoopFace` path — worse (the original pinch crack), but never
   "face doesn't render at all," which is the one regression this must never reintroduce.

**Verified, with real evidence, not just synthetic tests:** a temporary diagnostic
(`std::fopen("C:/temp/gosurvey_bridge_debug.log", ...)`, see §7) was added directly inside
`brep::Tessellate` and the user ran the actual reported file through it. The log showed the
flange's own solid (20 faces, including the two `loops=6` flat faces — outer boundary + one
central bore + four bolt holes) with **zero watertightness failures**. The visible torn look the
user kept reporting after this fix was confirmed, via the same log, to be coming from a *different
solid* in the same scene (§5) — not a residual of this bug.

Permanent regression tests added to `tests/BrepTests.cpp`:
- `"A bolt-circle flange (4 holes around a circular plate) tessellates crack-free"` — the fixed-
  direction-bridging repro (item 2 above).
- `"A pipe flange (big central bore + 4 bolt holes) tessellates crack-free"` — a mixed big-hole +
  small-holes repro, matching the real part's `loops=6` shape.

Both check volume (against the closed-form expectation AND the re-integrated tessellated volume),
winding, and `RequireMeshWatertight`. Full suite: 1210/1211 passing — the one failure is the
unrelated cone item, §6.

Commits on `fix/brep-tessellation-seam-crack`: `1a65ce1`, `29dbb47`, `985c1b2`, `704d8a9` (and
predecessors) — see that branch's history for the full sequence, including the two reverted
attempts recorded in §§3–4 below.

## 2. Wrong lead #1 (historical, corrected — do not re-open)

An early version of this task theorized the crack was a wall-vs-cap phase mismatch (the
cylindrical wall's `f.uStart` parametrization disagreeing with the flat cap's hole-loop edge
parametrization). **This was checked directly and disproven.** A `triFace`-tagged crack-location
dump (temporary scratch test, since removed) on a `BooleanSubtract(box, throughCylinder)` repro
showed every cracked mesh edge touched only ONE triangle, and that triangle's `triFace` was always
the SAME flat cap face — the crack was entirely INSIDE one face's own triangulation, never between
the wall and the cap. Two fix attempts on this wrong theory (deriving the wall's own angle
sequence from its rim edge, with and without an empirical winding-direction correction) both
caused real regressions elsewhere (wrong tessellated volumes, inverted normals on previously-
correct cylinder/cone/sweep cases) and were reverted.

## 3. Wrong lead #2 (historical, corrected — do not re-open)

The actual (at-the-time) root cause was identified as `TessellateGeneralLoopFace`'s per-band
column-count mismatch: it decomposes a face-with-holes into horizontal bands and grids each one
independently (`nCols = max(that band's own bottom-row need, that band's own top-row need)`),
with nothing tying one band's count to its neighbor's, even though adjacent bands share one row. A
two-pass relaxation fix (propagate the max column count through runs of bands with matching
interval topology) was implemented and is safe (monotonic, only ever raises resolution), but **did
not fix the reproduction's crack count** — the crack was concentrated at the hole's own top/bottom
extrema, a genuine topology pinch between bands (see §1's final root cause), not a resolution
mismatch between otherwise-corresponding bands. The two-pass relaxation code was later removed
entirely when §1's fix (bridging + ear-clip) replaced the whole band-tessellator code path for
planar faces.

## 4. Reproduction for the resolved bug

`tests/BrepTests.cpp`, tag `[brep]`: `RequireMeshWatertight(t)` checks every triangle edge in a
tessellation is shared by exactly two triangles.

- `"A bolt-circle flange (4 holes around a circular plate) tessellates crack-free"` — PASSES.
- `"A pipe flange (big central bore + 4 bolt holes) tessellates crack-free"` — PASSES.
- `"Curved B1: ... SUBTRACT drills a round hole through the box (B2a)"` — PASSES (0 cracked edges,
  was 536 before this task).

Run: `.\build\GoSurveyTests.exe "[brep]"` after `./dev/build`.

## 5. OPEN — NURBS loft/sweep transition-piece crack (this is what the user is still seeing)

**Observed:** after §1's fix landed and was confirmed correct on the flange plate itself (bolt
holes visibly clean in a follow-up screenshot), the user still saw a torn/jagged rim — but on a
small ring-shaped area near the flange's central bore, not on the bolt holes. Visually similar to
the original complaint, which is why it initially looked like a residual of the same bug.

### 5.1 It is a different solid, not the flange plate

Confirmed via the file-based diagnostic (§7.3) reading the user's actual loaded scene: alongside
the flange's own 20-face solid (zero watertightness failures — see §1), the SAME scene contains
one or two small solids with **2 flat (`SurfaceKind::Plane`, `loops=1`) faces + 6 (or 4)
`SurfaceKind::Nurbs` faces** — the shape of a `Loft`/`Sweep`-built transition or fillet ring, per
REQ-315/ADR-048. These are what fail watertightness:

```
Tessellate call: faces=8
  face 0 kind=Plane loops=1 / face 1 kind=Plane loops=1
  face 2..7 kind=Nurbs loops=1
WATERTIGHT FAIL: cracked=768 of 74880 edges, by face: face0=256 face1=256 face2=64 face3=64 face6=64 face7=64
```

(A second, 6-face variant of the same shape — 2 Plane + 4 Nurbs — showed the identical `cracked=768`
pattern, confirming this is deterministic and not scene-specific.)

The flat caps (`face0`, `face1`) and half the NURBS side patches crack; the pattern (both a flat
cap AND its neighboring curved faces failing, in matching-ish counts) is structurally the same
"neighboring faces disagree about a shared boundary's sampling" class of bug already fixed twice
this task (§1 items 1–2, and the original cylinder-wall case referenced in §2) — but here it's a
NURBS patch against its own flat end cap, in the `SurfaceKind::Nurbs` case of `Tessellate`
(`src/util/brep.cpp`, the block starting `case SurfaceKind::Nurbs: {`), which this task has not
touched before now.

### 5.2 Root cause, as far as confirmed

The NURBS grid resolution `n` (used uniformly for BOTH the patch's u and v directions) is computed
from `SegmentsForArc(netStep, kHalfPi, chordTolerance)`, where `netStep` is the patch's own
control-net edge length — a curvature heuristic with NO relation to how many segments the
NEIGHBORING flat cap uses for the SAME shared boundary edge (`SegmentsForEdge` on that edge
directly, the same rule every other fixed case in this task follows).

**A resolution-only fix (raise `n` to at least what the boundary edge needs) was tried and made
it WORSE**, not better: cracked-edge count went from 768 to 1024 on the synthetic repro (see §5.3)
when `n` was floored at `max(n, SegmentsForEdge(edge))` for every `CurveKind::Arc` edge in the
patch's own loop. This is the same signature seen with the wall-vs-cap wrong lead in §2: raising
resolution without fixing PHASE just gives two mismatched samplings of the same curve more points
to disagree at, not fewer. **This means the NURBS patch's own `u`/`v` knot-based parametrization
does not correspond in phase to the boundary edge's own geometric (arc-length) parametrization**,
and fixing this needs the same kind of "derive the shared boundary's points directly from the
edge, not from an independent formula" treatment that worked for the planar-face case in §1 — NOT
a floor/count adjustment, which was already tried and reverted (see the reverted diff in this
branch's history around the `case SurfaceKind::Nurbs` block, or reapply the idea below to see the
regression again quickly rather than re-deriving it).

### 5.3 Reproduction (regression test, in tree, currently RED)

`tests/BrepTests.cpp`, test `"Loft through three circles is a stack of cone frustums"`
(`[brep][req315]`) — a plain, minimal loft with three circular cross-sections, no booleans, no
holes. `RequireMeshWatertight` and `RequireWindingMatchesNormals` were added to its end; it
currently fails with **768 of 50304 edges cracked**, matching the real part's pattern exactly (2
flat caps + curved side patches disagreeing at their shared boundary). This is the fast, isolated
repro to iterate against — it reproduces in well under a second, versus reproducing via the full
GUI app and a multi-minute rebuild/relaunch/screenshot cycle each time.

Also in the tree: `TEST_CASE("DEBUG loft topology dump", "[brepdebug]")` at the end of
`BrepTests.cpp` — dumps every face's kind/loops and every loop edge's kind/sweep/radius for this
same loft, useful for inspecting the exact loop/edge structure around a NURBS patch's boundary
before attempting a fix. Not a kept test; remove or convert to a real assertion once §5 is closed.

### 5.4 Next steps

- Do NOT retry the "floor `n` at the boundary edge's `SegmentsForEdge` count" fix without also
  fixing phase — confirmed to make things worse, not a partial improvement.
- The likely correct fix mirrors §1: for the NURBS patch's boundary row(s) that correspond to a
  shared edge with a neighboring flat cap (or the next band in a multi-band loft), sample that
  row's points DIRECTLY from the edge (`SegmentsForEdge`/`EdgePointAt`, exactly as the cap already
  does), and only use `nurbs::EvaluateWithDerivs` for the INTERIOR of the patch, not its boundary.
  This guarantees the shared boundary is bit-identical between the two faces, matching the
  approach that has worked in every other case in this task, rather than trying to reconcile two
  independently-computed samplings of the same curve after the fact.
- Use `"Loft through three circles is a stack of cone frustums"` (§5.3) to iterate — it is a
  faithful, minimal, fast repro. Confirm the fix there before touching the real flange file again.
- Watch for the SAME class of regression risk as §1 item 4: if a NURBS patch's boundary loop
  structure varies (e.g. `Sweep`'s mitred corners, per REQ-315, may have a different edge layout
  than `Loft`'s bands), a fix tuned to `Loft` alone could misfire on `Sweep` output. Consider the
  same "fall back to the old grid-only sampling on any failure" safety net used in §1 item 4,
  rather than assuming one code path covers every NURBS-producing command.

### 5.5 Fix actually applied (RESOLVED)

Two changes to `brep::Tessellate` (`src/util/brep.cpp`), both landed together:

1. **Edge-exact boundary sampling.** In the `case SurfaceKind::Nurbs` grid-building block, the two
   v-boundary rows of the grid (`j == 0`, at `v = vLo`, and `j == n`, at `v = vHi` — the band's
   bottom/top rim) are no longer read from `nurbs::EvaluateWithDerivs`. When the face's loop has
   the standard 4-edge rectangle shape (`uses[0]`/`uses[2]` curved v-boundaries, `uses[1]`/`uses[3]`
   straight u-sides — confirmed against `brep::Loft`'s own band construction via §5.3's topology
   dump), those rows are sampled directly from the matching loop edge via `EdgePointAt`, exactly as
   a flat cap's own `sampleLoop` already does for the same edge. This is what actually fixes PHASE
   — §5.2's earlier "just floor `n`" attempt still used `EvaluateWithDerivs`, so a matching point
   COUNT did not imply matching point POSITIONS, and made the crack worse, not better. The analytic
   patch normal is still evaluated at the matching (u, v) for shading — only position changed.
2. **Cross-face shared-edge resolution unification.** A pre-pass, right before the per-face
   tessellation loop, finds every NURBS face's two v-boundary edges this way and unifies their
   segment counts by repeated relaxation (`edgeSegs`, in `Tessellate`): each face's own two
   boundary edges are forced to `max` of each other (a uniform (u, v) grid needs ONE resolution for
   its whole u-direction, but the two edges it touches — different radii top/bottom of a frustum
   band — can independently want different natural counts), folding in each face's own curvature
   need (`NurbsPatchCurvatureSegs`, factored out of the old inline computation) and propagating
   until stable (≤ 8 passes, monotonic — only ever raises a count, so it cannot invalidate an
   already-correct neighbour, the same safety argument the reverted §3 two-pass relaxation relied
   on). Every OTHER consumer of `SegmentsForEdge` in `Tessellate` (a flat cap's own loop sampling,
   the planar-hole bridging path, `TessellateGeneralLoopFace`'s fallback) now goes through a
   `segsForEdge` lookup that prefers this unified count over the edge's independent natural one, so
   a cap and its neighbouring NURBS band always agree on how many points to place along their
   shared rim.

Both changes are additive and defensive: a NURBS face whose loop doesn't match the standard
4-edge-rectangle shape (a future Sweep mitred corner, say) is left on the pre-existing
curvature-only grid untouched, per §1 item 4's "fall back, never misfire" rule.

**Verified:** `"Loft through three circles is a stack of cone frustums"` (§5.3) now passes —
`RequireMeshWatertight`/`RequireWindingMatchesNormals` both green, 0 cracked edges (was 768 of
50304). Full `[brep]` suite: 188/189 passing (the one failure is §6, unchanged, unrelated to this
fix — no holes, no NURBS faces). Full suite: 1212/1213 passing, same single failure.

The `TEST_CASE("DEBUG loft topology dump", "[brepdebug]")` scratch test (§5.3) is still in the
tree — remove it (or convert to a real assertion) as part of the §7 cleanup once §6 closes too.

## 6. OPEN — cone apex crack (separate, unconfirmed, lower priority)

`"Tessellation agrees with the analytic figures and winds outward"` / case "cone": 256 of 768 mesh
edges cracked. A plain `MakeCylinder`/cone primitive with NO holes — never touches
`TessellateGeneralLoopFace`, `EarClip`, or `BridgeHoleIntoOuter`, and (separately) does not involve
any `SurfaceKind::Nurbs` face either, so it is unrelated to both §1 and §5. Likely candidate: the
apex-fan triangulation (`SurfaceKind::Cone` with `r1 == 0`) disagreeing with the wall's own rim
sampling at the apex point — a structurally similar "everything meets at one point" situation to
the §1 pinch bug, but in yet another code path. Not investigated. Confirm via a `triFace`-tagged
crack-location dump on a plain `MakeCone`-only repro (the same technique that correctly diagnosed
§1, after two wrong leads that skipped this step — do this FIRST, before attempting a fix).

## 7. Temporary diagnostics still in the tree

These were load-bearing for finding §1's real root cause on the user's actual file (inference and
synthetic repros alone were not enough — two of them pointed at the wrong code entirely) and for
finding §5. Clean up is part of closing §5, not a prerequisite for starting it, since they will
likely be needed again to confirm any NURBS fix on the real file.

1. `src/util/brep.cpp`, `EarClip`: a `stallEscalations` tolerance-loosening retry loop (up to 6x
   10x escalations) on a full pass finding no ear. Safe to keep permanently — it is a genuine
   robustness improvement — but was NOT what fixed the real bug (§1 item 3), so do not rely on it
   alone if debugging a future `EarClip` failure; get real data first.
2. `src/util/brep.cpp`, inside `Tessellate`, right after `MeshBuilder mb{&mesh};`: an unconditional
   per-call dump of every face's kind/loop-count to
   `C:/temp/gosurvey_bridge_debug.log` (append mode). This is what revealed the flange scene
   actually contains a separate NURBS solid (§5).
3. `src/util/brep.cpp`, inside the `f.loops.size() > 1` planar-hole block: an on-failure-only dump
   (`face=`, `loops=`, `outerPts=`, `reason=`) to the same log, for diagnosing §1 item 4's fallback
   path specifically.
4. `src/util/brep.cpp`, right before `*out = std::move(mesh);` at the end of `Tessellate`: a full
   watertightness check (same algorithm as the test helper `RequireMeshWatertight`) on the
   FINISHED mesh, logging `WATERTIGHT FAIL: cracked=N of M edges, by face: ...` when any edge is
   not shared by exactly two triangles. This is what pinpointed §5 precisely (which faces, how
   many edges) directly on the user's real file, with no synthetic reproduction needed first.
5. `tests/BrepTests.cpp`: `TEST_CASE("DEBUG loft topology dump", "[brepdebug]")` — see §5.3.

**Before this task is fully closed** (§5 and §6 both resolved): remove all of the above, or
convert the permanent ones into documented, intentional features (the `EarClip` escalation retry
is worth keeping either way; the file-logging diagnostics are not — they write to a hardcoded
`C:/temp` path unconditionally and should not ship).

## 8. SPEC GAP candidate (applies across §1, §5, §6)

REQ-313's Tessellation acceptance criteria don't explicitly require adjacent-face seam agreement
or intra-face watertightness — only per-triangle winding/normal agreement and volume/area
convergence, which is not enough to catch this whole class of bug (as §1's EarClip fallback
demonstrated: a wrong triangulation can still pass a total-area check). Worth a recorded decision
to add an explicit watertightness acceptance criterion, with `RequireMeshWatertight` promoted from
a debugging aid to a documented, permanent requirement check across every REQ-313/314/315 solid
test, not just the ones this task happened to touch.
