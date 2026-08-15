# TASK-046 — TIN surface entity: triangulation, storage, render

- Type:    feature
- Status:  self-verify (REQ-100 surface measurement outstanding — needs a run on the reference machine)
- Opened:  2026-08-15
- Owner:   Workshop

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         M-Surfaces step 3 (roadmap) — the first step that puts a surface on screen
- Requirements: **REQ-068** (accepted 2026-08-12); **REQ-100** amendment (surface cost profile)
- Constraints:  CON — architecture §11.5 (immutable payload shared, not copied), §11.8 (interleaved
                XYZ), §11.9 (references by id/name, never index), §11.4/REQ-301 (no speculative
                abstraction), REQ-101 (±0.01 ft), REQ-200, REQ-201, REQ-300; **ADR-028**
- Acceptance:   restated verbatim from REQ-068:
  - a surface round-trips `.gs` with vertex positions bit-identical on reload;
  - a legacy `.gs` with no surface section loads unchanged;
  - surfaces are included in zoom-extents and in the drawing's bounding box;
  - erasing a surface is undoable in one step, and the restored surface is the same triangulation;
  - a surface on a frozen or off layer is not drawn, and one on a non-plottable layer is not plotted;
  - **an edit unrelated to the surface — drawing a line — does not copy the triangulation**: the
    undo snapshot shares the payload, asserted on the shared pointer rather than by inspection;
  - exporting a drawing containing a surface names the surface as excluded in the log.
- Owning subsystem: util (triangulation), Domain (store), IO (`.gs`), Renderer (draw), UI/Commands

## 2. Scope
- In scope:
  - `src/util/tinbuild.{hpp,cpp}` — **pure** Delaunay triangulation, double-precision predicates.
  - `CadTin` (immutable payload) + `CadSurface` (name, layer, style hook, source groups).
  - Storage on `AppCommandState` / `DrawingDocument` / `DrawingGeometrySnapshot`, held as
    `shared_ptr<const CadTin>` per §11.5 and the `CadMesh` precedent.
  - `.gs` additive section; DXF/DWG **logged** exclusion.
  - Selection, erase, undo, extents, layer visibility.
  - Render: triangle edges now; shaded faces reuse the REQ-064 triangle shader.
  - A `SURFACE` command + minimal creation UI: name it, pick point group(s), build.
  - REQ-100 surface bench case (100k points / ~200k triangles).
  - `TinBuildTests` — pure, GL-free.
- Out of scope (belongs to later, accepted requirements — do not build ahead):
  - **Breaklines, boundaries, and dynamic rebuild** — REQ-069. The surface gets a *minimal* source
    list (point groups) because it cannot be built without one; the ordered definition, the
    constrained triangulation and the background worker are REQ-069's and are not started here.
  - **Contours, styles, banding, arrows** — REQ-070/072. Nothing here generates display geometry
    beyond the triangles themselves.
  - Volumes (REQ-073), spot readout (REQ-074), Surface Manager (REQ-075).
- Smallest change: one pure triangulator, one entity, one `.gs` section, one draw path, one command.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **No — proceed.** Every element was decided in **ADR-028** before this task: the
      definition/triangulation split (a), display geometry not entities (b), **in-tree** constrained
      Delaunay in `util/` (c), **double predicates over float storage** (d), no DXF/DWG export (f),
      and no new abstraction (h). The `.gs` section is additive (ADR-020 (d) precedent). Sharing the
      payload is the §11.5 amendment used as intended, not a new exemption.
    - [ ] Yes → STOP.

## 4. Questions  (workflow.md §5 — ask before guessing)
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| — | none open. ADR-028 settled the design; REQ-068's acceptance is unambiguous. | — | — |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: REQ-068 builds an UNCONSTRAINED Delaunay triangulation. Constraint insertion
              (breaklines) is written in REQ-069, not stubbed here.
- Because:       ADR-028 (c) names "constrained Delaunay" for the feature as a whole, and REQ-069
                 owns breaklines. Building the constraint machinery now would be scaffolding for a
                 requirement not yet in flight (REQ-301, the anti-speculation rule).
- Risk if wrong: REQ-069 has to extend the module rather than call it. Accepted: extending a tested
                 triangulator is ordinary work, and guessing its constraint API now is not.
- Validate by:   REQ-069's task; the API returns triangles over an index-addressed vertex list,
                 which is what constraint insertion needs to operate on.

ASSUMPTION-2: Points coincident in plan (within REQ-101) are de-duplicated before triangulating,
              keeping the FIRST occurrence, and the count is reported.
- Because:       Delaunay is undefined for duplicate sites — they produce degenerate triangles or a
                 non-terminating flip loop. REQ-069 owns the *diagnostic* for conflicting
                 elevations; REQ-068 must simply not break.
- Risk if wrong: the wrong elevation wins where two shots share a plan position. Reported, not
                 silent, so the user can see it (REQ-201).
- Validate by:   TinBuildTests; revisited by REQ-069's crossing/duplicate diagnostics.

ASSUMPTION-3: A surface references its point groups BY NAME.
- Because:       groups are named and unique per drawing (REQ-067); a name is a stable identifier,
                 not an array index, so §11.9 is satisfied. This mirrors how a CadAnnotation
                 references a text style by `styleName` (ADR-020).
- Risk if wrong: renaming a group orphans the surface. Mitigated by reporting an unresolved group
                 name at build time rather than silently building an empty surface.
- Validate by:   the rename path already refuses duplicates (REQ-067); revisit under REQ-075.
```

## 6. Plan  (workflow.md §6 — write BEFORE any code)
- Approach: hardest and most testable part first — the pure triangulator with its predicates, fully
  unit tested, before any storage or pixels exist. Then the entity, following `CadMesh` field for
  field, since REQ-068's ownership and lifetime requirements are deliberately identical to REQ-063's.
- Files/functions to touch:
  - `src/util/tinbuild.{hpp,cpp}` — **new, pure**: `BuildTin(points) -> TinResult`, double
    `Orient2D`/`InCircle`, dedupe, degenerate-input diagnostics.
  - `src/commands/CadEntities.hpp` — `CadTin`, `CadSurface`.
  - `src/commands/CadCommands.{hpp,cpp}` — stores, capture/restore, tab save/restore, erase,
    extents, selection, `SURFACE` command.
  - `src/io/GsIo.cpp` — additive `surfaces` section.
  - `src/io/DxfIo.cpp` — logged exclusion (REQ-201).
  - `src/render/ViewportRenderer.{hpp,cpp}` — draw triangles.
  - `src/util/benchscene.{hpp,cpp}` — REQ-100 surface profile.
  - `tests/TinBuildTests.cpp`; `CMakeLists.txt` (both targets).
- Test approach:
  - happy path = a known point set triangulates to hand-computed triangles; every triangle is
    counter-clockwise; the Delaunay empty-circumcircle property holds for every triangle; a grid of
    N points yields the Euler-predicted triangle count; vertex Z is carried through untouched.
  - failure mode = fewer than 3 points, all-collinear points, and duplicate plan positions each
    produce a specific diagnostic and **no partial surface**; a 100k-point set terminates and stays
    within REQ-101 on interpolated elevations.
- Steps:
  - [ ] 1. `util/tinbuild` + `TinBuildTests` — triangulation correctness before anything else.
  - [ ] 2. `CadTin` / `CadSurface` entity + stores + undo sharing (assert no deep copy).
  - [ ] 3. `.gs` round trip; DXF/DWG logged exclusion.
  - [ ] 4. Extents, selection, erase, layer visibility.
  - [ ] 5. Render triangles.
  - [ ] 6. `SURFACE` command + creation UI from point groups.
  - [ ] 7. REQ-100 surface bench case; record the number.
  - [ ] 8. Self-verification (§9).

## 7. Workflow-specific notes
- Feature: no pre-flight questions outstanding. Tests-first for step 1 — geometric predicates are
  the classic place where "looks right on screen" hides a sign error that only shows on one input.

## 8. Implementation log  (append as you work)
- 2026-08-15 — opened. §3 clean: ADR-028 predates the task and settles every architectural element.
- 2026-08-15 — step 1, tests first. Predicates are pinned **directly** as well as through the
  triangles they produce: a sign error in `TinOrient2D`/`TinInCircle` is invisible on most inputs.
  The structural assertions (CCW winding, empty-circumcircle, the Euler count `2n − h − 2`) matter
  more than any hand-computed triangulation, because they hold for *every* valid Delaunay result and
  so catch wrong output on inputs nobody enumerated.
- 2026-08-15 — **The first triangulator was correct and unusable, and the tests are what made
  replacing it safe.** Bowyer–Watson scanning every triangle per inserted point is O(n²): measured
  4.8 ms at 1k, 66 ms at 4k, **1,838 ms at 16k** — extrapolating to ~70 s at the 100k points REQ-100
  specifies. Rewrote the core around **triangle adjacency**: walk to the containing triangle, flood-
  fill the cavity across shared edges, relink the fan. Insertion order is a serpentine grid
  traversal so consecutive points land near each other and the walk stays short. Result:
  **100k points → 199,957 triangles in 89 ms**, ~800× faster and near-linear (16k→100k is 6.25× the
  points for 7.3× the time). All 14 existing tests passed unchanged against the new core — which is
  the whole return on having written them before the algorithm.
  A **complexity guard** was added (50k points under 5 s; the real figure is ~40 ms, quadratic would
  be ~18 s) so this cannot silently regress. Deliberately loose to avoid flakiness, tight enough to
  catch the actual failure mode.
- 2026-08-15 — steps 2–4. `CadTin` / `CadSurface` follow `CadMesh` field for field. Two tests assert
  REQ-068's sharing condition directly on the shared pointer: 50 snapshot copies leave **one**
  payload, and a rebuild **replaces** the pointer while an earlier snapshot still sees the old
  triangulation (the §11.5 contract, stated as an executable check rather than a comment).
- 2026-08-15 — **DECISION within boundary (render as edges, not shaded faces).** The renderer's mesh
  path draws only in Shaded, by ADR-026's reasoning that a triangle soup as edges is unreadable. A
  TIN is the opposite case: its triangles *are* what a surveyor reads, and the default style is 2D
  Wireframe — a surface visible only in Shaded would be invisible in the view users spend most of
  their time in. Surface edges are therefore drawn in every style, through the existing line
  program, following the `surveyMarkers` parameter precedent. Not an architectural decision: no new
  abstraction, no new shader, no ownership change.
- 2026-08-15 — layer visibility is filtered **caller-side** when building the edge buffer, so layer
  policy stays in one place and the renderer stays ignorant of it. The buffer is rebuilt only when
  the geometry revision (or the active tab) changes: at REQ-100 density it is ~600k segments, and
  regenerating that per frame would spend the budget re-deriving unchanged input.
- 2026-08-15 — step 7. `BENCH SURFACE [points] [frames]` added; the report **names the profile**,
  because a p95 quoted without saying which of REQ-100's three profiles produced it is as
  unreproducible as one quoted without the reference machine.

- 2026-08-15 — steps 1–7 complete in code. **Step 7's measurement is not**: `BENCH SURFACE` exists
  and runs, but it has not been executed on the reference machine, so REQ-100's surface profile has
  no recorded figure yet. Stated rather than assumed — see §9.

## 9. Self-verification  (run BEFORE submitting — verification/skills/)
- [x] build-project        — **PASS**. Clean build; no new warnings (the two that remain,
      `-Wmicrosoft-goto` in `ViewportRenderer.cpp` and an unused const in `PdfAttach.cpp`, are
      pre-existing and in files this task did not touch).
- [x] architecture-review  — **PASS**. No Workshop architectural decision; ADR-028 predates the
      task. §11.5 honoured and **asserted** (shared immutable payload; rebuild replaces the
      pointer). §11.8 honoured — `CadTin` is interleaved XYZ, no sidecar Z. §11.9 honoured —
      surfaces reference point groups by **name**, never by index. §11.4 / REQ-301 — no new
      abstraction: a concrete struct, free functions, and the existing line program.
      Layering: the triangulator is pure `util/`, GL-free, and knows nothing of `AppCommandState`.
- [x] code-review          — **PASS**. The O(n²) core was found and replaced by this review's own
      measurement rather than shipped. Corrupt stored triangulations are rejected at load with a
      message instead of being handed to the renderer to index out of bounds.
- [x] dependency-audit     — **n-a**. No dependency added; the triangulator is in-tree per
      ADR-028 (c). The revisit trigger it records (~1,200 lines) is not close — `tinbuild.cpp` is
      ~300.
- [ ] performance-review   — **PARTIAL**. Triangulation is measured and recorded (89 ms / 100k
      points / 199,957 triangles) and guarded by a test. **The REQ-100 surface *frame* profile is
      NOT yet measured** — `BENCH SURFACE` needs a run on the reference machine. Until it is,
      REQ-068 cannot claim the budget, and this task is not `done`.
- [x] testing              — **PASS**. `TinBuildTests`: 17 cases / 128 assertions. Full suite
      **292 cases, 65,430 assertions, green** (was 275 after TASK-045; no existing test changed).

## 10. Verification result
- Submitted:  <date>
- Verdict:    <PASS | FAIL | SPEC GAP>
- Findings:   <ids + how resolved>

## 11. Outcome
- Requirements satisfied: REQ-068 (pending the REQ-100 surface measurement — see §9)
- Tests added:            `tests/TinBuildTests.cpp` (17 cases)
- New modules:            `src/util/tinbuild.{hpp,cpp}` (pure), `src/ui/CadUi_Surfaces.cpp`
- Test fixture:           **`samples/surface-demo.gs`** (546 points, 5 point groups, 600 x 400 ft
                          site with a drainage swale and a ridge), **generated** by
                          `tools/Make-SurfaceDemoGs.ps1` and committed alongside it.
                          Generated rather than hand-built for the `benchscene.cpp` reason: a
                          committed `.gs` is an opaque blob nobody can review in a diff, whereas the
                          generator's terrain is six lines of arithmetic and its output is verified
                          byte-identical across runs (fixed-seed LCG, no clock, no `Get-Random`).
                          Template paper-space layouts are dropped — ~527 KB of title-block geometry
                          irrelevant here — taking the fixture from 1.17 MB to 152 KB.
                          It deliberately encodes two things that are easy to get wrong: points 1-5
                          carry an **edited description with the original raw code**, so the
                          description-keyed group resolves to 495 where the raw-keyed one resolves to
                          500 (REQ-066); and "Ground + Curb" combines a raw-desc match with an id
                          range, which resolves to 540 under the union rule and **zero** under
                          intersection (REQ-067).
- Docs updated:           none yet — `spec/roadmap.md` M-Surfaces status updates when the REQ-100
                          surface figure is recorded and this task closes.
- Done:                   <pending the REQ-100 surface run>
