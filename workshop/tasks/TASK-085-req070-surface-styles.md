# TASK-085 — Surface styles: the style table, contour generation, and the Surface Style editor

- Type:    feature
- Status:  plan  (**blocked on TASK-084** — the cache is keyed by a surface's stable entity id, which
           does not exist until ADR-036 (a) lands; see FINDING-2)
- Opened:  2026-08-21
- Owner:   Workshop

## 1. Authority

- Goal:         GOAL-05 (terrain modelling — M-Surfaces). Roadmap step 6 (`spec/roadmap.md:71`).
- Requirements: **REQ-070** (accepted) — surface styles.
                **REQ-201** (accepted) — a rejection states what it did.
                **REQ-100** (accepted) — the frame budget contour regeneration must not break.
- Constraints:  CON-07; architecture §11.5 (nothing heavy in the undo snapshot); §11.4 (a `util/`
                module stays dependency-free).
- Authority for the architectural shape: **ADR-028 (b) (c) (h)** + **ADR-036 (d) (e) (f) (h) (i)** +
  decision **D-2026-08-21-a**.

### Acceptance (restated verbatim from REQ-070)

- "changing the contour interval updates the display **without rebuilding the triangulation** and adds
  no entity to the drawing or to the saved `.gs`"
- "two surfaces sharing a style both change when the style is edited"
- "a style with triangles off and contours on draws only contours; with both off and border on, only
  the border"
- "a major interval that is not a whole multiple of the minor interval is rejected with a specific
  message rather than producing mis-labelled contours"
- "styles round-trip `.gs`; a legacy `.gs` loads unchanged; a surface whose style was deleted falls
  back to a default style rather than failing to draw"

Plus the statement's standing rules: "**Contours are display geometry, not entities**: they are
regenerated from the triangulation and the style, are never stored in `.gs`, never appear in
selection, and never appear in the drawing's entity counts."

- Owning subsystem: **util** (contour generation — pure), **Domain** (`commands/`, the style table and
  the display-geometry cache), **IO** (`.gs`), **Renderer** (draw), **UI** (the editor dialog).

## 2. Scope

- **In scope:** a document-owned `surfaceStyles` table; `CadSurface::styleName`; a pure
  `util/contourgen` marching-triangles generator; a per-surface display-geometry cache keyed on
  `(tin pointer, style revision)`; the components **triangles, border, major contours, minor
  contours, points**, each with visible / colour / linetype / lineweight; the Surface Style dialog
  with tabs **Information, Borders, Contours, Points, Triangles, Display, Summary**; a style dropdown
  in the Surface Manager; `.gs` round-trip; the `RenderScene` parameter replacement.
- **Out of scope, each for a stated reason:**
  - **Analysis tab** — TASK-086 (REQ-072). Its tab is present and its rows are drawn there, not here.
  - **Grid** and **Watersheds** tabs — no requirement; a SPEC GAP, refused by D-2026-08-21-a.
  - **Contour smoothing** — ADR-028 leaves it undesigned; the Civil 3D slider is not built.
  - **Contour labelling** — no requirement.
  - **Per-component Layer / Plot Style columns** — a surface has one layer; there is no plot-style
    table. ADR-036 (i).
  - **REQ-071 EXTRACT** — deferred to its own task by the user.
  - **3D Geometry / "Flatten to elevation" / "Exaggerate by scale factor"** rows visible in the
    reference screenshots — no requirement. Not built, and **not drawn as disabled rows either**: a
    greyed control the product will never honour is a promise, and REQ-201's spirit is that the
    product says what it does.
- **Smallest change:** one new table (the `TextStyle` pattern, already proven), one new pure module,
  one cache, one dialog. No change to `CadTin`, to triangulation, or to the rebuild worker — REQ-070's
  central constraint is that a style edit does not reach any of them.

## 3. Architectural boundary check  (workflow.md §4)

- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **Yes → escalated and RESOLVED before planning.** A new document-owned store, a new `.gs`
      section, a new `util/` module, a changed `RenderScene` signature, and the marching-triangles
      algorithm are all architectural. Escalated as a SPEC GAP and decided as **ADR-036 (d) (e) (f)
      (h) (i)** + **D-2026-08-21-a**, 2026-08-21. ADR-028 (b) (c) (h) had already pre-decided the
      shape; ADR-036 records the concrete form and **amends ADR-028 (h)** on the shader point (see
      TASK-086 §3 — the amendment bites there, not here).
- **No new dependency**: `contourgen` is in-tree, beside `tinbuild`, for ADR-028 (c)'s reasons.
- **No new global**: the style table hangs off the document exactly as `drawingLayerTable` and the
  `TextStyle` table do.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Which of the ten Civil 3D Surface Style tabs are in scope? | 2026-08-21 | "Spec scope + Analysis". Grid and Watersheds refused as a SPEC GAP. |
| Q2 | Is REQ-071 (contour EXTRACT) in this task? | 2026-08-21 | No — deferred to a follow-up so a FAIL here does not block it. |
| Q3 | REQ-070 requires a major interval that is not a whole multiple of the minor to be **rejected**. Rejected at entry (the field refuses the value) or at generation? | — | **Answer before implementing step 5.** Recommendation: reject at entry with the specific message, so the invalid state never exists to be generated from. Raise with the user only if entry-time rejection turns out to block a legitimate editing sequence (e.g. typing "10" over "5" digit by digit). |

## 5. Assumptions

```
ASSUMPTION-1: A contour level that coincides exactly with a vertex elevation is treated as
              infinitesimally ABOVE that vertex, uniformly, at every edge of every triangle.
- Because:    REQ-070 does not name the degenerate case, but REQ-072 states the obligation for the
              analogous one ("the band a value falls into is defined and tested rather than left to
              float comparison"), and the same reasoning governs here.
- Risk if wrong: nothing wrong, but if the rule is applied per-edge rather than uniformly, a contour
              passes through a vertex on one triangle and not its neighbour — a half-open contour,
              which looks like a triangulation bug and is not one.
- Validate by: a unit test with a level exactly on a vertex, asserting the contour is closed and
              the triangles either side agree.
```

```
ASSUMPTION-2: Regenerating contours only when the (tin pointer, style revision) key changes keeps
              contour cost off the per-frame path entirely.
- Because:    ADR-028 predicted contour regeneration as a third REQ-100 profile; ADR-036 (e) is the
              mechanism that answers it.
- Risk if wrong: a 200k-triangle surface at a 1 ft interval regenerated per frame would blow the
              REQ-100 budget outright.
- Validate by: the REQ-100 bench case measures the CACHE HOLDING across frames, not one regeneration.
              A profile that only times a single generation would pass while the defect is present.
```

## 6. Plan

### Approach

Four separable pieces, in dependency order. Each is independently buildable, so a FAIL in one does not
strand the others.

**(1) `util/contourgen` — pure, GL-free, unit-tested first.**
Marching triangles: for each triangle and each contour level between its min and max Z, the level
crosses exactly two of its three edges (given ASSUMPTION-1's tie rule, which removes the third case);
linear interpolation gives the two endpoints. Segments are chained into polylines through shared
edges, so the output is contours rather than a segment soup. Input is `CadTin`'s arrays and a level
list; output is flat XYZ verts + per-contour offsets, the same shape `userPolylineVerts` /
`userPolylineOffsets` already use — which is what makes REQ-071's later EXTRACT nearly free.
**Written and tested before anything draws it.**

**(2) The style table.**
`SurfaceStyle` in `CadEntities.hpp`: name; per-component `SurfaceComponentStyle { bool visible;
std::string color; std::string linetype; float lineweightMm; }` for **triangles, border, majorContour,
minorContour, points**; `minorInterval`, `majorInterval`; and the REQ-072 fields TASK-086 fills.
`std::vector<SurfaceStyle> surfaceStyles` on the document, threaded exactly as the `TextStyle` table
is. `CadSurface::styleName`, resolved **on read** (ADR-036 (d)) with a built-in "Standard" fallback,
so a deleted style draws rather than fails.

**(3) The display-geometry cache + renderer.**
`CadSurfaceDisplayGeometry`: per-component coloured line batches (and, from TASK-086, coloured
triangle batches), each stride-3 XYZ (§11 invariant 8). Built by a `RefreshSurfaceDisplayGeometry(st)`
called from the same per-frame maintenance point as `TickSurfaceRebuilds`, and **early-returning on an
unchanged staleness key before any allocation** — that early return is the REQ-100 mechanism, not an
optimisation, and it is worthless if it runs after a `clear()`/`reserve()` (§11 invariant 7).
`RenderScene`'s `surfaceEdges` parameter is **replaced** by `const CadSurfaceDisplayGeometry*`
(ADR-036 (h)).

**Placement is the load-bearing detail here — ADR-036 (e), raised as FINDING-1 and FINDING-2 against
the first draft of this plan:**
- `std::vector<SurfaceDisplayCacheEntry>` **on `AppCommandState`**, keyed by the surface's **stable
  entity id**. **Not a member of `CadSurface`**: `cadSurfaces` is assigned wholesale into the document
  and into every geometry snapshot (`CadCommands.cpp:88, 148, 1126, 1190`), so a field on the surface
  would be deep-copied into all 50 undo frames — the exact cost the cache exists to avoid, arriving
  through the one door nobody thinks to check.
- **Not keyed by array index**: `cadSurfaces` compacts on erase (§11 invariant 9), so an index key
  would start drawing one surface's contours over another's triangulation after a delete.
- Entries whose id no longer resolves are **reaped** in the same pass, so an erased surface's contours
  do not outlive it.
- The `main.cpp` function-local `static std::vector<float> surfaceEdges` block goes away with it. That
  static is per-process, not per-document, and already carries a hand-rolled `surfaceEdgesTab` guard
  because two tabs can sit at the same revision; moving the cache onto `AppCommandState` deletes the
  guard along with the class of bug it patches.

**(4) The dialog + Surface Manager wiring.**
`ui/CadUi_SurfaceStyles.cpp` — a new file beside `CadUi_Surfaces.cpp`, matching how every other panel
of this size is organised. Tabs per ADR-036 (i). A style dropdown and an "Edit style…" button in the
Surface Manager's right pane.

### Files / functions to touch

| File | Change |
|---|---|
| `util/contourgen.hpp/.cpp` | **new**, pure. `GenerateContours(verts, indices, levels, out)`. |
| `commands/CadEntities.hpp` | `SurfaceComponentStyle`, `SurfaceStyle`; `CadSurface::styleName`. |
| `commands/CadCommands.hpp/.cpp` | `surfaceStyles` on the document + `AppCommandState`; `FindSurfaceStyle` with fallback; `CadSurfaceDisplayGeometry`; `RefreshSurfaceDisplayGeometry`; **delete** `AppendSurfaceEdgeLines` (its one caller becomes the cache). |
| `io/GsIo.cpp` | additive `surfaceStyles` section + `styleName`; a legacy file loads with "Standard". |
| `render/ViewportRenderer.hpp/.cpp` | parameter replacement; draw the coloured line batches. |
| `app/main.cpp` | the `surfaceEdges` static block becomes the cache refresh call. |
| `ui/CadUi_SurfaceStyles.cpp` | **new** — the dialog. |
| `ui/CadUi_Surfaces.cpp` | style dropdown + Edit style. |
| `tests/` | `ContourGenTests`, `SurfaceStyleTests`. |

### Test approach

- **Happy path:** contours over a hand-computed planar and a hand-computed saddle TIN land at exactly
  the expected elevations, each vertex within REQ-101 of the linear interpolation on its edge; two
  surfaces sharing a style both change when the style is edited; a style round-trips `.gs`.
- **Failure modes:**
  - a major interval that is not a whole multiple of the minor is **rejected with a specific
    message** — asserted on the message, not merely on the rejection;
  - a surface whose `styleName` names a deleted style **draws with the default** rather than not
    drawing;
  - a legacy `.gs` with no style section loads unchanged and gets "Standard";
  - **changing the interval adds nothing to `.gs` and does not change the tin pointer** — this is
    REQ-070's central claim and gets its own assertion on the `shared_ptr`, in the same shape as
    REQ-068's payload-sharing test;
  - **the drawing's entity counts are unchanged by contours** — REQ-070 states this separately from
    "adds no entity to the `.gs`", and it is separately assertable: a surface with 40,000 contour
    segments displayed still reports zero polylines (FINDING-3);
  - **erasing a surface reaps its cache entry**, and the surface that compacts into its former index
    does not inherit its contours (FINDING-2's failure mode, tested rather than argued);
  - triangles off + contours on draws only contours; both off + border on draws only the border;
  - a contour level exactly on a vertex closes (ASSUMPTION-1);
  - **a surface with no tin** generates nothing and does not crash.

### Steps

- [ ] 1. `util/contourgen` + `ContourGenTests` — pure, no UI, no GL. Green before step 2 starts.
- [ ] 2. `SurfaceStyle` + the table + `.gs` round-trip + `SurfaceStyleTests`.
- [ ] 3. The display-geometry cache; `RenderScene` parameter replacement; delete `AppendSurfaceEdgeLines`.
- [ ] 4. Renderer draws the coloured component batches.
- [ ] 5. The dialog (answer Q3 first) + Surface Manager wiring.
- [ ] 6. REQ-100 bench: assert the **cache holds across frames** (ASSUMPTION-2).
- [ ] 7. Self-verification (§9).

## 7. Workflow-specific notes

- Feature: pre-flight answered (Q1, Q2); **Q3 is open and must be answered before step 5.**
- Tests-first for step 1 — a contour generator is exactly the kind of pure geometry that is cheap to
  test and expensive to debug through a viewport.

## 7a. Technical debt identified at plan time  (CLAUDE.md rule 7)

```
DEBT-1: A surface is never plotted. `src/io/PdfPlot.cpp` contains no surface or mesh handling at
        all — zero matches for either.
- Constraint forcing it: no requirement covers it. REQ-068's "a surface on a non-plottable layer is
        not plotted" is satisfied VACUOUSLY today, because nothing plots a surface on any layer.
        REQ-070 governs display, not plotting, so building it here would be work with no REQ behind
        it — which Verification would flag under §7.2, and rightly.
- Why it matters more after this task than before: contours are the deliverable on a topo plan. A
        user who styles contours on screen and then plots the sheet will get a blank one. Today the
        surface is a raw triangle mesh nobody wants on paper, so the gap is invisible; the moment
        this task lands, it is the first thing anyone hits.
- Removal condition: a requirement for plotting surface display geometry, then a task against it.
        The display-geometry cache this task builds is the right input for it — `PdfPlot` would
        consume the same coloured line batches the renderer does — so the follow-up is small, and
        deliberately not smuggled in here.
- Follow-up: raise with the user for a REQ decision. NOT silently deferred.
```

## 8. Implementation log

- 2026-08-21 opened; ADR-036 + D-2026-08-21-a recorded before planning.
- 2026-08-21 plan reviewed by Verification **before implementation** (workflow §3). FAIL — two
  blocking findings, both on cache placement (see §10). Plan corrected; ADR-036 (e) amended to carry
  the two placement rules so they bind the next reader as well as this one. DEBT-1 recorded above.

## 9. Self-verification
- [ ] build-project
- [ ] architecture-review
- [ ] code-review
- [ ] dependency-audit — n/a (in-tree generator, ADR-028 (c))
- [ ] performance-review — **required**; the REQ-100 third profile (ADR-028's own prediction)
- [ ] testing

## 10. Verification result

### Plan review — 2026-08-21 (workflow §3, before implementation)

```
REVIEW VERDICT — TASK-085 plan — 2026-08-21
- Outcome:   FAIL → corrected → PASS (plan stage; implementation not yet reviewed)
- Domains:   arch ✗→✓   quality ✓   deps ✓   perf ✗→✓
- Findings:  2 blocking, 2 advisory
```

```
FINDING-1
- Severity:  blocking
- Domain:    architecture
- Location:  TASK-085 §6, "living beside the surface in live state"
- Violates:  ADR-036 (e); architecture §11.5; REQ-070 ("never stored in .gs")
- Observed:  The plan placed the display-geometry cache on CadSurface without saying it must not be
             a member. `cadSurfaces` is assigned wholesale at CadCommands.cpp:88, 148, 1126 and 1190
             — into DrawingDocument and into every DrawingGeometrySnapshot. A cache field on the
             surface is therefore deep-copied into all 50 undo frames, which is the precise cost
             ADR-036 (e) exists to prevent. The plan as written would have satisfied its own stated
             intent and violated it in the same sentence.
- Required:  A separate parallel container on AppCommandState. Recorded in ADR-036 (e) as a rule,
             not only in this task, so the next reader inherits the reason.
```

```
FINDING-2
- Severity:  blocking
- Domain:    architecture
- Location:  TASK-085 §6 (cache key); §1 (task sequencing)
- Violates:  architecture §11 invariant 9 ("a reference from one object to another is a stable id —
             never an array index")
- Observed:  ADR-036 (e) named `(tin pointer, style revision)` as the key — that is the STALENESS
             key, and the plan never named the IDENTITY key. The obvious implementation indexes the
             cache by surface array index, and `cadSurfaces` compacts on erase: delete surface 0 and
             surface 1's entry silently becomes surface 0's, drawing one surface's contours over
             another's triangulation. This also makes TASK-085 depend on TASK-084, which the plan
             did not state.
- Required:  Key by stable entity id (TASK-084 / ADR-036 (a)); reap entries whose id no longer
             resolves; mark TASK-085 blocked on TASK-084; test the erase-and-compact case directly.
```

```
FINDING-3
- Severity:  advisory
- Domain:    correctness
- Location:  TASK-085 §6, test approach
- Violates:  REQ-070 ("never appear in the drawing's entity counts")
- Observed:  The test list covered "adds no entity to the saved .gs" but not the entity COUNT, which
             REQ-070 states as a separate condition and which a UI-side count could break on its own.
- Required:  Its own assertion. Added.
```

```
FINDING-4
- Severity:  advisory
- Domain:    performance
- Location:  TASK-085 §6, RefreshSurfaceDisplayGeometry
- Violates:  architecture §11 invariant 7
- Observed:  "Early-returning on an unchanged key" does not say the early return precedes allocation.
             An early return placed after a clear()/reserve() still allocates every frame and would
             pass a naive reading of the plan.
- Required:  Stated explicitly. Added.
```

- Implementation review: **not yet submitted.**

## 11. Outcome
- —
