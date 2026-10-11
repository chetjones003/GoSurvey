# TASK-272 — B-rep tessellation seam crack: torn bolt holes / curved-face joins

- Type:    bug
- Status:  **RESOLVED 2026-09-23 — see §10. The reported bug was never in the tessellator: it was
  DEPTH-BUFFER PRECISION. Read §10 first; §§1–9.8 are a historical record of an investigation that
  spent itself on the wrong subsystem, kept because the dead ends are worth not repeating and
  because several of the tessellation fixes made along the way are real improvements in their own
  right. §6 (cone apex) is still open and is a genuine, separate tessellation bug.**
- Previous status (superseded): §9.8 (2026-09-23): render-side geometry-shader fix tried, PROVEN INSUFFICIENT, and
  REVERTED. STILL OPEN — but the root cause is now much better understood; see §9.8 for what a
  correct fix actually needs.** A geometry shader that inflates a screen-space-thin triangle's apex
  (perpendicular to its longest edge, additive, capped) was built and tested directly on the user's
  real `2in Flange.dwg` file, live in the GUI, across many iterations. Two real implementation bugs
  were found and fixed along the way (naive centroid-scaling stretched a sliver's LONG axis too,
  producing visible spikes off the mesh; then a flat-shading test that seemed to show "no holes at
  all" turned out to be a red herring — a hole viewed nearly axially has no shading gradient to
  reveal its depth, so it optically vanishes into the surrounding flat-lit material, which is not a
  bug). A debug build that tints exactly the triangles the shader identifies as thin (solid red, live
  in the GUI) proved the mechanism DOES correctly find and process the real degenerate slivers — their
  shape matches the dark tearing almost exactly. **But even with the fix correctly engaging on the
  right triangles, the visible tearing was completely unchanged, pixel for pixel, from the unfixed
  baseline.** Root cause of why the fix doesn't work: it inflates each thin triangle INDEPENDENTLY,
  perpendicular to its own longest edge. These slivers are not isolated — they form a dense FAN of
  triangles that share edges with their immediate neighbours (an `EarClip` artifact of triangulating a
  bridged hole's slit, per §9h/§9m in this file). Pushing one triangle's apex without correspondingly
  moving its neighbours' matching edge misaligns the shared boundary between them, opening a NEW gap
  at that seam even as the original triangle's own degeneracy is fixed — same visible symptom, from a
  newly introduced cause, which is why nothing about the visible pattern ever changed. Confirmed via a
  completely independent, GPU-free proof: `brep::Tessellate`'s own triangle output was dumped straight
  to an OBJ and rasterized with plain CPU polygon fill (`tests/AcisSatParserTests.cpp`'s new
  `"SCRATCH dump face7 to OBJ..."` test, `[scratchobjdump]`, + a one-off render script since deleted) —
  the mesh itself is watertight and every hole is cleanly excluded (matches every prior watertightness
  test), but the fill reveals the exact same dense sliver-fan pattern this task has documented since
  §9h. All shader changes (the geometry shader, its debug-tint variant, the temporary flat-shading
  diagnostic) were fully reverted; `src/render/ViewportRenderer.cpp` is back to its pre-§9.8 state.
  Full `[brep]`/`[issue473]` suites unchanged (188/189, same single unrelated §6 cone-apex failure).
  **What a correct fix needs, for whoever picks this up next:** treat the WHOLE fan's outer silhouette
  coherently, not each triangle in isolation — e.g. inflate only the fan's true OUTER boundary edges
  (the ones on the hole/outer rim, not the internal diagonals the fan is built from) by a shared,
  consistent amount, or replace the `EarClip`-produced fan with a proper adjacency-aware
  re-triangulation of that region before it ever reaches the renderer. A stateless per-primitive
  geometry shader cannot solve this on its own because it has no visibility into a triangle's
  neighbours.
- Opened:  2026-09-22
- Owner:   Claude (chetjones003@gmail.com)

## 10. RESOLVED 2026-09-23 — the real cause: the ortho depth range, not the tessellation

### 10.1 The cause, in one paragraph

The 3D viewport projects with `Ortho(..., -depthPad, +depthPad)` where `depthPad` was
`max(cam.farZ, -cam.nearZ, halfH * 20)` — and `cam.nearZ/farZ` are a fixed `+/-100000`, so the
depth range was **200000 world units wide at every zoom level**. The depth buffer is
`GL_DEPTH24_STENCIL8`, and an orthographic projection's depth is LINEAR, so that range resolved
`200000 / 2^24 = 0.0119` world units — **about an eighth of an inch in a foot-unit drawing**. A 2"
flange's plate is 0.2 ft thick and its bolt holes are 0.062 ft across, so its front face, its bore
wall, its rim wall and its far cap all landed within a handful of depth steps of each other and
**z-fought**. The "torn / jagged rims" were that fight: at a rim, the wall and the cap swap which
one wins the depth test from pixel to pixel, so shards of the wrong surface's shading appear along
every silhouette. The mesh was fine the whole time — every watertightness result in §§1–9.5 was
true, and none of it was ever the bug.

The renderer's own comment records exactly how this was missed. Both earlier widenings of this
range were justified with the same sentence: *"depth testing is off (draw order decides), so a wide
range costs nothing."* That was true when it was written. It stopped being true when B-rep solids
and meshes arrived (REQ-313 / ADR-045) and started depth-testing, and nobody revisited the comment.

### 10.2 Why every earlier test pointed the wrong way

This is the part worth remembering, because three separate attempts were misled by the same trick.
Every "is it the lighting?" experiment in §9.6 and §9.7 removes **per-pixel colour variation** —
and so does z-fighting's only visible symptom. So:

- **Wireframe is clean** — it draws no depth-tested faces at all.
- **Hidden is clean** — its faces write depth with colour writes OFF. Every surface is the same
  colour, so whichever one wins the depth test, the picture is identical.
- **Flat colour (`i = 1.0`) is clean** — same reason. §9.6 treated this as conclusive proof that
  the fragment shader's lighting was at fault; it proves only that the artifact needs two surfaces
  that shade *differently*, which is equally true of z-fighting.
- **No lighting formula, ambient level, dithering or MSAA setting changes it** — none of them
  affect which surface wins the depth test.
- **RenderDoc showed pixels below the shader's own ambient floor** (§9.7.2). §9.7 read that as
  partial MSAA coverage on a sliver. It is much more simply a *different, darker surface* winning
  the depth test at that pixel — which the lighting formula's floor says nothing about.

The general lesson: "the artifact disappears when I flatten the colour" does not isolate lighting.
It isolates *anything that needs two differently-shaded surfaces*, and z-fighting is in that set.
The test that would have separated them in five minutes is to change the DEPTH RANGE, not the
shader.

### 10.3 The fix

`src/render/ViewportRenderer.cpp` (the projection setup) and `src/render/Camera.hpp`:

1. **The depth range is tied to the view's own scale**: `Camera::OrthoDepthPad()` returns
   `orthoHalfH * 1000`. Depth then resolves to about a seventeenth of a pixel on a 1000-pixel-tall
   viewport **at every zoom level**, instead of to a fixed world distance that happens to be usable
   for a site plan and useless for a pipe fitting.
2. **`glEnable(GL_DEPTH_CLAMP)`** (core since GL 3.2; this context is 3.3 core). This is what makes
   (1) safe: geometry outside the near/far planes is no longer CLIPPED, its depth is clamped to the
   end of the range instead. Anything nearer than the near plane still draws and still wins the
   depth test; anything past the far plane still draws and still loses. That removes the
   containment requirement that forced the range to be huge in the first place — i.e. it fixes the
   precision problem without reintroducing either of the two clipping bugs the comment records
   (entities above z = 1000 vanishing, and issue #381's tilted grid at extreme zoom-out).

`cam.nearZ/farZ` are left alone: they still feed `Camera::OrthoProjection` and the perspective
eye pull-back. The model viewport simply no longer clips with them.

### 10.4 Evidence

Captured from the real reported file (`2in Flange.dwg`) through the app itself, not by eye:
a Developer Shell test (`dwg-shaded-shots`, `src/devshell/DevShellTests.cpp`) opens a DWG named by
`GOSURVEY_T272_DWG`, frames the part named by `GOSURVEY_T272_CENTER`, and writes eight shaded
viewport captures at different orientations. Before: white shards of the flat cap's shading torn
through the grazing rim band, broken arcs where the hub meets the face, stair-stepped bore rim.
After: every rim smooth, the hub circle one continuous hairline. Same mesh, same shader, same MSAA
— only the depth range changed.

The cause was then isolated by bisection through a temporary `GOSURVEY_T272` env switch with the
candidate fixes behind separate bits (polygon offset off / tight depth range / back-face culling):
turning off the solid faces' `glPolygonOffset` changed **nothing**, tightening the depth range
fixed it **completely**. That switch has been removed.

Tests: `tests/CameraTests.cpp` gains *"Ortho depth range resolves finer than a pixel at every
zoom"*, which pins the property that broke — the range must scale with the view, and the resulting
depth step must stay under a tenth of a pixel at any zoom. Full suite **1212 of 1213**; the single
failure is §6 (cone apex), unchanged and unrelated.

### 10.5 Also cleaned up in this pass

- **All of §7's temporary diagnostics are gone** from `src/util/brep.cpp`. They wrote to
  `C:/temp/gosurvey_*` on **every** `Tessellate()` call, and one of them (§9f's self-intersection
  check) was an O(n²) scan over the bridged polygon of every planar face with holes — a real
  per-frame cost, not just an untidy log.
- The four `SCRATCH ...` tests in `tests/AcisSatParserTests.cpp` and `DEBUG loft topology dump` in
  `tests/BrepTests.cpp` are removed; they read `C:/temp/2in_flange.json`, which is not in the repo,
  and one of them failed outright in a full run.
- `kShadedAmbient` is back to `0.25` and the Shaded fragment shader back to its committed form —
  the `0.6` and the comment describing a Lambertian term the code never contained were leftovers
  from §9.7's abandoned attempts.
- `dev/build-devshell.bat` added, because the Developer Shell could not be built at all:
  `build.bat debug` fails to LINK with ~500 `LNK2038` runtime-library mismatches, since
  `third_party/xerces-c` ships a **release-only** prebuilt `.lib` (`/MD`) that a `/MDd` Debug link
  cannot use. The script configures `build/devshell` as **RelWithDebInfo** with
  `GOSURVEY_DEVELOPER_SHELL=ON` — CMakeLists only forces the shell off for `CMAKE_BUILD_TYPE`
  exactly `Release`, and RelWithDebInfo uses the release runtime, so it links. Without this there
  was no way to capture the viewport from an automated run, which is precisely why the three
  earlier attempts each fell back to asking a human to eyeball the live app after every rebuild.

### 10.6 Still open

- **§6, the cone-apex crack** — a genuine, separate tessellation bug (256 of 768 edges cracked on a
  plain `MakeCone`), untouched by any of this and still the suite's one failing test.
- **§8's SPEC GAP candidate** — REQ-313's acceptance criteria still do not require watertightness.
  Worth a recorded decision; §10 does not change that argument either way.
Nothing else. The RenderDoc scratch scripts (`rdc_analyze*.py`, `render_cap.ps1`, `cap_render.png`,
`rdcheck4.png`) belonged to §9.7's disproven theory and are deleted; the `C:/temp/rdc/*` capture
artifacts outside the repo can go too.

## 0. Read this first if you are picking this up cold

**Superseded by §10 — the reported bug was depth-buffer precision, not tessellation. Everything
below is kept as a record.**

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

## 9. OPEN, HIGH PRIORITY — the general-trim-loop (`paramLoops`) path is the real bug for imports

**This is almost certainly what the user is actually seeing.** Discovered 2026-09-22, same day as
§1/§5's (incorrect, as it turns out) "RESOLVED" claims — those claims were only verified against
hand-built `brep::Face` fixtures with `paramLoops` empty. A real imported file does not take that
shape.

### 9.1 Evidence

Added a direct `RequireMeshWatertight`-equivalent check to
`tests/AcisSatParserTests.cpp`'s existing real-file test (`"ACIS SAT import: a real Civil 3D flange
(.sat, ACISOUT) imports as a valid solid"`, `[acissat][issue473]`, using
`samples/CJ_4in_WELD_NECK_FLANGE.sat` — a real ACISOUT export of a 4" weld-neck flange, the same
real-world file class the original bug report came from). Result:

```
cracked (non-manifold) triangle edges: 2560 of 5048 by face: face0=32 face1=32 face2=32 face3=32
face4=32 face5=32 face6=32 face7=32 face8=32 face9=128 face10=94 face11=646 face12=128 face13=654
face14=110 face15=512
```

Over half the mesh is cracked, on every single face — including plain single-loop Cylinder walls
with no holes (face0–8, 32 edges each). The file-based diagnostic (§7.2) confirms why: **every
face** on this import has `paramLoops` populated (`loops=N paramLoops=N` for all 16 faces, ADR-052
general trim loops), which hits this unconditional early exit in `Tessellate()`
(`src/util/brep.cpp`, right after the per-face loop begins):

```cpp
if (!f.paramLoops.empty()) {
  TessellateGeneralLoopFace(f, chordTolerance, &mb);
  continue;
}
```

This runs for EVERY face on such an import, before the `switch (sf.kind)` block — so neither §1's
bridge+EarClip fix (which lives inside `case SurfaceKind::Plane` for `f.loops.size() > 1`) nor §5's
NURBS edge-exact boundary fix (inside `case SurfaceKind::Nurbs`) ever executes. Both fixes are real
and both regression-test suites for them are green, but they are dead code as far as a real
imported file is concerned.

### 9.2 What needs to happen

`TessellateGeneralLoopFace` itself needs the same treatment §1 gave the `paramLoops`-empty planar
case, generalized to work from a face's `paramLoops` 2D polygons directly instead of re-walking
`s.edges` (a general trim loop's whole point, per ADR-052, is that the 3D boundary curve is already
reduced to 2D polyline vertices — no `Edge` walk needed, which also sidesteps needing an edge index
to key a shared-resolution map by, unlike §5's fix). Concretely, for a **Plane** surface with
`paramLoops`: the outer+hole loops are already 2D polygons in the surface's own plane parametrization
(`curveisect::Vec2`, index-aligned with `f.loops`) — these can go straight into
`BridgeHoleIntoOuter` + `EarClip` (§1's kernel) without needing to re-derive them from edges at all,
which should fix face10/11/13/14 (the Plane, paramLoops-bearing faces) outright. The **cracked
single-loop Cylinder faces with no holes at all** (face0–8, 32 each) are a DIFFERENT, more basic bug
inside `TessellateGeneralLoopFace`'s own band decomposition — not a holes/bridging problem, since
there's no second loop — needs its own root-cause pass (start with the same `triFace`-tagged
crack-location + `EdgePointAt`-vs-band-sample comparison technique that found §1's real root cause,
per §2's note on skipping straight to data instead of guessing).

### 9.2b Partial fix applied (holes only — curved-face walls still crack)

Exempted `Plane` faces with `f.loops.size() > 1` (holes) from the unconditional `paramLoops` early
exit, and fed the bridge+EarClip kernel from `f.paramLoops` directly (as literal plane-local 2D
points — same space `WorldToPlane`/`PlaneToWorld` use, confirmed via
`TessellateGeneralLoopFace`'s own `pushVertex`) instead of resampling from `s.edges`. This is a
real, measured improvement on the real flange file (`[issue473]`, `samples/CJ_4in_WELD_NECK_FLANGE.sat`):

```
before: cracked=2560 of 5048 edges, by face: face0=32 ... face9=128 face10=94 face11=646
        face12=128 face13=654 face14=110 face15=512
after:  cracked=1728 of 3696 edges, by face: face0=32 ... face9=128 face10=32 face11=320
        face12=128 face13=320 face14=32 face15=512
```

Every Plane face's hole-driven crack count dropped sharply (face10 94→32, face11 646→320, face13
654→320, face14 110→32) — the bolt-hole/bore rims specifically, which is almost certainly what was
most visible in the user's screenshot. **But this is NOT a full fix.** ~1728 edges are still
cracked, on EVERY face including the ones that dropped, and the residual pattern (a near-uniform
`32` on most faces, `128`/`512` on the two biggest curved ones) looks like a DIFFERENT, still-open
bug: a Plane cap's outer rim (now sampled exactly from `paramLoops`) no longer matches its
neighbouring Cylinder/Cone wall's OWN independent `paramLoops`-driven band resampling — Cylinder/
Cone faces are untouched by this fix and still route through `TessellateGeneralLoopFace`'s scanline
bands, which resample the shared boundary at their own internal resolution rather than reading
`paramLoops` points directly. Full `[brep]`/full suite re-run: no regressions (same single §6
cone-apex failure as before, 188/189 and 1212/1213 respectively).

**Next step for whoever picks this up:** the same fix needs to reach `TessellateGeneralLoopFace`
itself for curved (Cylinder/Cone/Sphere/Torus/Nurbs) `paramLoops` faces — likely by having its band
rows read boundary POSITIONS directly from `paramLoops` (not just use them for inside/outside
scanline classification, which is all they are used for today per the `Face::paramLoops` docs) at
least at each band's v0/v1 edge, mirroring this same "authoritative source, not resampled" fix.
This is materially harder than the Plane case: a curved face's `paramLoops` point does not
trivially map back to a single point (it needs `LocalSurfaceDerivs`/`SurfacePointAt`, and unlike a
Plane, u is not simply x), and a band boundary can be shared between TWO curved faces whose own
`paramLoops` may have been independently resampled by the importer at different point counts.

### 9.4 2026-09-22 continued — infrastructure added, real fix still blocked, root cause of the
block now identified

Added (still in the tree, on `fix/brep-tessellation-seam-crack`, uncommitted): a `Tessellate`-level
`edgeSegs` unification pass that registers every Arc/Ellipse edge referenced by ANY paramLoops
face's loops (Plane, Cylinder, Cone) — not just the pre-existing NURBS-only pre-pass — keyed
directly by `Edge` INDEX (no geometric corner-matching needed here, unlike NURBS: `Loop::uses`
already names the real shared edge by index). Paired with `reconstructParamLoop2D`, a lambda that
rebuilds a loop's 2D boundary directly from its real edges at the unified `segsForEdge` count, used
in place of the raw (importer-fixed-count) `paramLoops` points wherever the rebuild is confirmed —
by enclosed-area agreement — to be describing the same loop the raw points do (guards against
`BrepTests.cpp`'s hand-built fixtures, which set `paramLoops` on a face whose `loops` intentionally
describes a DIFFERENT shape per ADR-052 (c), and must keep using the raw points). This is safe (full
`[brep]` suite still 188/189, only the pre-existing §6 cone-apex failure) and is genuinely a no-op
improvement on `[issue473]` — 1728/3696 cracked, unchanged — not a regression, but not a fix either.

**Tried and reverted (made things worse, not better):** also overriding `TessellateGeneralLoopFace`'s
per-band `nCols` (the Cylinder/Cone wall's own column count) up to the matching rim edge's
`segsForEdge` count, when the row is a genuine hole-free single-interval extremal boundary. This
raised the real flange's cracked-edge count from 1728 to **6912** (of 13872, resolution itself
tripled). Root cause, confirmed by direct measurement (temporary print, since removed): for a
0.031"-radius bolt hole at this test's `chordTolerance`, `SegmentsForEdge` (the formula every edge
consumer outside `TessellateGeneralLoopFace` uses) wants **256** segments, while
`GeneralLoopChordSegments` (the formula `TessellateGeneralLoopFace`'s OWN band grid has always used,
for both `nRows` and the pre-override `nCols`) wanted only **16** for the exact same edge at the
exact same tolerance. **These two "how many segments does this curve need" formulas disagree by
16x on identical input.** Forcing just the wall's boundary row up to the `SegmentsForEdge` count
without also moving whatever the NEIGHBOURING Plane cap's `reconstructParamLoop2D` rebuild settles
on (which is gated by its own area-agreement check against a different point count, and — per the
identical face11/13 crack counts before and after — did not always accept the higher count) makes
two mismatched samplings even more different, not less: the same "raise resolution without fixing
correspondence just gives two independent samplings more points to disagree at" failure mode as
§5.2's reverted floor attempt and §2's wrong lead, now confirmed a third time in a third code path.

**What this actually means for whoever picks this up next:** the fix is NOT "make
`TessellateGeneralLoopFace` consult `segsForEdge`" — it is "make `GeneralLoopChordSegments` AND
`SegmentsForEdge` agree in the first place" (or, more surgically, replace
`TessellateGeneralLoopFace`'s use of `GeneralLoopChordSegments` for a band's TWO EXTREMAL ROWS ONLY
with a direct `segsForEdge`-driven `EdgePointAt` sample — not just a column-count override — while
simultaneously confirming, not just hoping, that whatever `reconstructParamLoop2D` (or the raw
`paramLoops` fallback) the neighbouring Plane face settles on for the SAME edge is being handed the
exact same count via the exact same `edgeSegs` map key, not silently falling back to its own,
un-unified value because its area-agreement gate happened to reject the rebuild. Confirm both sides
actually reach the SAME final segment count for the SAME edge — with a print, on the real file —
BEFORE building the rest of the fix, not after; this is precisely the mistake this attempt made.

### 9.5 RESOLVED 2026-09-22 — both sides found and fixed together

The two remaining bugs, found by doing exactly what §9.4 said to do next (print and compare):

1. **`reconstructParamLoop2D`'s area-agreement gate (§9.4) was rejecting every real loop.** Added a
   print of `aRaw`/`aRebuilt`/`reldiff` and reran `[issue473]`: every genuinely-matching real loop
   (same edges, importer's 32-point `paramLoops` vs. the unified ~256–514-point rebuild) measured a
   **0.64% area difference** — legitimate inscribed-polygon discretization error between a 32-gon and
   a 256-gon approximating the same circle, not a shape mismatch — comfortably outside the original
   1e-3 (0.1%) gate. So `reconstructParamLoop2D` was silently declining on every real face, which is
   why §9.4 measured it as a no-op rather than a partial fix. Widened the gate to 2% (`0.02 * aRaw`),
   which still rejects a hand-built test fixture's genuinely-different loop (`BrepTests.cpp`'s
   `paramLoops`-on-a-mismatched-`loops` fixtures, per ADR-052 (c)) by orders of magnitude margin.
2. **Doing only one side alone makes it worse, not better — confirmed twice.** With only the Plane
   cap's gate fixed (item 1) and `TessellateGeneralLoopFace`'s own band grid still using its
   independent, ~16x-coarser `GeneralLoopChordSegments` formula, the real file's cracked-edge count
   rose to **6208** (from 1728) — the cap jumped to the fine count, the wall stayed coarse, maximal
   mismatch. Re-applying §9.4's reverted `TessellateGeneralLoopFace` rim-edge / `segsForEdge` `nCols`
   override AT THE SAME TIME as item 1 (both sides converging on the identical unified count for the
   identical shared edge) is what actually closed it.

**Verified:** `[issue473]` (the real `samples/CJ_4in_WELD_NECK_FLANGE.sat` Civil 3D flange import) —
**0 cracked edges**, all 18 assertions pass (was 1728 of 3696). Full suite: 1212/1213 passing, the
one remaining failure is §6 (cone apex), confirmed unrelated (no holes, no `paramLoops` involvement
in that repro at all). Code: `Tessellate`'s new `edgeSegs` pre-pass + `reconstructParamLoop2D`
lambda (§9.4/§9.5, `src/util/brep.cpp`), `TessellateGeneralLoopFace`'s new optional `Solid*`/
`edgeSegs*` params and rim-edge `nCols` override (§9.4/§9.5, same file). Both call sites
(`fFixed`/`tmp` dispatch) now pass `&s, &edgeSegs`; default-null params keep the function's
signature backward compatible for any future caller that doesn't have a `Solid`/`edgeSegs` handy.

**Still open after this:** §6 (cone apex, unrelated, lower priority) and §7's cleanup (the
`C:/temp/gosurvey_*` file-logging diagnostics scattered through `Tessellate`, `EarClip`,
`BridgeHoleIntoOuter` — harmless but not meant to ship; remove once §6 is closed too, per §7's own
note).

## 9.6 WRONG CONCLUSION 2026-09-23 — see §9.7 for the real cause; this section's diagnosis is
## superseded, kept as a record of the (reasonable-looking, still wrong) dead end

**This is what the user was actually seeing the whole time.** Everything in §9.1–§9.5 is real,
tested, and worth keeping (it fixed a genuine crack in the bare `.sat` fixture), but it was fixing a
bug in a code path the user's actual file never reaches.

### 9.6.1 How this was found

The user provided the real file directly: `C:\Users\chetj\OneDrive\Desktop\2in Flange.dwg`. Converted
via `GsJsonDwgFixture from-dwg` to JSON and loaded straight into a `brep::Solid` via `gsio::
SolidFromJson` (bypassing the DWG importer, reading the exact same topology `Tessellate` would see).
This is a DIFFERENT solid from `samples/CJ_4in_WELD_NECK_FLANGE.sat`: 20 faces (the pipe welded to the
flange as one combined solid), and — critically — **`paramLoops` is empty on every one of them**
(confirmed both via this JSON extraction and via the existing `Tessellate`-internal debug log reading
the live app's actual in-memory solid). So none of §9.2b/§9.4/§9.5's paramLoops-driven fixes, and
neither does the original §1 fix's `TessellateGeneralLoopFace` dispatch, ever run on this file — every
face goes through the plain rectangle-span `switch (sf.kind)` grid path that predates this whole task.

Direct measurement on this exact solid, at the live app's real `kSolidChordToleranceFt = 0.01` (not
the tests' usual 0.001–0.02):
- **Watertight**: 0 of 12312 mesh edges cracked.
- **Correctly wound**: 0 flipped-winding triangles (worst adjacent-facet angle 90°, a real square
  corner, not a bug).
- **No duplicate/overlapping geometry**: the one heuristic check that flagged something (16 "near-
  coincident" triangle pairs) turned out to be ordinary neighbouring triangles at the seam between a
  cylinder wall's own two half-turn faces (near-parallel normals, small non-zero distance) — expected
  geometry, not a z-fighting duplicate.
- A large fraction of triangles (~5800 of ~12300) are geometrically "thin" (aspect ratio > 50, one as
  extreme as 4.4 million) from ear-clipping the bridged bolt-hole/bore boundary at very fine
  resolution — this LOOKED like it could explain sub-pixel GPU rasterization dropout (exactly §9h's
  theory), and a same-session attempt to reduce them (picking the best-quality available ear each
  pass, not just the first — `EarClip`, `src/util/brep.cpp`) measurably reduced the sliver count
  (6007 → 5849) but did not move the WORST offender at all, even scanning the entire remaining ring.
  That dead end is left in place (harmless, a real if minor quality improvement, zero regression
  risk) but is NOT what fixed the user's bug — see §9.6.2.

### 9.6.2 The actual proof and fix

With the mesh independently confirmed clean, the only remaining candidate was the Shaded-view
fragment shader itself (`kShadedFs`, `src/render/ViewportRenderer.cpp`). Forced its lighting term
flat — `i = 1.0` unconditionally, bypassing `d = max(abs(dot(n, uViewDir)), 0.35)` entirely — rebuilt,
relaunched GoSurvey.exe on the real `2in Flange.dwg`: **every previously-torn edge (outer rim, bore,
every bolt hole) rendered perfectly smooth.** This is conclusive, not circumstantial: the ONLY change
was the shader, on the SAME already-verified-clean mesh.

The existing §9e grazing-term floor (0.35, from an earlier session) was the right idea but not high
enough — it reduced but did not eliminate the near-silhouette gradient steepness that lets two
adjacent facets' small normal differences land on visibly different brightnesses. Raised to 0.7,
confirmed smooth on the real file both zoomed out (whole pipe+flange) and zoomed to the flange itself.
`MSAA` was independently confirmed active and working (32 samples, `GL_MAX_SAMPLES`) — it anti-aliases
polygon edge coverage, not this per-pixel shading gradient, so it was never going to help here, exactly
as the original §9e comment said.

**Verified:** full suite 1212/1213 passing (§6 cone-apex, unrelated, only remaining failure). GUI:
relaunched on the real reported file, zoom-extents and a flange close-up both show smooth silhouettes
where every screenshot in this task previously showed torn ones.

**Lesson for next time:** a "torn/jagged edge in Shaded view" report should get the flat-shading test
(`i = 1.0`) FIRST, before any tessellation work — it isolates geometry from lighting in about five
minutes and would have saved most of today. The mesh-crack investigation in §1–§9.5 was not wasted
(the paramLoops fixes are real and the regression tests are genuine improvements), but it was answering
a question the user's actual bug report never asked.

### 9.3 Do not repeat this mistake

Before marking ANY brep tessellation fix "RESOLVED" in this task again: verify it against
`[issue473]` (or another real imported-file fixture) with a watertightness assertion, not only
against hand-built `brep::Face` fixtures. A hand-built fixture with `paramLoops` left empty tests a
code path a real import does not take.

## 9.7 STILL OPEN 2026-09-23 — RenderDoc-confirmed real cause: sub-pixel rasterization dropout on
## bridge-slit sliver triangles

§9.6 concluded this was a Shaded-fragment-shader lighting bug, based on: Wireframe clean, Hidden
clean, flat-color (`i = 1.0`) clean, only real per-pixel lighting torn. **That reasoning was a trap**
— every one of those tests disables the same thing (per-pixel COLOR variation), which also hides an
entirely different class of bug: real GPU rasterization dropout on a degenerate triangle. A pixel
with dropped coverage still gets written (background/previous-content color, or a partial MSAA
blend) whether or not the shader that WOULD have covered it computes lighting — so "does removing
lighting make it go away" is not actually a valid lighting-vs-not-lighting test. This is worth
remembering: it fooled a very thorough investigation for several hours.

### 9.7.1 What was tried and ruled out (all confirmed by direct user testing, not assumption)

- Three different Shaded fragment-shader lighting formulas (the original `abs(dot(n,viewDir))`
  rim term floored at 0.35, then 0.7; a proper Lambertian "headlight" `max(dot(n,-viewDir),0)`) —
  all still showed the torn look on the real file.
- `kShadedAmbient` raised from 0.25 to 0.6 — no change.
- 8-bit color-banding dithering added to the fragment shader output — no change.
- MSAA toggled off entirely (Settings → Graphics Performance → Hardware Acceleration) — no visible
  change either way (user-confirmed via two screenshots, same file, same view).
- z-fighting / duplicate-surface theory — directly disproven: Hidden style depth-tests the identical
  geometry and renders perfectly smoothly; if two near-coincident surfaces were competing for the
  depth buffer, Hidden would show the same flicker. It doesn't.
- Mesh-data sanity, on the EXACT real 20-face solid (extracted via `GsJsonDwgFixture from-dwg` →
  `gsio::SolidFromJson`, tessellated at the live app's real `kSolidChordToleranceFt=0.01`, not a
  test-only tolerance): 0 cracked edges, 0 flipped-winding triangles, 0 NaN/zero/off-magnitude
  normals, 0 same-face triangle-pairs with a normal jump over 30°, 0 same-FLAT-face (2D, exact)
  triangle overlaps. The data going INTO the renderer is clean by every measure tried.
- A quality-ranked `EarClip` ear-selection pass (pick the best-shaped available ear each pass,
  scanning up to the WHOLE remaining ring, not just the first valid one — §9m/§9h) — real, harmless,
  landed, but only reduced the sliver COUNT (6007 → 5849 on the real file); the single worst offender
  (aspect ratio 4.4 million) was completely unchanged even with an unbounded scan window, proving the
  problem isn't which ear gets picked — it's structural to ear-clipping a bridge slit at all.

### 9.7.2 The RenderDoc capture (the actual proof)

User captured a live GoSurvey.exe frame (Shaded style, zoomed on the torn bolt holes/bore) with
RenderDoc, saved as `flangeRenderDocCapture1.rdc`. Analyzed via RenderDoc's embedded Python console
(`pyrenderdoc`/`controller` API — NOT the standalone `renderdoc` module, which isn't importable
outside qrenderdoc's own process) with a series of scripts (`rdc_analyze2.py` through
`rdc_analyze6.py`, all still in the repo root — clean these up once this closes):

1. First two attempts picked the wrong render target (the depth/stencil buffer, then a stale
   cleared intermediate) — RenderDoc's texture list has ~5 different 1796×1070-class targets per
   frame (resolve buffers, ping-pong targets); the one actually on screen is the **swapchain**
   texture (`TextureCategory.SwapBuffer`, `R8G8B8A8_SRGB`, matching the window's real pixel
   dimensions e.g. 2560×1528 on this machine) — use that, not anything merely matching the logical
   viewport size.
2. Scanned the swapchain texture for sharp horizontal pixel-to-pixel color jumps, restricted to the
   bore/bolt-hole screen region, and further restricted to jumps where NEITHER side is near the
   black background (so silhouette edges against the clear color don't dominate the results — they
   did, at first, and are not the bug).
3. Real interior jumps found: pairs like `(216,216,216) → (72,72,72)`, repeated near-identically at
   dozens of distinct pixel locations. **This value pair is the proof.** With `kShadedAmbient = 0.6`
   at capture time, the Shaded formula's minimum possible output is `0.6 × uColor` — for a
   near-white `uColor`, that floors intensity around 130/255. **72/255 is below that floor and is
   therefore impossible for the lighting formula to produce, under ANY normal direction.** The only
   way to get there is if the fragment at that pixel isn't drawing the lit surface at full coverage
   at all — i.e. a partial-coverage MSAA blend between the object's colour and whatever is behind it
   (the black background), from a triangle so thin the rasterizer's fixed sample positions mostly
   miss it. This is a well-known real GPU limitation for near-degenerate triangles, not a driver bug
   or a GoSurvey-specific quirk.
4. `controller.DebugPixel(...)` was attempted to get the actual interpolated shader trace at these
   pixels but returned empty traces (`num inputs=0`) on every attempt — RenderDoc's GL pixel-history/
   shader-debug support is known to be less complete than its D3D/Vulkan backends; not pursued
   further since the color-value evidence above is already conclusive on its own.

### 9.7.3 The fix that was attempted and reverted

`BridgeHoleIntoOuter`'s "zero-width slit" (the bridge's return trip reuses the EXACT same `M` and
`target` points as the outbound trip, by design, per Held's bridging technique) is what forces
`EarClip` to eventually produce a triangle with two corners at, or an epsilon from, the same point —
watertight and correctly wound (real, not a bug in either sense), but with an aspect ratio that can
reach into the millions. Tried: instead of exactly reusing `M`/`target` for the return trip, offset
that copy by a tiny amount (`1e-4 * holeExtent`) perpendicular to the bridge direction, turning the
zero-width slit into a real, if tiny, corridor no triangle across it could be truly degenerate.

**This measurably CRACKED the real file**: 120 of 18540 edges, was 0, concentrated on exactly the
bolt-hole/bore-carrying faces (10–15). Root cause of the regression (diagnosed enough to revert
confidently, not fully solved): the offset changes which side of the bridge is "inside" the polygon
for at least one hole arrangement, effectively adding or removing a sliver of covered AREA rather
than just thickening the existing one — a correct version of this fix needs to preserve the
polygon's total covered area, not just perturb a point. **Reverted** rather than ship a regression;
`BridgeHoleIntoOuter` is back to its exact pre-§9q form.

### 9.7.4 What a correct fix needs to do (for whoever picks this up next)

- The corridor-offset idea is very likely still the right SHAPE of fix (give the slit real width),
  but it must preserve total polygon area exactly — e.g. offset BOTH the outbound and return copies
  symmetrically inward by half the corridor width, rather than only perturbing the return copy, or
  compute the offset in a way that provably doesn't change which side of the bridge is "outside".
  Verify by re-running the `[issue473]` watertightness assertion (must stay at 0 cracked) AND a
  volume/area check (must stay within existing epsilon of the analytic figure) before considering it
  done — both, not just watertightness, since this class of bug can pass one and fail the other.
- Confirm the fix on the REAL file, not just synthetic fixtures — a synthetic bolt-circle repro
  might not reproduce the exact hole arrangement (a single very-short bridge, or a specific
  numerical edge case) that broke §9q's attempt.
- Once a fix lands, verify it closes the loop for real: relaunch GoSurvey.exe on
  `2in Flange.dwg`, Shaded style, zoomed on the bolt holes/bore — the actual screen the user has
  been checking against all session — not just an automated test passing.
- Clean up the RenderDoc scratch scripts (`rdc_analyze*.py` in the repo root) and the `C:/temp/rdc/*`
  capture artifacts once this is done; they were essential for finding the real cause and are safe
  to keep around in the meantime.

### 9.7.5 STILL OPEN 2026-09-23 continued — a second corridor-widening attempt found the STRUCTURAL
### reason this whole approach can't work, not just a tuning problem

A second attempt (`BridgeHoleIntoOuter`, `src/util/brep.cpp`) kept `M`/`target` themselves exactly
in place (so the shared boundary with neighbouring faces stays bit-exact — the specific thing
§9.7.3's first attempt broke) and instead bowed the corridor's two rails apart only in the *middle*,
tapering back to zero width at both ends. Tried and reverted — this made things worse in the same
"worse, not better" way (0 → 8720 of 30456 cracked edges on `[issue473]`), and testing it at two
corridor widths 100x apart gave the identical cracked-edge count (8720) and identical worst aspect
ratio (97784.1) both times, which is what proved this isn't a tuning problem:

**Why real width can never work inside a single `EarClip` ring, at all.** The existing zero-width
slit only passes the watertightness check because the outbound edge (`target -> M`) and the return
edge (`M -> target`) are POSITIONALLY IDENTICAL — the check quantizes vertex positions and counts
edge uses, so it sees "2 uses of the same edge" and calls it watertight, even though it's really one
degenerate fold-back with no actual second triangle behind it. The instant the two rails stop being
positionally identical — any nonzero bow, in any direction, at any width — every one of those newly-
distinct edges becomes a real boundary edge used only ONCE, because nothing else in the mesh was
ever built to be its positional twin: a bridge is internal to one face's own `EarClip` ring, and no
neighbouring face's independently-sampled geometry has any reason to land on this corridor at all.
A single simple-polygon ring cannot represent a real-width, still-watertight corridor this way —
doing so needs FOUR distinct rail points forming an actual quadrilateral loop with its own internal
diagonal (two triangles sharing that diagonal, both fully inside this same face's triangulation),
not two rails that only pass the check by coinciding.

**What this means for whoever picks this up next:** do not retry any variant of "give the bridge
slit real width by moving points in `BridgeHoleIntoOuter`'s ring" — this is now confirmed structural,
not a magnitude or direction mistake, on the second independent attempt. The correct fix almost
certainly belongs downstream of `brep::Tessellate`'s topology entirely, on the render side, where the
strict "every edge used exactly twice" invariant doesn't apply (that invariant is for the CSG/volume-
facing mesh's correctness, not for what the GPU actually rasterizes):
- A render-only vertex nudge: after tessellation, in the code path that builds the GPU vertex buffer
  (`ViewportRenderer.cpp` or wherever `Tessellation` gets uploaded), detect triangles with extreme
  aspect ratio and nudge their near-duplicate vertices apart by a tiny screen-space amount at render
  time — this is cosmetic only, never touches `brep::Tessellate`'s output, and so can't affect volume,
  CSG, export, or the watertightness tests at all.
- Or a "minimum triangle screen coverage" / conservative-rasterization-style technique, applied per-
  frame in the vertex/geometry shader, which is the standard real fix for this exact known GPU
  limitation (thin triangles missing every fixed MSAA sample position) and doesn't require touching
  mesh topology at all.

Regression test added (kept, currently green against the un-widened baseline, will catch any future
regression whether from a topology change or a render-side one that somehow makes the underlying
mesh worse): `tests/AcisSatParserTests.cpp`, `[acissat][issue473]` — asserts the worst tessellated
triangle's aspect ratio is `< 10000` on the real flange import (this is a placeholder ceiling, not a
target; the actual bug is still open, so the real worst aspect ratio today is far below 10000 only
because it's the UNFIXED baseline — tighten this bound once a real fix lands, don't treat 10000 as
"good").

## 8. SPEC GAP candidate (applies across §1, §5, §6)

REQ-313's Tessellation acceptance criteria don't explicitly require adjacent-face seam agreement
or intra-face watertightness — only per-triangle winding/normal agreement and volume/area
convergence, which is not enough to catch this whole class of bug (as §1's EarClip fallback
demonstrated: a wrong triangulation can still pass a total-area check). Worth a recorded decision
to add an explicit watertightness acceptance criterion, with `RequireMeshWatertight` promoted from
a debugging aid to a documented, permanent requirement check across every REQ-313/314/315 solid
test, not just the ones this task happened to touch.
