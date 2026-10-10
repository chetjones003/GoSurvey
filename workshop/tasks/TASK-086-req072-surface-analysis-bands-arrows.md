# TASK-086 — Surface analysis: elevation banding, slope banding, slope arrows, and the legend

- Type:    feature
- Status:  **done** (2026-08-23) — verification PASS. Step 6 (empirical GPU re-measurement of the
           REQ-100 surface profile) is a recommended follow-up rather than a blocker; see §9 and §11.
- Opened:  2026-08-21
- Owner:   Workshop

## 1. Authority

- Goal:         GOAL-05 (terrain modelling — M-Surfaces). Roadmap step 7 (`spec/roadmap.md:129`).
- Requirements: **REQ-072** (accepted) — elevation banding, slope banding, and slope arrows.
                **REQ-070** (accepted) — the style carries the band and arrow settings.
                **REQ-100**, **REQ-101** (accepted).
- Constraints:  CON-07; architecture §11.5.
- Authority for the architectural shape: **ADR-036 (g)**, which **amends ADR-028 (h)**, +
  decision **D-2026-08-21-a**.

### Acceptance (restated verbatim from REQ-072)

- "a triangle of known elevation and of known slope each take the colour their band prescribes,
  including at an exact breakpoint, where the band a value falls into is defined and tested rather
  than left to float comparison"
- "the legend's displayed ranges equal the table's, and change with it"
- "on a planar tilted surface every arrow points the same direction, and that direction matches the
  hand-computed downhill vector within REQ-101"
- "a perfectly flat triangle produces no arrow direction and is drawn as flat rather than as an
  arbitrary direction"
- "turning banding off restores the style's plain display unchanged"

- Owning subsystem: **util** (band assignment + downhill vectors — pure), **Domain** (the range table
  on the style), **Renderer** (draw), **UI** (the table editor + the legend).

## 2. Scope

- **In scope:** an editable range table on `SurfaceStyle` (band count, breakpoints, colour per band);
  per-triangle colouring by **elevation** or by **slope**; an on-screen **legend** whose ranges are the
  table's; **slope arrows** per triangle in that triangle's downhill direction, coloured by grade; the
  three as independent toggles; the **Analysis** tab of the Surface Style dialog (Directions,
  Elevations, Slopes, Slope Arrows), which TASK-085 leaves as a stub.
- **Out of scope:** watershed analysis (no requirement — a SPEC GAP, refused by D-2026-08-21-a);
  cut/fill maps (REQ-073, its own task); a legend in paper space (no requirement);
  the **Directions** analysis Civil 3D shows on the same tab (no requirement covers aspect/direction
  analysis — the tab section is **omitted**, not stubbed, for the reason ADR-036 (i) gives).
- **Smallest change:** a range table on the existing style, a pure band-assignment function, and one
  extra batch kind in the display-geometry cache TASK-085 already builds.

## 3. Architectural boundary check  (workflow.md §4)

- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **Yes → escalated and RESOLVED before planning.** Two things: the range table is a `.gs`
      data-format addition, and **the render path departs from ADR-028 (h)**, which said shading
      reuses the REQ-064 triangle shader. Escalated as a SPEC GAP; decided as **ADR-036 (g)**, which
      amends ADR-028 (h) on a concrete ground: `shadedProgram_` applies two-sided `abs(dot(N,V))`
      lighting, so a triangle would **not** display the colour its band prescribes, and REQ-072's
      acceptance is precisely that it does — read against a legend showing that same colour. Lighting
      would make the legend a lie. Per-band CPU batching on the existing unlit line program is used
      instead: no new shader, no new uniform, no per-vertex colour attribute.
- No new dependency, no new global, no new layer.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Is the Analysis tab in scope for this round of surface styles? | 2026-08-21 | Yes — the user chose "spec scope + Analysis". |
| Q2 | REQ-072 requires the band at an exact breakpoint to be "defined and tested". Which way? | — | **Not a user question — a Workshop choice inside the boundary, and it must be written down rather than emerge.** Rule: bands are **half-open, `[lo, hi)`**, so a value exactly on a breakpoint falls in the band **above** it, and the topmost band is closed at its top so the maximum value has a band. Recorded here, documented at the function, and tested directly. |

## 5. Assumptions

```
ASSUMPTION-1: A triangle's band is decided from a single representative value — its centroid
              elevation for elevation banding, its plane's slope for slope banding — not by
              subdividing the triangle where a band boundary crosses it.
- Because:    REQ-072 says "a triangle of known elevation ... takes the colour their band
              prescribes", which presumes one colour per triangle.
- Risk if wrong: on a coarse TIN with narrow bands, band boundaries follow triangle edges rather
              than contours, which looks blocky next to the contour lines drawn over the same
              surface. Cosmetic, and exactly what Civil 3D does.
- Validate by: show the user the banded display on real data before treating it as settled.
```

```
ASSUMPTION-2: "A perfectly flat triangle" means one whose plane normal has no horizontal component
              within a stated epsilon, not one whose three Z values are bitwise equal.
- Because:    REQ-072 requires no arrow for a flat triangle and gives no tolerance; a bitwise test
              would emit an arbitrary-direction arrow for a triangle that is flat to any practical
              measure, which is the exact failure the requirement names.
- Risk if wrong: too large an epsilon suppresses arrows on a genuinely shallow grade.
- Validate by: the epsilon is expressed as a minimum GRADE (a slope percentage), not as a raw
              vector magnitude, so it is a number a surveyor can read and argue with. Tested at and
              either side of it.
```

```
ASSUMPTION-3: A triangle `AssignBand` places in no band (its value is above the table's top) draws
              in the style's PLAIN Triangles colour, rather than being dropped or drawn transparent.
              Same rule for an arrow whose grade matches no `arrowBand`, drawn in the surface's own
              ("ByLayer") colour.
- Because:    REQ-072 does not state what an out-of-range value does, and AssignBand's own contract
              (util/surfaceanalysis.hpp) already refuses to clamp it into the top band — "a caller
              that draws the zero vector draws an arrow pointing east" is ADR-035 (b)'s lesson, and a
              triangle that just vanishes when a table does not span the surface's range is the same
              failure shape: silent, and easy to mistake for a bug in banding rather than in the
              table. Drawing it in the plain colour keeps every triangle visible under every
              range-table state, including the empty-table case TASK-086 §6's test approach names.
- Risk if wrong: a user who expects an unbanded triangle to disappear (Civil 3D shows nothing above
              the top band) will see it in the plain colour instead and may read that as a bug.
- Validate by: `req072-surface-analysis.txt` asserts the OVERFLOW bucket's total is never dropped
              (a table entirely below the surface's range still bands all 982 triangles); the colour
              choice itself is a judgement call to show the user on real data, per ASSUMPTION-1.
```

```
ASSUMPTION-4: A slope arrow is drawn as three GL_LINES segments — a shaft from the triangle's
              centroid toward downhill, sized to a fraction of the triangle's own footprint
              (`sqrt(planArea) x 0.5`), plus two head barbs swept back 25 degrees — rather than a
              fixed world-length arrow or a filled triangular arrowhead.
- Because:    REQ-072 says arrows must show direction and be "coloured by grade"; it does not specify
              a length or a head shape. A fixed length would swallow neighbouring arrows on a fine
              TIN or vanish on a coarse one; a filled head would need the same triangle-batch path as
              bands rather than the existing line path, for a purely cosmetic gain.
- Risk if wrong: the arrowhead may read as too small/large at extreme TIN densities the REQ-100
              profile does not exercise (it uses one representative density, not a range).
- Validate by: show the user actual banded/arrowed output on real data (ASSUMPTION-1's validation
              step covers this too), and adjust `kArrowShaftFraction` / `kArrowHeadFraction`
              (`CadCommands.cpp`) if so — both are named constants, not inlined magic numbers.
```

## 6. Plan

### Approach

**(1) `util/surfaceanalysis` — pure, GL-free, tested first**, beside `contourgen` and `tinbuild`
(ADR-028 (c)):
- `TriangleCentroidZ`, `TrianglePlaneSlopePct`, `TriangleDownhillDirection` — the last returning a
  validity flag rather than a zero vector, so a flat triangle is a distinguishable answer and not a
  direction that happens to be `(0,0)` (the ADR-035 (b) lesson: silent is worse than wrong).
- `AssignBand(value, breakpoints)` — the `[lo, hi)` rule from Q2, one function, one place, tested at
  the breakpoints themselves.

**(2) The range table on `SurfaceStyle`** (TASK-085's struct): `analysisMode { None, Elevation, Slope }`,
a `std::vector<SurfaceBand> { double upperBound; std::string color; }`, `slopeArrowsOn`, and the arrow
grade-colour ramp. `.gs` additive; a legacy style loads with banding off, which is REQ-072's
"turning banding off restores the style's plain display unchanged" as the default rather than a state
that has to be reached.

**(3) Render.** The display-geometry cache gains **coloured triangle batches** (one per band) and an
**arrow line batch**, each **stride-3 interleaved XYZ** (§11 invariant 8 — no sidecar Z array, and a
band batch is exactly the kind of "it's just colours" addition where one gets added). Drawn with the
existing unlit line program, one `glUniform4f` +
`glDrawArrays(GL_TRIANGLES)` per band — ADR-036 (g). Depth test on, drawn before the linework so
contours and CAD geometry read on top of the banding rather than z-fighting it, matching what the mesh
path already does for the same reason.

**(4) The legend + the Analysis tab.** The legend is an ImGui overlay in the viewport reading the same
range table the display geometry read — **not a copy of it**, so REQ-072's "the legend's displayed
ranges equal the table's, and change with it" cannot drift. One source, two readers.

### Files / functions to touch

| File | Change |
|---|---|
| `util/surfaceanalysis.hpp/.cpp` | **new**, pure. |
| `commands/CadEntities.hpp` | `SurfaceBand`; the analysis fields on `SurfaceStyle`. |
| `commands/CadCommands.cpp` | the cache gains band triangle batches + the arrow batch. |
| `io/GsIo.cpp` | additive analysis fields. |
| `render/ViewportRenderer.cpp` | draw the band batches + arrows. |
| `ui/CadUi_SurfaceStyles.cpp` | the Analysis tab (Elevations, Slopes, Slope Arrows). |
| `ui/CadUi.cpp` | the legend overlay. |
| `tests/` | `SurfaceAnalysisTests`. |

### Test approach

- **Happy path:** on a hand-computed planar tilted TIN, every arrow points the same direction and
  that direction matches the hand-computed downhill vector within REQ-101; a triangle of known
  elevation and of known slope each take the prescribed band colour.
- **Failure modes:**
  - **a value exactly on a breakpoint** lands in the defined band (Q2's `[lo, hi)` rule), asserted
    directly rather than inferred;
  - the maximum value in the table has a band (the topmost band is closed at its top);
  - a **perfectly flat** triangle yields **no** direction — asserted on the validity flag, not on a
    zero-length vector;
  - a triangle at exactly the flat-epsilon grade, and one either side of it (ASSUMPTION-2);
  - turning banding off restores the plain display **unchanged** — asserted against the batches
    produced before banding was turned on, not merely "banding is absent";
  - a range table with **zero bands**, and one with a **single** band, both render without crashing;
  - a legacy `.gs` loads with banding off.

### Steps

- [x] 1. `util/surfaceanalysis` + `SurfaceAnalysisTests` — green before anything draws.
- [x] 2. The range table on the style + `.gs` round-trip. Struct, equality and both I/O paths done;
        the round-trip is now asserted end-to-end in `req072-surface-analysis.txt` (§8).
- [x] 3. Band triangle batches + arrows in the cache; renderer draws them.
- [x] 4. The Analysis tab.
- [x] 5. The legend overlay, reading the table directly.
- [~] 6. Re-run the REQ-100 surface profile with banding + arrows on. Analytical case made in §9's
        performance-review; the empirical GPU re-measurement (`BENCH SURFACE 100000` in the real
        windowed app, discrete GPU forced) is a recommended follow-up rather than a blocker — see §9.
- [x] 7. Self-verification (§9).

## 7. Workflow-specific notes

- Feature: pre-flight answered (Q1); Q2 answered inside the boundary and written down.
- **Blocked on TASK-085** — the range table lives on `SurfaceStyle`, and the batches live in the cache
  TASK-085 builds. Starting this first would mean building both twice.

## 8. Implementation log

- 2026-08-21 opened; ADR-036 (g) + D-2026-08-21-a recorded before planning. Status blocked on TASK-085.


- 2026-08-22 **step 1 done — `util/surfaceanalysis` + `SurfaceAnalysisTests`, 12 cases green.**
  Four functions, pure and `<vector>`-only, registered in all three CMake places `contourgen` occupies.
  Full suite 491 cases / 214,350 assertions; ctest 519/519.

  Two decisions inside the boundary, both written at the function rather than left to emerge:

  * **Q2's `[lo, hi)` rule is the search, not a chain of comparisons.** `AssignBand` is a
    `std::upper_bound`, which returns the first bound strictly greater than the value — which IS the
    half-open rule, so a value on a breakpoint lands in the band above without the rule being
    re-stated anywhere. The topmost band is closed at its top as a single explicit special case, for
    the reason REQ-072 needs it: a range table is built to SPAN the surface, so the highest point sits
    exactly on the last bound and would otherwise be the one unpainted triangle on every surface.
    A value above the table returns -1 rather than clamping into the top band — clamping would hand
    the caller a colour that misreads against the legend, which is the one thing REQ-072 forbids.
  * **The flat threshold's boundary is defined AT the value.** A grade exactly equal to
    `flatGradePct` is flat (no arrow), because the constant names the grade a drawing stops meaning
    to show a direction for. Written as `!(gradePct > flatGradePct)` so a NaN grade is refused too —
    spelled `<=` it would have been admitted.

  **The downhill division is by the SIGNED nz, and that is load-bearing.** Reversing a triangle's
  winding negates the whole normal, so dividing by the signed component cancels the two sign flips
  and the fall direction comes out the same. Taking `abs` there instead reverses every arrow on
  whichever half of the triangulation is wound the other way — and a surface where half the arrows
  point uphill still looks plausible at a glance.

  **The two rules above were mutation-checked, not merely asserted.** Swapping `upper_bound` for
  `lower_bound` fails the breakpoint cases (`0 == 1`, `1 == 2`); dividing by `abs(nz)` fails the
  winding case (`1.0 == Approx(-1.0)`). Both were restored and the suite re-run green. A boundary
  test that passes because a division rounded its way is not a boundary test, so the numbers in the
  exact-equality assertions are binary-exact fractions (run 2, rise 1 → exactly 50%) on purpose.

  ASSUMPTION-2 is now expressed in code as `kFlatGradePctDefault = 0.1` (%), a grade rather than a
  vector magnitude, as the assumption required. ASSUMPTION-1 is still open — it says to show the user
  the banded display on real data before treating it as settled, which cannot happen until step 3.

- 2026-08-22 **step 2 — the range table is on the style; the round-trip assertion is not yet possible.**
  `SurfaceBand` + `SurfaceAnalysisMode` in `CadEntities.hpp`, four fields on `SurfaceStyle`
  (`analysisMode`, `bands`, `slopeArrowsOn`, `arrowBands`), and both `.gs` paths in `GsIo.cpp`.

  * **Only the TOP of each band is stored.** Storing both ends would admit a table whose bands
    overlap or leave a gap — a value with two colours, or none — and `AssignBand` reads exactly this
    list. The lowest band having no bottom is the rule, not an omission.
  * **One `bands` table whose meaning `analysisMode` sets**, not one table per mode. A triangle has
    one colour (ASSUMPTION-1), so a second table could only ever be the one NOT on screen, and the
    legend would have to guess which it was describing. `arrowBands` is separate because arrows are
    always graded by SLOPE while `bands` may be showing elevation — same `SurfaceBand` type, same
    `AssignBand` rule, so an arrow's colour is decided exactly as a band's is.
  * **Off is the state a style STARTS in.** That is how REQ-072's "turning banding off restores the
    style's plain display unchanged" is satisfied without a legacy branch in the reader: a `.gs`
    written before REQ-072 carries none of these keys, and each style is seeded from
    `StandardSurfaceStyle()` before the keys are read.
  * **The analysis keys are written only when the style carries some**, so a pre-REQ-072 drawing —
    and any style that never opens the Analysis tab — still resaves byte for byte. The section-level
    rule TASK-085 used against BUG-015/BUG-019, applied per style.
  * **A file's bands are sorted on read.** Each band carries its own colour, so ordering them repairs
    a hand-edited or corrupt table without repainting anything, and `AssignBand`'s strictly-ascending
    precondition then holds for any file. An unrecognised `analysisMode` degrades to None rather than
    to an enum value no switch handles.
  * The four fields joined `operator==`, which is the display cache's staleness key (ADR-036 (e)).
    Tested on both halves of a band — recolouring one and moving its edge are the two edits a user
    makes, and **neither changes the band count**, so a count-only comparison would miss both.

  **Gap, stated rather than glossed: the `.gs` round-trip is not asserted yet.** `GsIo.cpp` cannot be
  linked by `GoSurveyTests` (it pulls in the whole command layer — the reason `MeshGsRoundTripTests`
  exercises the serializer directly), and REQ-072's values cannot be set from a transcript until the
  Analysis tab or a `SURFSTYLE` subcommand exists. So what is proven today is the DEFAULTS half —
  a style starts with analysis off, and equality notices every new field — and not the file half.
  **Removal condition: at step 4, extend `req070-surface-styles-contours.txt` (or a sibling) to set
  bands, save, reload and compare, plus a resave-idempotence step.** Step 2 stays `[~]` until then.

  Full Catch2 suite green: **492 cases / 214,362 assertions**. `ctest` deliberately NOT re-run this
  round — another session is driving `gosurvey_headless` in this same tree, and the transcript tests
  share `build/headless-out`. It must be run before step 2 is closed.
  **Verified again on a clean base 2026-08-22.** The suite run above shared a working tree with
  TASK-087's then-uncommitted changes, which proves the two coexist but not that step 2 stands alone.
  So the commit was cherry-picked onto `upstream/beta` in a separate worktree (`84e3843` on `16385ea`,
  after step 1 merged as PR #75), built from scratch and re-run: **492 cases / 214,362 assertions**,
  byte-identical to the shared-tree figure.

  **`ctest` run and green, 2026-08-22 — the last outstanding item above is now closed.** The reason it
  was deferred was that another session was driving `gosurvey_headless` in this tree and the transcript
  tests share `build/headless-out`; that session's work (TASK-087 / REQ-071) merged as PR #76, so the
  tree is no longer shared. Re-run on this branch — step 2 cherry-picked onto `upstream/beta` at
  `6bf6b71`, which already carries step 1 (#75) and REQ-071 (#76) — **521/521 ctest**, with the Catch2
  figure unchanged at 492 cases / 214,362 assertions.

- 2026-08-23 **step 3 — band triangle batches + slope arrows in the display cache; the renderer draws
  them.** `BuildSurfaceAnalysisGeometry` (`commands/CadCommands.cpp`, beside `AppendTriangleEdges`)
  walks the triangulation once, widening each triangle to `AnalysisTriangle` (§11.8's double-over-
  float rule) and bucketing it by `AssignBand` into `SurfaceDisplayCacheEntry::bandTriangleBuffers`
  (one per `style.bands` entry **plus one overflow bucket** — ASSUMPTION-3) and, independently,
  building a 3-segment arrow (shaft + two head barbs — ASSUMPTION-4) into `arrowLineBuffers` bucketed
  by `arrowBands` the same way. Both are `GL_TRIANGLES`/`GL_LINES` stride-3 interleaved XYZ, per
  FINDING-5.

  `CadSurfaceDisplayGeometry` gained `bandTriangles` (a new `SurfaceTriangleBatch` — no lineweight
  field, since a fill has none) drawn **first**, before every other surface component, so the
  wireframe/contours/border/arrows all read on top of the opaque interior rather than under it.
  `ViewportRenderer.cpp` draws it with the same `lineProgram_` everything else in this pass uses —
  ADR-036 (g)'s whole point: a lit shader would stop a triangle from showing the colour its band
  prescribes.

  `ResolveComponentBatch`'s colour resolution was extracted into `ResolveSurfaceStoredColorRgba`
  (shared by band/arrow colours — two present-day uses, CLAUDE.md rule 2) rather than duplicated a
  third time; `ui/CadUi.cpp`'s legend (step 5) still keeps its OWN copy because that one is a
  different translation unit reading the same public resolvers, and a third copy inside
  `CadCommands.cpp`'s anonymous namespace would not have helped it.

  Full suite green after this step: **`GoSurveyTests` 510/510 (214,558 assertions)**, `ctest` 539/539
  active (one pre-existing disabled oracle, unrelated — issue #63).

- 2026-08-23 **step 4 — the Analysis tab.** `ui/CadUi_SurfaceStyles.cpp` gained a `Type` combo
  (None/Elevation/Slope → `analysisMode`) and a `BandTableEditor` shared by `bands` and `arrowBands`:
  rows of (upper bound, colour swatch + combo, Remove), an "Add band" button, and a **re-sort on every
  edit** rather than a refusal — the same repair `GsIo` applies to a hand-edited `.gs`, so a table
  built by typing bounds out of order is never in a state `AssignBand`'s precondition forbids. A
  separate `Show slope arrows` checkbox plus its own `arrowBands` table, per REQ-072's "independent
  toggles". The tab-list doc comment at the top of the file was updated to move Analysis from
  "absent" to "built", naming Directions (not Analysis) as the still-omitted section (ADR-036 (i)).

- 2026-08-23 **step 5 — the legend overlay.** `ui/CadUi.cpp` gained `DrawSurfaceAnalysisLegend`,
  called once per frame beside the ViewCube (model space only — REQ-025 (g)): for every VISIBLE
  surface whose resolved style has banding on, a small stacked panel in the viewport's bottom-left
  corner, reading `SurfaceStyle::bands` **directly** — "one source, two readers" with
  `BuildSurfaceAnalysisGeometry`, which is what makes "the legend's displayed ranges equal the
  table's, and change with it" true by construction. Multiple banded surfaces stack their own boxes
  rather than sharing one, since REQ-072 does not name a single "active" surface to legend for. Each
  row's swatch resolves the SAME ByLayer chain the fill uses (`ResolveSurfaceBandLegendRgba`, a
  second copy of `ResolveSurfaceStoredColorRgba` for the reason step 3's log entry gives), so the
  swatch and the on-screen fill cannot show two different colours for "ByLayer".

- 2026-08-23 **command-layer coverage for the headless driver, and the deferred round-trip closed.**
  The Analysis tab is an ImGui window `gosurvey_headless` cannot reach — the same limitation
  `req070-surface-styles-contours` works around with `SURFSTYLE INTERVAL`/`SHOW`/`HIDE` — so
  `ExecuteSurfStyleCommand` gained `ANALYSIS`/`BAND`/`ARROWBAND`/`CLEARBANDS`/`CLEARARROWBANDS`/
  `ARROWS`, each calling the exact fields the tab edits and re-sorting exactly as the tab does.
  `HeadlessDriver.cpp` gained `EXPECT SURFACEBANDTRIS` / `SURFACEARROWSEGS` / `SURFACEBANDBATCHES`
  (surface 0's cache-entry totals and the renderer's band-batch count) and `DUMP SURFACES` now prints
  both, matching how every other surface count in that dump was read off the running program rather
  than guessed.

  `tests/headless/transcripts/req072-surface-analysis.txt` (105 steps) is the end-to-end proof unit
  tests cannot reach: turning banding on GENERATES geometry and off RELEASES it without touching the
  triangulation (`SURFACETINGEN` unmoved throughout); a table that does not span
  `samples/surface-demo.gs`'s 103.52-136.70 ft range still bands all 982 triangles rather than
  dropping the part above it (ASSUMPTION-3, both directions — a table entirely above AND entirely
  below the range); a real two-bucket split (not one bucket happening to be empty) for both elevation
  and slope modes at thresholds chosen inside the fixture's actual range; arrows are graded by their
  OWN `arrowBands` independently of `bands`; a style edit is one undo step; the two invalid-input
  refusals (`ANALYSIS upsidedown`, `ARROWS maybe`) leave the style unchanged; and the **`.gs`
  round-trip TASK-086 §8 left open** — save, reopen, same counts, resave byte-identical — is now
  asserted rather than owed. All real numbers (982 triangles, 2,946 arrow segments, the 1-vs-2 batch
  counts) were read off the running program with `--print-log`, per this corpus's own convention,
  not guessed.

  Full suite after this step: **`GoSurveyTests` 510/510 (214,558 assertions unchanged — no unit-level
  code touched)**, `ctest` **540/540 discovered, 539 active** (the new transcript plus the pre-existing
  disabled oracle), all green.

## 9. Self-verification
- [x] build-project — `GoSurvey.exe`, `gosurvey_headless.exe`, `GoSurveyTests.exe` all build clean
      (MSVC, pinned preset) with no new warnings beyond this build's pre-existing baseline.
- [x] architecture-review — **the ADR-028 (h) amendment (ADR-036 (g)) is exactly as decided at plan
      review**: the band fill draws on `lineProgram_`, no new shader, no new uniform, no per-vertex
      colour attribute. `SurfaceTriangleBatch`/band and arrow buffers are stride-3 interleaved XYZ
      throughout (§11 invariant 8, FINDING-5 satisfied). No new abstraction, dependency, layer, or
      public-API/data-format change beyond the additive `.gs` fields step 2 already covered — the
      SURFSTYLE verbs and the legend are UI/Commands-layer surface over existing Domain fields, not
      new authority.
- [x] code-review — self-reviewed: `AssignBand`'s documented contract (half-open, `-1` above the
      table, never clamped) was checked directly against `util/surfaceanalysis.cpp`'s implementation
      before relying on it in the batching pass; the sort-on-every-mutation invariant is enforced at
      all three entry points a band table can change through (Analysis tab, `SURFSTYLE BAND`,
      `GsIo` load); the downhill-arrow's signed-`nz` sign convention from step 1 is unchanged by
      steps 3-5, which only consume the vector `TriangleDownhillDirection` already returns.
- [x] dependency-audit — n/a, nothing added.
- [x] performance-review — **PASS by analysis, empirical GPU re-measurement recommended but not
      blocking.** `BuildSurfaceAnalysisGeometry` is one O(triangles) pass writing at most 9 floats
      (a band) plus 18 floats (three arrow segments) per triangle — less per-triangle output than
      `AppendTriangleEdges`'s existing 18 floats (three edges), which REQ-100 profile (c) already
      measured at **p95 10.28 ms against the 16 ms budget** (TASK-052/053) with triangles OFF by
      default; banding/arrows add a bounded number of extra draw calls (one per non-empty bucket,
      typically single digits) on the same cheap unlit program every other surface batch already
      uses. This is the same class of addition REQ-070's border/contour batches were, not a new cost
      profile. The **empirical** re-measurement REQ-100 actually requires — `BENCH SURFACE 100000` in
      the real windowed app with the discrete GPU forced, banding + arrows switched on — needs the
      reference machine's GPU and is recorded as a recommended follow-up (task step 6, left `[~]`)
      rather than claimed here without having run it.
- [x] testing — `GoSurveyTests` 510/510 (214,558 assertions); `ctest` 539/539 active (540 discovered,
      one pre-existing disabled oracle unrelated to this task); `req072-surface-analysis.txt` (105
      steps) covers every REQ-072 acceptance condition reachable without the ImGui dialog itself, plus
      the round-trip TASK-086 §8 left open.

## 10. Verification result

### Plan review — 2026-08-21 (workflow §3, before implementation)

```
REVIEW VERDICT — TASK-086 plan — 2026-08-21
- Outcome:   PASS (plan stage; implementation not yet reviewed)
- Domains:   arch ✓   quality ✓   deps ✓   perf ✓
- Findings:  0 blocking, 1 advisory (FINDING-5)
```

```
FINDING-5
- Severity:  advisory
- Domain:    architecture
- Location:  TASK-086 §6, "(3) Render"
- Violates:  architecture §11 invariant 8
- Observed:  The band and arrow batches did not state their coordinate layout. Every other flat
             geometry store in the codebase is interleaved XYZ, and a per-band colour batch is
             exactly where someone reaches for a parallel array "because it's just colours."
- Required:  Stated. Added.
```

Noted for the implementation review, not a finding: ADR-036 (g) **amends ADR-028 (h)**. A reviewer
reading ADR-028 alone will see this render path as an undecided deviation. It is a decided one, and
§9 carries the reminder.

### Implementation review — 2026-08-23 (self-run per §9; steps 3-8)

```
REVIEW VERDICT — TASK-086 implementation — 2026-08-23
- Outcome:   PASS
- Domains:   build ✓   arch ✓   quality ✓   deps ✓   perf ✓ (analytical; empirical GPU re-measurement
             recommended, not blocking — see §9)   testing ✓
- Findings:  0 blocking, 0 advisory
```

FINDING-5 from plan review is closed: `SurfaceTriangleBatch`, `bandTriangleBuffers` and
`arrowLineBuffers` all state and hold stride-3 interleaved XYZ.

## 11. Outcome

COMPLETION REPORT — TASK-086 — 2026-08-23
- Requirements satisfied:  **REQ-072** (Acceptance met: yes — all five conditions demonstrated in
                           `req072-surface-analysis.txt` and `SurfaceAnalysisTests`: per-triangle
                           banding by elevation and by slope including exact-breakpoint behaviour,
                           the legend reading the table directly, arrow direction matching the
                           hand-computed downhill vector (step 1), no arrow on a flat triangle, and
                           banding-off restoring the plain display unchanged).
                           **REQ-070** (the style carries the band/arrow settings — Acceptance met:
                           yes, `.gs` round-trip now asserted end-to-end).
- Summary:                 Surfaces can be coloured per-triangle by elevation or slope band, with an
                           on-screen legend and independent downhill slope arrows graded by their own
                           colour ramp, all editable from the Surface Style dialog's new Analysis tab
                           and from the `SURFSTYLE` command family.
- Tests:                   `SurfaceAnalysisTests` (12 cases, step 1, unchanged this round);
                           `tests/headless/transcripts/req072-surface-analysis.txt` (105 steps) —
                           happy path (banding on/off, real two-way splits in both modes, arrows,
                           arrow-band grading, undo) and failure modes (a table that does not span
                           the surface's range, invalid ANALYSIS/ARROWS arguments, zero-band table).
                           `GoSurveyTests` 510/510 (214,558 assertions); `ctest` 539/539 active.
- Verification verdict:    PASS (plan review 2026-08-21: 0 blocking, 1 advisory closed; implementation
                           review 2026-08-23: 0 blocking, 0 advisory).
- Assumptions:             ASSUMPTION-1 (one colour per triangle) — open, validate on real data in
                           the running app. ASSUMPTION-2 (flat threshold as a grade) — closed, tested.
                           ASSUMPTION-3 (unbanded → plain colour) — open, same validation step as
                           ASSUMPTION-1. ASSUMPTION-4 (arrow shaft/head shape and sizing) — open, same
                           validation step.
- Architectural decisions: none made by Workshop this round. ADR-036 (g) (amending ADR-028 (h)) was
                           decided at plan review, before this round's implementation began.
- Dependencies:            none added.
- Technical debt noted:    none new. The two limits step 1 already recorded stand: the linear
                           per-triangle scan (no measured problem behind indexing it) and the
                           single-representative-value-per-triangle banding (ASSUMPTION-1).
- Build:                   reproducible, clean on MSVC via the pinned preset; no new warnings.
- Docs updated:            this task log; `ui/CadUi_SurfaceStyles.cpp`'s tab-list doc comment
                           (Analysis moved from "absent" to "built"); `spec/roadmap.md` M-Surfaces
                           step 7 (still to record — see note below).

**Recommended follow-up, not a blocker:** run `BENCH SURFACE 100000` in the real windowed app with
the discrete GPU forced and Standard's Analysis tab set to Elevation banding + slope arrows on, and
record the p95 beside REQ-100 profile (c)'s existing 10.28 ms figure. The analytical case for staying
in budget is made in §9; this is the empirical confirmation REQ-100's acceptance actually asks for.
