# TASK-086 — Surface analysis: elevation banding, slope banding, slope arrows, and the legend

- Type:    feature
- Status:  plan  (blocked on TASK-085 — the style table is where the range table lives)
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

- [ ] 1. `util/surfaceanalysis` + `SurfaceAnalysisTests` — green before anything draws.
- [ ] 2. The range table on the style + `.gs` round-trip.
- [ ] 3. Band triangle batches + arrows in the cache; renderer draws them.
- [ ] 4. The Analysis tab.
- [ ] 5. The legend overlay, reading the table directly.
- [ ] 6. Re-run the REQ-100 surface profile with banding + arrows on.
- [ ] 7. Self-verification (§9).

## 7. Workflow-specific notes

- Feature: pre-flight answered (Q1); Q2 answered inside the boundary and written down.
- **Blocked on TASK-085** — the range table lives on `SurfaceStyle`, and the batches live in the cache
  TASK-085 builds. Starting this first would mean building both twice.

## 8. Implementation log

- 2026-08-21 opened; ADR-036 (g) + D-2026-08-21-a recorded before planning. Status blocked on TASK-085.

## 9. Self-verification
- [ ] build-project
- [ ] architecture-review — **note the ADR-028 (h) amendment explicitly**; a reviewer reading ADR-028
      alone would flag this render path as a deviation, and it is a decided one.
- [ ] code-review
- [ ] dependency-audit — n/a
- [ ] performance-review — **required**; per-triangle work over the REQ-100 surface profile
- [ ] testing

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

- Implementation review: **not yet submitted** (blocked on TASK-085).

## 11. Outcome
- —
