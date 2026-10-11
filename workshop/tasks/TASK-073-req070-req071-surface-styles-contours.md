> **SUPERSEDED by TASK-085 (REQ-070) — 2026-08-21.** This was the earlier plan for the same roadmap
> step, written before ADR-036 and D-2026-08-21-a settled the cache placement, the `RenderScene`
> parameter and which style tabs are in scope. TASK-085 is the plan that was reviewed and
> implemented; its §10 records the two blocking findings that changed the design. Kept because its
> Q1/Q2 and its REQ-071 phase F are the starting point for the EXTRACT follow-up, which is still open.

# TASK-073 — REQ-070 + REQ-071: surface styles and contour extraction

- Type:    feature
- Status:  plan — Authority and Plan complete, no code written
- Opened:  2026-08-19
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         M-Surfaces **step 6** (`spec/roadmap.md` §M-Surfaces, "Now" and "Next"). Steps 1–5 are
                done (TASK-044/045/046/055/072); the sequencing note puts styles and contours here,
                and step 7 (REQ-072 banding/arrows) reads the same style table this task creates.
- Requirements: **REQ-070** (surface styles, `must`, accepted 2026-08-12),
                **REQ-071** (contour extraction, `should`, accepted 2026-08-12)
- Constraints:  REQ-101 (±0.01 ft — REQ-071's vertex tolerance), REQ-100 profile (c) (the surface
                bench is defined as *contoured* and orbited, so it is re-run here), REQ-201 (report,
                never absorb), REQ-064 (visual styles), REQ-076/ADR-027 (stable ids — extracted
                polylines are ordinary entities and get ids like any other), ADR-020 (the
                document-owned style-table pattern this borrows), ADR-028 (b)(c)(h), architecture §10
                (`util/tincontour` is already in the module list), §11.4 (no new abstraction), §11.5
                (shared, immutable TIN payload), §11.8 (interleaved XYZ, float storage / double
                predicates), §11.9 (reference by stable name or id, never index).
- Acceptance, restated verbatim:
  - **REQ-070**
    - changing the contour interval updates the display **without rebuilding the triangulation** and
      adds no entity to the drawing or to the saved `.gs`;
    - two surfaces sharing a style both change when the style is edited;
    - a style with triangles off and contours on draws only contours; with both off and border on,
      only the border;
    - a major interval that is not a whole multiple of the minor interval is rejected with a specific
      message rather than producing mis-labelled contours;
    - styles round-trip `.gs`; a legacy `.gs` loads unchanged; a surface whose style was deleted falls
      back to a default style rather than failing to draw.
  - **REQ-071**
    - extraction produces polylines at exactly the displayed contour elevations, each vertex within
      REQ-101 of the linear interpolation along the triangle edge it came from;
    - extracting twice produces two independent sets, neither affecting the other;
    - rebuilding the surface afterwards leaves already-extracted polylines untouched;
    - the created count and interval are reported;
    - extracting from a surface whose style has contours disabled creates nothing and says so, rather
      than silently extracting a hidden interval.
- Owning subsystem: `util/tincontour` (new pure module — contour polyline generation, already named in
  architecture §10), Domain (the style table on the document + `CadSurface::styleName`), Renderer
  (draw the generated geometry), UI (style editor inside the existing Surface Manager), Commands
  (EXTRACT), IO (`.gs` persistence).

## 2. Scope
- In scope: everything the two acceptance lists above state, plus the REQ-100 profile-(c) re-run the
  roadmap's step-6 entry explicitly calls for ("Re-run the REQ-100 surface bench case").
- Out of scope:
  - **REQ-072** band tables, slope banding, the legend and slope arrows (step 7). REQ-070's statement
    says the style "controls … the REQ-072 band and arrow settings" — those fields are *not* added
    now; see ASSUMPTION-1.
  - **REQ-075** Surface Manager consolidation (step 9). This task adds a style editor and a style
    assignment to the panel that already exists (`src/ui/CadUi_Surfaces.cpp`); it does not rebuild the
    panel or add definition reorder / stale-state UI.
  - Contour **labelling** and contour **smoothing**. ADR-028's consequences record "linear contours
    only" as deliberately left open; no requirement asks for labels, and REQ-071 exists precisely so
    that contours which must be labelled become ordinary polylines first.
  - DXF/DWG export of contours as a surface feature — extracted polylines export normally as
    polylines (ADR-028 (f)); surfaces themselves stay excluded, unchanged.
- Smallest change: one pure module + one document-owned table + one regenerated display buffer + one
  command. No surface interface, no style base class, no per-surface style copy.

## 3. Architectural boundary check (workflow.md §4)
- New abstraction / layer / dependency / ownership change / global state / public-API or data-format
  change / algorithm the spec didn't specify?
    - [x] **No — proceed.** Each item that *looks* like one is already decided upstream:
      - **The new `util/tincontour` module** is not a Workshop invention: architecture §10's module
        list names it verbatim ("tincontour — contour extraction and band assignment from a TIN
        (ADR-028)"), and ADR-028 (c) already settled that this family of algorithms is written
        in-tree, GL-free, in `util/`, for the TASK-035 §11 reason. Marching triangles over a TIN is
        the published algorithm for the specified data structure, not a substitution for a specified
        one.
      - **The style table** is ADR-028 (h) verbatim: "the style table is the ADR-020 document-owned-
        table pattern". Threading one more `std::vector<…>` through `DrawingDocument`, the undo
        snapshot and tab save/restore is the `drawingLayerTable` / `textStyles` shape, not a new
        ownership model.
      - **The `.gs` addition** is required *by REQ-070's own acceptance* ("styles round-trip `.gs`; a
        legacy `.gs` loads unchanged"), and the additive-key / no-`kGsFormatVersion`-bump mechanism is
        ADR-020 (d), already followed by the mesh and surface sections (`src/io/GsIo.cpp:633`).
      - **One more `RenderScene` parameter** follows the REQ-068 precedent set three lines away:
        `surfaceEdges` is a caller-built, layer-filtered, revision-cached flat buffer
        (`src/render/ViewportRenderer.hpp:69-80`). Contours are the same shape with colour and width
        attached — see Q2, which is where the one genuinely open design choice lives.
      - **Contours as display geometry rather than entities** is ADR-028 (b), decided there against
        the stated alternative and declined by the user in that form.
- If Q2 cannot be answered inside those precedents, that half — and only that half — becomes a SPEC
  GAP; the rest of the task is unaffected.

## 4. Questions (workflow.md §5)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | REQ-071's EXTRACT: which layer do the baked polylines land on? REQ-071 says "a chosen layer" without saying how it is chosen. (a) the current layer, named in the REQ-201 message; (b) `EXTRACT <surface> [layer]`, defaulting to the current layer; (c) a dialog. **Recommendation: (b)** — it satisfies "chosen" literally, stays usable with no argument, and reads like the other surface commands (`SURFELEV`, the designate commands). | 2026-08-19 — **open, needs the user** | |
| Q2 | How does the contour buffer carry per-style colour and lineweight? Minor and major differ, and two surfaces with different styles draw in the same frame, so one flat buffer with one colour (the `surfaceEdges` shape) cannot express it. (a) a vertex-coloured buffer `x,y,z,r,g,b`, reusing the renderer's existing vertex-colour line path (`cpuVcLines_` / `VcLineBatch`, which already carries a per-batch `widthPx`); (b) a small `std::vector<SurfaceDisplayBatch>{ verts, aci, lineweight }` passed as one parameter. **Recommendation: (b)** — the renderer already resolves ACI + lineweight for entity lines, batches are what the GL path wants anyway, and it describes display geometry in the vocabulary the rest of the drawing already uses. | 2026-08-19 — **open, needs confirmation** (Workshop-level under ADR-028 (b) if (b); raise as SPEC GAP only if neither option fits) | |
| Q3 | Is a style edit undoable? The table is document-owned, so it lands in `DrawingGeometrySnapshot` like `textStyles` and is undoable by construction. Recorded so the answer is deliberate rather than incidental. | 2026-08-19 (self-resolved — follows from the ADR-020 pattern ADR-028 (h) selected) | Yes. No extra work, but the interval-change test runs with undo enabled so the snapshot cost is exercised. |

## 5. Assumptions
```
ASSUMPTION-1: CadSurfaceStyle carries contour / triangle / border / point display only. The REQ-072
              band table and slope-arrow settings are NOT added in this task.
- Because:       REQ-070's statement lists them, but REQ-072 is step 7 and is what defines their shape
                 (band count, breakpoints, colour per band, arrow colouring by grade). Building fields
                 to a requirement that has not been implemented is speculative generality
                 (implementation-rules §2, CON-06).
- Risk if wrong: a second additive `.gs` change when REQ-072 lands.
- Validate by:   that risk is already priced at zero — ADR-020 (d)'s tolerant additive read means
                 REQ-072 adds keys to the same object with no version bump and no migration, exactly
                 as this task adds them to files that predate it. Confirmed against src/io/GsIo.cpp:633
                 and :1356, which read every surface field with a default.

ASSUMPTION-2: EXTRACT and the renderer call the SAME tincontour function with the SAME style, rather
              than extraction re-deriving contours with its own parameters.
- Because:       REQ-071's acceptance says "at exactly the displayed contour elevations". Two code
                 paths that agree today drift tomorrow; one pure deterministic function cannot.
- Risk if wrong: extraction silently produces a different set from what is on screen — precisely the
                 defect that acceptance bullet is written to catch.
- Validate by:   a test that extracts, then compares the resulting polyline vertices against the
                 display buffer's contour vertices for the same surface and style.

ASSUMPTION-3: A surface whose styleName is empty resolves to the built-in default style, and so does a
              styleName that no longer matches any row.
- Because:       REQ-070 states the deleted-style fallback; the empty case is what every surface in
                 every existing `.gs` file will have on first load after this ships.
- Risk if wrong: legacy drawings open with no surface display at all.
- Validate by:   the legacy-load test opens a committed pre-REQ-070 `.gs` and asserts the surface still
                 draws. This is ADR-020 (b)'s "an empty styleName resolves from the annotation's own
                 fields" with the resolution target changed to a default row, since a surface has no
                 own fields to fall back to.
```

## 6. Plan (workflow.md §6)
- Approach: seven phases in dependency order, each independently testable and independently shippable.
  A–C deliver REQ-070's display; D–E make it persistent and editable; F is REQ-071; G is the REQ-100
  re-run and self-verification. REQ-071 cannot start before C, because "exactly the displayed
  contours" has no meaning until something is displayed.

### Phase A — `util/tincontour`, the pure module
- New: `src/util/tincontour.{hpp,cpp}`, registered in `CMakeLists.txt` beside `tinbuild`.
- API mirroring `tinbuild`'s shape (flat arrays in, flat arrays out, no `CadTin` include, GL-free —
  the reason stated at `src/util/tinbuild.hpp:146`):
  - `TinContourResult ExtractContours(vertsXyz, indices, intervalFt, zMin, zMax)` — marching triangles:
    for each triangle intersect the plane z = k·interval against its three edges, emit the segment,
    then chain segments into polylines. Interpolation in `double`, stored `float` (ADR-028 (d),
    §11.8) — the widen-at-the-predicate rule the triangulator already follows.
  - `bool TinIntervalsCompatible(minorFt, majorFt, std::string* why)` — the major-is-a-whole-multiple
    rule, tolerance-based (never `==` on floats, implementation-rules §6), producing REQ-070's
    "specific message".
- Failure modes decided before the happy path (implementation-rules §4): interval ≤ 0 or non-finite →
  refuse with a message, no partial output; a flat surface where zMin == zMax → zero contours, a
  correct answer and not an error; a triangle whose vertex sits exactly on a contour elevation → the
  degenerate case that yields duplicate or zero-length segments if handled naively, so it gets its own
  test and the tie rule is stated in a comment.
- Tests: `tests/TinContourTests.cpp` — a single tilted plane of known slope (contour count and spacing
  hand-computable), a two-triangle roof (a contour crossing the ridge), the vertex-on-contour
  degenerate case, a flat surface, interval validation happy/refused, and REQ-101 tolerance on every
  vertex against the hand-computed edge interpolation.

### Phase B — the style table (Domain)
- New: `src/commands/SurfaceStyle.hpp` — `CadSurfaceStyle` value type (name; minor interval + ACI
  colour + lineweight; major interval + ACI colour + lineweight; `showTriangles`, `showContours`,
  `showBorder`, `showPoints`) plus a pure `SurfaceStyles::` helper namespace (`EnsureDefault`, `Find`,
  `Resolve`) — the `src/commands/TextStyle.hpp` precedent: a concrete function set, not an abstraction
  (§11.4).
- `CadSurface` gains `std::string styleName` (`src/commands/CadEntities.hpp:268`) — by name, exactly as
  `sourcePointGroups` are and for the reason documented there (§11.9).
- `std::vector<CadSurfaceStyle> surfaceStyles` threaded through `AppCommandState`, `DrawingDocument`,
  `DrawingGeometrySnapshot` and tab save/restore — the sites `textStyles` occupies
  (`src/commands/CadCommands.hpp:323` and `:1043` neighbourhoods). A reserved default row always
  exists.
- **Resolve-on-read, NOT ADR-020's bake-on-write.** ADR-020 bakes because ~12 render/measure sites read
  an annotation's own fields; a surface has exactly one generation site, so baking would buy nothing
  and would put a stale copy on the surface. Recorded here so the divergence from the cited pattern is
  deliberate and reviewable rather than an oversight.
- Tests: `tests/SurfaceStyleTests.cpp` — resolve, unknown-name fallback to default, empty-name
  fallback, `EnsureDefault` idempotence, interval validation surfaced through the helper.

### Phase C — display geometry (Renderer + Commands)
- `AppendSurfaceDisplayGeometry` in `src/commands/CadCommands.cpp`, generalising the existing
  `AppendSurfaceEdgeLines` (`:1209`) rather than sitting beside it: per surface, resolve the style and
  emit triangles / contours / border per its toggles, keeping the layer off/frozen filter that
  function already applies. Contours come from Phase A; the border is the boundary of the culled
  triangulation.
- `src/app/main.cpp:521` builds the buffer; `ViewportRenderer::RenderScene` takes it per Q2.
- **The trap, and why this phase is not mechanical:** REQ-070's first acceptance bullet is "changing
  the contour interval updates the display **without rebuilding the triangulation**", and the current
  staleness test is `builtAtRevision != cadGpuRevision` (`CadEntities.hpp:296-302`), where
  `cadGpuRevision` increments on *every* drawing mutation. A style edit is a drawing mutation — so as
  the code stands, editing an interval would bump the revision, mark every surface stale and trigger a
  full retriangulation, failing that bullet directly. The fix stays inside Domain (that field is
  TASK-072's own implementation choice, documented as such): key the rebuild trigger on the surface's
  *definition* — a signature over `sourcePointGroups` + `breaklineIds` + `boundaries` + the resolved
  input geometry — rather than on the global revision, or exclude a style edit from what a surface
  counts as a change.
- Caching: the display buffer is rebuilt only when the TIN pointer or the style table changes, never
  per frame. REQ-100 profile (c) is a *per-frame* budget and contour regeneration is exactly the cost
  it exists to measure — REQ-100's statement says so in as many words.
- Tests: the toggle matrix (triangles off + contours on → only contours; both off + border on → only
  the border) at the buffer level; **an interval change does not retriangulate**, proven by asserting
  `surface.tin.get()` is pointer-identical before and after (§11.5's shared immutable payload makes
  that an exact test rather than an inference); two surfaces sharing one style both change when it is
  edited.

### Phase D — `.gs` persistence (IO)
- An additive top-level `surfaceStyles` array plus `styleName` on each surface object, written only
  when non-empty so a drawing with no styles still serialises byte-identically (`GsIo.cpp:633`'s
  stated rule, and what BUG-015 / BUG-019 taught about resave idempotence). Tolerant read with
  defaults, no `kGsFormatVersion` bump (ADR-020 (d)).
- Tests: round-trip; a committed legacy `.gs` predating this task loads and still draws
  (ASSUMPTION-3); resave idempotence on both; a surface referencing a deleted style falls back rather
  than failing to draw.

### Phase E — the editor (UI)
- Extend `DrawSurfaceManagerWindow` (`src/ui/CadUi_Surfaces.cpp:54`): a style dropdown per surface and
  a style editor (create / rename / delete / edit, the default row protected from deletion) in the
  text-style manager's shape. The major-not-a-multiple-of-minor case is refused at the editor with
  Phase A's message — REQ-070's "rejected with a specific message rather than producing mis-labelled
  contours".
- No new window if the panel can carry it: REQ-075 is where the panel gets consolidated, not here.

### Phase F — REQ-071 EXTRACT (Commands)
- `Kind::ExtractContours` beside `Kind::SurfaceElevGrade` (`CadCommands.hpp:469`), command name
  `EXTRACT` — the name ADR-028 (b) already uses. It bakes the *currently displayed* contours (same
  function, same style — ASSUMPTION-2) into ordinary polylines on the layer Q1 decides, each getting a
  normal stable id, deliberately unlinked from the surface.
- Reports created count and interval (REQ-201). Contours disabled → creates nothing and says so.
- Tests: vertex-level agreement with the displayed contours within REQ-101; extract twice → two
  independent sets; rebuild the surface afterwards → the extracted polylines are unchanged;
  disabled-contours refusal; the count/interval message.
- A headless transcript (`tests/headless/transcripts/req070-req071-styles-and-contours.txt`) covering
  build → style → interval change → extract → undo → save → reopen, in the TASK-070 precedent, since a
  unit test cannot reach the command state machine end to end.

### Phase G — REQ-100 profile (c) re-run and self-verification
- The surface bench case (`BENCH SURFACE`) currently measures an *uncontoured* surface, because
  contours did not exist when TASK-052/053 measured it. REQ-100 defines profile (c) as "100,000
  points / ~200,000 triangles, **contoured** and orbited", so the case is re-run with a style whose
  contours are on, and `bench-req100.txt` records the interval beside the existing `profile` / `scene`
  lines (TASK-053's fix (b): the permanent record is the half that gets missed).
- Measured on the **RTX 5060** with the `project.md` §7 toolchain — a figure from a different GPU or
  compiler is a different result (BUG-013 / TASK-053 FINDING-3).
- Full self-verification pass and the completion report.

- Steps:
  - [ ] Phase A — `util/tincontour` + `TinContourTests`
  - [ ] Phase B — `CadSurfaceStyle`, the document-owned table, `CadSurface::styleName` + tests
  - [ ] Phase C — display geometry, the rebuild-trigger fix, the no-retriangulation test
  - [ ] Phase D — `.gs` persistence + legacy / idempotence tests
  - [ ] Phase E — the Surface Manager style editor
  - [ ] Phase F — EXTRACT + headless transcript
  - [ ] Phase G — REQ-100 profile (c) re-run, self-verification, completion report

## 7. Workflow-specific notes
- Feature (workflow.md A). Pre-flight: owning subsystem named per phase; no new abstraction (§3);
  failure modes decided before the happy path in Phase A; correctness proven by the per-bullet tests
  each phase lists. Two questions (Q1, Q2) are open and must be answered **before Phase C** — Phases A
  and B depend on neither, so work can start without guessing (workflow.md §5).
- Tests-first where practical: Phase A is a pure module, so its tests are written against the API
  before the implementation, as TASK-072's Phases B/C were.

## 8. Implementation log
- 2026-08-19 — opened; Authority and Plan written. Status: plan. No code.

## 9. Self-verification (run BEFORE submitting — verification/skills/)
- [ ] build-project        — clean, warning-free
- [ ] architecture-review  — no Workshop architectural decision (§3, with Q2's answer recorded)
- [ ] code-review          — correctness, simplicity, ownership, readability
- [ ] dependency-audit     — n-a (no manifest touched; the in-tree contour module is ADR-028 (c))
- [ ] performance-review   — REQUIRED, not optional: REQ-100 profile (c) with contours on, numbers
                             recorded, reference GPU and toolchain named
- [ ] testing              — happy + failure-mode per acceptance bullet, run green, full ctest

## 10. Verification result
- Submitted:  <date>
- Verdict:    <PASS | FAIL | SPEC GAP>
- Findings:   <ids + how resolved>

## 11. Outcome
- Requirements satisfied: REQ-070, REQ-071 (Acceptance met: <yes/no>)
- Tests added:            <ids>
- Refactors:              <ids or none>
- Docs updated:           <files or none>
- Done:                   <date>
