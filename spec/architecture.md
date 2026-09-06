# Architecture Specification

> **Template.** Defines the *shape* of the system: its layers, boundaries,
> ownership model, and data flow. Implementation lives within this shape; reviews
> audit against it. Architecture here is descriptive of intent and prescriptive
> of rules — not a UML museum. Keep diagrams in ASCII so they live in the repo
> and survive diffs.

---

## 1. Architectural style

State the style in one sentence and mean it.

> `<This is a single-process, data-oriented desktop application with a strict
> downward-only layer dependency graph and explicit, single-owner resource
> management.>`

Guiding stance (Cherno-style):

- **Few moving parts.** Prefer one well-named concrete type over a constellation
  of interfaces.
- **The machine is visible.** Memory layout, allocations, and ownership are
  intentional and reviewable, not hidden behind magic.
- **Layers, not webs.** Dependencies form a DAG that points one way.

## 2. Layering

Dependencies flow **downward only**. A lower layer must never name a higher one.

```
┌─────────────────────────────────────────────┐
│ Application        (lifecycle, wiring)       │
├─────────────────────────────────────────────┤
│ UI / Viewport      (interaction, display)    │
├─────────────────────────────────────────────┤
│ Commands           (parse, validate, run)    │
├─────────────────────────────────────────────┤
│ Renderer           (GPU, draw, shaders)      │
├─────────────────────────────────────────────┤
│ Entities / Domain  (data + invariants)       │
├─────────────────────────────────────────────┤
│ IO                 (formats, serialize)      │
├─────────────────────────────────────────────┤
│ Platform           (OS, window, files, GL)   │
└─────────────────────────────────────────────┘
        dependencies point DOWN ↓ only
```

- ✅ **Good:** `Editor → Entities`
- ❌ **Bad:** `Entities → Editor`

If a lower layer appears to need a higher one, the design is inverted. Fix it by
passing the value *down* explicitly, or by introducing a callback/event the
lower layer emits and the higher layer subscribes to — never by reaching up.

> Replace the layer stack with your project's. The **rule** (downward-only) is
> the part that does not change.

## 3. Subsystem responsibilities

Each subsystem has one responsibility and an explicit *not*-list. The not-list is
what keeps subsystems from absorbing each other's work.

| Subsystem | Responsible for | NOT responsible for |
|-----------|-----------------|---------------------|
| **Renderer** | GPU resources, draw calls, shaders, buffers | UI, commands, business logic |
| **Commands** | Parsing, validation, execution | Rendering, window management |
| **UI / Viewport** | User interaction, presenting state | Core domain logic |
| **Entities / Domain** | Domain data + its invariants | How it is drawn or edited |
| **IO** | Reading/writing formats | Domain meaning beyond parse/serialize |
| **Platform** | OS, windowing, file handles, GL context, outbound HTTPS | Anything domain-specific |
| **Update** | Deciding whether a fetched manifest describes a newer build | Fetching it, downloading, or presenting it (Platform / UI do those) |

## 4. Ownership model

Ownership must be obvious **at the type level**. This is the single most
important architectural property for debuggability.

### General rule

> Exactly one owner per resource. Everything else borrows, visibly.

### Per-language idiom

| Language | Owning | Borrowing | Cleanup |
|----------|--------|-----------|---------|
| **C++** | `std::unique_ptr<T>`, value members | `T*` / `T&` (non-owning) | RAII / destructors |
| **Rust** | move semantics, `Box<T>` | `&T` / `&mut T` (lifetimes) | `Drop` |
| **Zig** | explicit allocator + handle | passed slices/pointers | `defer alloc.free(...)` |
| **Go** | value / single struct owner | passed pointers | `defer`, GC for memory; explicit `Close()` for handles |

Rules that hold across all of them:

- A raw/borrowed pointer means "owned elsewhere, referenced here." It never frees.
- Shared ownership (`shared_ptr`, `Rc`/`Arc`, ref-counting) is a *justified
  exception* recorded in the decision log — not a reflex.
- In Zig, the allocator is part of the API. Pass it in; don't hide it.
- In Go, if a type owns an OS resource, give it an explicit `Close()` and
  document who calls it — don't rely on the GC for non-memory resources.

## 5. Data-oriented design

Design around the data and its dominant access pattern, not around an object
taxonomy.

- **Lay out for the loop that matters.** If you iterate vertices every frame,
  store them contiguously (`struct of arrays` where it pays), not as a forest of
  heap-allocated nodes.
- **Separate hot from cold.** Keep per-frame data dense and free of cold,
  rarely-touched fields that evict cache lines.
- **Batch.** Prefer transforming arrays of data in tight loops over per-object
  virtual calls.

```
// Cold, "OOP-shaped": pointer-chasing, virtual per item — avoid in hot paths
for (Shape* s : shapes) s->Draw();   // cache-hostile, virtual dispatch

// Hot, data-oriented: contiguous, predictable, batched
renderer.DrawLines(lineVertices);    // one call over a packed buffer
```

> This is the heart of the Cherno-style mindset: think about *what the data is
> and how it moves*, then write the simplest code that moves it efficiently.

## 6. Data flow

Prefer explicit data flow; forbid hidden global state.

- ✅ `renderer.Draw(scene);`
- ❌ `GlobalScene::Get().Draw();`

State is passed, not summoned. A function's inputs and outputs should be visible
in its signature. Singletons and global mutable state are a recorded exception,
never a convenience.

## 7. Rendering / OpenGL boundary

> Include this section for graphics projects. The goal is to keep GL out of the
> rest of the codebase.

- All GL calls live behind the Renderer/Platform boundary. No `gl*` call appears
  in UI, Commands, or Domain code.
- GPU resource handles (buffers, textures, shaders) are owned by RAII/`defer`
  wrappers that create on construction and delete on destruction — no leaked
  handles.
- The render API is *retained-friendly but immediate-simple*: callers submit
  data (`DrawLines(verts)`), the Renderer owns the buffers and state.
- Shader sources, uniforms, and pipeline state are explicit and centralized, not
  scattered through call sites.

## 8. Concurrency model

> State it even if the answer is "single-threaded." Ambiguity here causes the
> worst bugs.

- **Threading: a single-threaded UI, plus detached one-shot worker threads for long compute.** There
  is no thread pool, no job system, and no scheduler — introducing one is an architectural decision,
  not a Workshop choice. GoSurvey's entire drawing state (`AppCommandState`) is owned by the UI
  thread and is **never** read or written from a worker.
- **The one-shot worker pattern** (written down 2026-08-12; it was already in use, undocumented, in
  `PdfAttach` and `AppCommandState::AsyncBuild`). Every background task follows it, and a task that
  needs something else is escalated rather than improvised:
  1. **Inputs are copied, not referenced.** The worker receives its own copy of everything it needs.
     It holds no pointer into `AppCommandState`.
  2. **The task's state is heap-allocated** (`unique_ptr` to a struct holding the `std::thread`, an
     `std::atomic<bool> done`, and the result), so its atomics don't make the owning state
     non-copyable.
  3. **The worker's last act is a release store to `done`.** The UI thread polls `done` each frame
     and consumes the result; that acquire/release pair is the only synchronisation, and there is no
     mutex to get wrong.
  4. **Results are validated against a generation counter before being applied.** The state may have
     moved on — an undo, a further edit — while the worker ran. A stale result is **discarded**, not
     applied to a state it was not computed from.
  5. **Cancellation is cooperative**, via an `std::atomic<bool>` the worker polls.
- **Ownership across threads:** data is copied into the worker, the result is moved back on the UI
  thread. There is no shared mutable state and therefore no lock to document. A `shared_ptr<const T>`
  crossing a thread boundary is permitted (§11.5 — the payload is immutable); a `shared_ptr<T>` is
  not.
- **Rust note:** lean on `Send`/`Sync` to make this a compile-time guarantee.
- **Go note:** "share memory by communicating" — pass ownership over channels;
  don't share structs across goroutines without a mutex you can point to.

## 9. Error-handling architecture

Match the mechanism to the failure kind, consistently across the codebase.

| Failure kind | Mechanism |
|--------------|-----------|
| Programmer error (broken invariant) | assertion / `unreachable` / `panic` — fail loud in debug |
| Recoverable runtime failure | C++: status/`expected`; Rust: `Result<T,E>`; Zig: error unions; Go: `error` return |
| Truly exceptional | only if the language/ecosystem already relies on it (rare in C++ here) |

No error path is empty. Either handle it, return it, or assert — never swallow.

## 10. Module / directory layout

Keep code grouped by subsystem, not by file kind. Avoid a junk-drawer `utils/`.

```
src/
  app/          application lifecycle, wiring
  ui/           panels, viewport, input
                RichTextEdit — WYSIWYG rich-text edit widget for MTEXT (ADR-023)
                ViewCube — 3D view-orientation widget (ADR-025)
  commands/     parsing, validation, execution
  renderer/     GL backend, buffers, shaders
                Camera — eye/target/up + projection → view & projection matrices (ADR-025)
  domain/       entities, invariants, compute
  io/           format readers/writers
  font/         SHX stroke-font geometry — pure, no rendering (shared by ui + io; ADR-022)
  util/         pure, dependency-free math and formatting — unit-testable without GL or a window
                geom2d, NumFormat, AngleFormat, StringUtil
                ray3d — screen→world ray, ray×plane, ray↔entity distance (ADR-025)
                tinbuild — constrained Delaunay triangulation, double predicates (ADR-028)
                tincontour — contour extraction and band assignment from a TIN (ADR-028)
                tinanalysis — slope, downhill direction, surface-to-surface volumes (ADR-028)
                surfacestats — 2D/3D area, slope extrema, extents from a TIN (ADR-039)
                watershed — drain graph, basins, water-drop, catchment (ADR-039; Phase 3)
                brep — B-rep solid kernel: topology, the seven primitives, validity,
                       exact mass properties, tessellation (ADR-045)
  update/       version ordering + manifest parse — pure, no network (ADR-029)
  platform/     window, files, GL context, WinHTTP fetch + SHA-256 (ADR-029)
third_party/    vendored dependencies (each recorded in the decision log; REQ-300)
build/          all build artifacts (never in source tree)
spec/           this specification layer
```

## 11. Architectural invariants (the audit list)

A change is rejected if it breaks any of these:

1. No upward dependency across layers.
2. No subsystem doing another subsystem's job.
3. No new global mutable state.
4. No new abstraction without ≥2 present-day concrete uses.
5. **Every *mutable* resource has exactly one visible owner.** Shared ownership of an
   **immutable** payload is permitted, and is the intended pattern for large read-only
   geometry (amended 2026-08-12, decision log). The invariant exists to prevent
   mutation-ordering hazards — two owners disagreeing about when a thing changed — and
   data that cannot change cannot have them. The permission is narrow and comes with
   conditions: the shared type must be held as `shared_ptr<const T>` so the compiler
   enforces the immutability the exemption rests on, and "editing" such a resource means
   **replacing the pointer**, never writing through it. A `shared_ptr<T>` to mutable data
   is still a blocking finding.
   *Amended 2026-08-30 (D-2026-08-30-a):* the **Start tab** (`cmd.drawingTabs[0]`, REQ-308) is
   exempt — it is a UI sentinel that owns **no** mutable document resource (no entry in
   `documents[]`/`viewportRenderers[]` is used for it), so there is no ownership to make single.
   Every real drawing tab (index ≥ `FirstDrawingTabIndex()`) still obeys the rule.
   *Raised by TASK-041:* `DrawingGeometrySnapshot` deep-copies every geometry array and
   50 frames are kept, so a 2M-triangle mesh (REQ-063's own ceiling, ~53 MB) would cost
   ~2.6 GB of undo stack — and would be re-copied by every unrelated edit. ADR-026 (c)
   had already made meshes non-editable, so the payload was immutable before the problem
   was found; this amendment records that immutability as the thing that makes sharing safe.
6. No `gl*` (or other backend) calls outside the Renderer/Platform boundary.
7. No allocation/logging/virtual dispatch added to a measured hot path without a
   profile justifying it.
8. **Geometry coordinates are interleaved XYZ — never split across a sidecar array.**
   Every flat geometry store carries its Z inline (`userLinesFlat` stride 6,
   `userPolylineVerts` stride 3, `userCirclesCxCyZR` stride 4,
   `CadFilledRegion::vertsXyz` stride 3). Adding a parallel Z array beside an
   existing store is a blocking finding: it splits one coordinate across two
   allocations (§5) and introduces a desync failure mode that interleaving cannot
   have. Widening a stride is done **with a rename**, so every affected site is a
   compile error rather than a silent misread (ADR-025 (a)). *A per-vertex
   non-coordinate channel — the polyline `userPolylineVertsBulge` array
   (ADR-047) — is a parallel array by exception: a bulge is not a coordinate, so
   §11.8's anti-split does not reach it, and it follows the ADR-035 (c) /
   D-2026-08-31-f side-car pattern (count checked in `docinvariants`).*
9. **A reference from one object to another is a stable id — never an array index.**
   Entities are stored in flat arrays that **compact on erase**, so an index is not a name: after a
   delete it silently designates a different entity. Storing an index across an object boundary, or
   adding a fix-up pass that walks references decrementing them at an erase site, is a blocking
   finding. Use the REQ-076 id and resolve it through an index built on demand.
   *Raised by the REQ-069 verification:* the codebase's one pre-existing cross-reference
   (`SurveyPoint::labelMtextAnnIndex` ↔ `CadAnnotation::surveyPointLabelFor`) is an index pair, and
   keeping it correct already costs a decrement loop in `EraseCadAnnotationAtIndex` plus ~46
   maintenance sites across 7 files. It works because it is one reference maintained at one erase
   site. It does not generalise, and surfaces would have needed several more.

## 12. Architecture decision records (ADRs)

> Significant structural decisions get a short record. Link them from
> `project.md`'s decision log. One ADR, one decision.

```
### ADR-NNN — <title>            (<date>, <accepted|superseded>)
- Context:    the forces and constraints in play
- Decision:   what we chose
- Alternatives: what we rejected, and why
- Consequences: what this makes easy, and what it costs
```

### ADR-001 — Least-squares traverse adjustment module   (2026-06-10, accepted)
- Context:    REQ-014–017 require a rigorous adjustment of a closed-loop traverse
  that yields per-observation angular and distance residuals for blunder review.
  The existing `ComputeTraverse` produces only an unadjusted closure. A
  least-squares adjustment is a new compute capability — an architectural
  decision, not a Workshop choice.
- Decision:   Add a Domain-layer module (in `src/traverse/`) that assembles the
  weighted normal equations (N = AᵀWA) for a closed loop's unknown station
  coordinates and solves them with an **in-tree** dense symmetric solver
  (Cholesky / Gaussian elimination), producing adjusted coordinates and
  residuals v = Ax − l. No third-party linear-algebra dependency is added.
- Alternatives: (a) Eigen or similar matrix library — rejected: the systems are
  tiny (2 × unknown stations); a heavy header-only dependency is not justified
  under the REQ-300 dependency policy. (b) Rule-based methods only
  (Compass/Transit/Crandall) — rejected: they distribute coordinate misclosure
  and cannot produce true per-observation angle/distance residuals (REQ-016).
- Consequences: a small in-tree solver to maintain and test; the dependency
  graph stays minimal; scope is bounded to closed loops this increment
  (connecting traverses deferred to the roadmap).

### ADR-003 — Backsight reading on the leg + `ReduceLegFromSets` reduction   (2026-06-11, accepted)
- Context:    REQ-018 makes a leg's per-set F1/F2 observations editable, after
  which the leg's horizontal angle must re-derive from the edited circle
  readings. A leg's H.Angle is `circle reading − backsight circle reading`, but
  the backsight reading was consumed by the FBK importer (`setup.bsHzDec`) and
  never stored on the leg — so the edited sets had no reference to reduce against.
  How an edited set feeds the leg is a data-model decision, not a Workshop choice.
- Decision:   Store the backsight circle reading on `TraverseLeg`
  (`backsightCircleDeg`, `hasBacksightCircle`) and add a Domain reduction
  function `ReduceLegFromSets(TraverseLeg&)` (in `TraverseCalc`) that face-averages
  the literal per-set circle readings, subtracts the backsight reading to get the
  reduced H.Angle, averages the zenith angles and slope distances, and writes the
  leg's reduced fields and input buffers. The FBK importer is refactored to
  populate the sets + backsight reading and then call this one function, so the
  import path and the edit path reduce identically (single source of truth).
- Alternatives: (a) store pre-reduced directions in the sets instead of literal
  circle readings — rejected: loses raw-measurement fidelity (REQ-010 "see every
  measurement made"). (b) make sets editable but not re-reduce the leg —
  rejected: editing would silently not affect the computed traverse (a hidden
  failure, REQ-201).
- Consequences: `TraverseLeg` gains two fields; reduction logic moves out of the
  importer into one reusable Domain function (less duplication, edit==import);
  the literal field readings are preserved for display and adjustment.

### ADR-002 — Domain test target (Catch2 + ctest)   (2026-06-10, accepted)
- Context:    REQ-011/012/015/016 require committed numeric regression tests, but
  the project had no test infrastructure. The accepted least-squares math
  (ADR-001) is exactly the kind of compute that needs tolerance-asserted tests.
- Decision:   Add a separate `GoSurveyTests` executable, built only when tests
  are enabled, that compiles the Domain compute sources (`TraverseCalc.cpp` and
  the new least-squares module) and exercises them with Catch2 v3 under ctest.
  GoSurvey (the GUI app) does not link Catch2; the dependency is test-only.
- Alternatives: (a) in-tree assert harness — viable but weaker tolerance
  assertions/reporting; (b) folding tests into the GUI executable behind a flag —
  rejected: couples tests to UI and the GL/window stack.
- Consequences: first test target for the repo; Catch2 added as a test-only
  FetchContent dependency (recorded in the decision log per REQ-300); domain
  code must stay linkable without the UI/GL layers (a healthy layering pressure).

### ADR-004 — Configurable angle DISPLAY via a pure formatter module   (2026-06-11, accepted)
- Context:    REQ-021 makes angle/bearing presentation user-configurable (format,
  precision, direction, base angle). The application has a single app-wide angle
  convention — bearings clockwise from north — baked into hard-coded formatters
  (`CadFormatBearingCwNorthDegMinSec`, `%.4f°`). Making display configurable
  touches every angle readout, so it is an architectural decision, not a Workshop
  choice.
- Decision:   Add a Domain/util-layer **pure** `AngleFormat` module (the angle
  analog of `NumFormat.hpp`) that, given angle-format settings (type, precision,
  direction, base), formats an angle for display. The stored/compute convention
  (CW-from-north, used for angle *entry* and geometry) is unchanged; the new
  module is a display layer over it. Settings live on `AppCommandState` and
  persist via `UserPrefs`. No third-party dependency.
- Alternatives: (a) make `ANGBASE/ANGDIR` reinterpret angle *input* too — rejected
  this increment: it touches every angle-entry path and is a larger, riskier
  change (deferred; would need its own REQ). (b) Scatter format branches into each
  readout — rejected: duplicates logic and is untestable without the UI (violates
  the ADR-002 layering pressure and §11.2).
- Consequences: angle display becomes one tested seam reused by ≥2 readouts
  (satisfies §11.4); the underlying convention is preserved so existing geometry,
  angle entry, and REQ-101 fidelity are untouched; default settings must reproduce
  the previous bearing output (guarded by a parity test).

### ADR-005 — Survey-point identity in DXF via a registered XDATA schema   (2026-06-12, accepted)
- Context:    REQ-023 requires survey points to survive a DXF round-trip with their
  identity (id, description, label style, linked label). A DXF `POINT` has no
  native field for these, and the importer currently expands every `POINT` into
  cross-line geometry — so the data is lost. Encoding extra identity in the DXF is
  a data-format decision, not a Workshop choice.
- Decision:   Carry survey-point identity in DXF **XDATA** under one registered
  application id, `GOSURVEY` (added to the APPID table). On a survey `POINT`:
  `1071` id, `1070` label style, `1000` description (coordinates stay in `10/20/30`,
  layer in `8`). On a survey-label `MTEXT`: a `GOSURVEY` marker so import skips it
  and the reconstructed point regenerates its own linked label. Import rebuilds a
  `SurveyPoint` from any `POINT` carrying this XDATA (applying the same
  transform/world-origin handling as geometry); a `POINT` without it keeps the
  cross-line behavior. Contained to `src/io/DxfIo.cpp`; no new dependency.
- Alternatives: (a) a custom OBJECTS-section dictionary — heavier and more fragile
  than entity XDATA; (b) layer-name / point-style conventions — can't carry a
  description or id robustly; (c) leave it — the accepted data loss (issue #37).
- Consequences: GoSurvey DXF becomes a faithful survey round-trip; third-party
  apps still read valid `POINT`s (unknown XDATA is ignored); a small XDATA schema
  and one registered APPID to maintain. The APPID handle is appended at the end of
  the symbol-handle range so existing handles do not shift.

### ADR-006 — Paper-space data model and a paper/model render mode   (2026-06-15, accepted)
- Context:    REQ-025–028 introduce paper space: each drawing gains named paper
  **layouts**, each holding **viewports** (rectangular windows onto model space with
  their own scale, center, and frozen-layer set), plus an active-**space** notion
  (model vs a paper layout). Model space today is a single coordinate space rendered
  by one viewport path. Adding layouts/viewports is new Domain data + ownership, and
  drawing a sheet with scaled clipped model views is a new Renderer mode — an
  architectural decision, not a Workshop choice (architecture §3, §7, §11.4).
- Decision:   Add a Domain data model owned by the drawing/document: a vector of
  `PaperLayout` (name, paper size, orientation) each owning a vector of `Viewport`
  (paper-space rect, model center, scale, frozen-layer set), plus an `activeSpace`
  selector on `AppCommandState`. The Renderer gains a **paper-space pass**: it draws
  the sheet outline in paper units, then for each viewport sets a scissor rect and a
  paper←model transform (scale + center) and reuses the **existing** model geometry
  batches, skipping that viewport's frozen layers. Model space rendering is
  unchanged when `activeSpace == Model`. No new geometry storage — viewports
  reference the one model. Concrete uses (layouts, viewports) are present today, so
  the types are concrete, not speculative (§11.4).
- Alternatives: (a) duplicate geometry per layout — rejected: wastes memory and
  desynchronizes from the model. (b) a generic "scene graph / camera" abstraction —
  rejected as speculative (§11.4); a viewport is a concrete transform+rect+scale.
  (c) render each viewport to an offscreen FBO — deferred; scissor + transform over
  the existing batches is simpler and sufficient.
- Consequences: the document model grows by two small owned vectors + an enum; the
  Renderer learns one new pass reusing existing batches; `.gs` gains a layouts
  section (REQ-031); DXF layout/VPORT export is explicitly deferred. Built
  incrementally (decision log) so each slice passes Verification.

### ADR-007 — Plot output as vector PDF via the bundled PDFium edit API   (2026-06-15, accepted)
- Context:    REQ-029/030 require plotting a layout (and batches) to a printable
  sheet at true plot scale. The project already links **PDFium** (read path for PDF
  underlays, with `fpdf_edit.h` available). How plot output is produced — and
  whether a new dependency is taken — is an architectural/dependency decision
  (REQ-300).
- Decision:   Produce plot output as **vector PDF** using PDFium's edit API
  (`FPDF_CreateNewDocument`, `FPDFPage_New`, path/text page objects), one PDF page
  per layout sized to the layout's paper size, geometry emitted at true plot scale
  (paper units), each viewport's model content transformed and clipped to its rect.
  Batch plot writes multiple pages into one document. **No new dependency.**
  Direct-to-OS-printer (GDI) is deferred; users print the produced PDF.
- Alternatives: (a) Windows GDI printing — rejected this increment: Windows-only
  print code and batch still needs PDF. (b) raster-to-PDF (hi-DPI image per sheet) —
  rejected: not vector-crisp, larger files; may be a fallback for dense underlays
  only. (c) a new PDF-writer dependency — rejected: PDFium already provides write
  APIs (REQ-300).
- Consequences: plotting lives in IO/Renderer with no new dependency; output is
  vector and measurable against REQ-101; a PDF-emit path to maintain alongside the
  existing PDF-read path; real-printer support remains a future REQ.

### ADR-008 — Viewports as selectable objects + floating model space   (2026-06-15, accepted)
- Context:    REQ-033–036 add command-driven viewport creation, viewport selection
  with MOVE/COPY/DELETE, and floating model space (edit the model through a viewport,
  AutoCAD MSPACE). This extends paper space (ADR-006) from a passive display into an
  interactive space, which touches the selection model and the command/coordinate
  flow — architectural decisions, not Workshop choices.
- Decision:   (a) **Viewport-creating commands** are new `AppCommandState::Kind`
  values with paper-space draft state; their clicks are handled in a paper-space
  input branch (screen↔paper-inch), not the model `SubmitViewportPick` path, and they
  render a rubber-band preview through the paper overlay. (b) **Viewports are
  selectable** in paper space via a paper-space selection set; the existing MOVE,
  COPY, and DELETE commands branch on the active space to operate on selected
  viewports (translate the rect / duplicate / erase) instead of model entities —
  reusing the command surface without a parallel command set. (c) **Floating model
  space** is an `AppCommandState` mode (active viewport index) under which the
  model command + snap pipeline runs through that viewport's transform, clipped to
  its rect; entering is a double-click, leaving is double-click-out / Esc / PSPACE.
  No new dependency, no new global; state lives on `AppCommandState`/`DrawingDocument`.
- Alternatives: (a) a parallel paper-space command set (separate move/copy/delete) —
  rejected: duplicates command logic and diverges UX from model space (the user
  asked for parity). (b) viewports as model `SelectedEntity` — rejected: they live in
  paper coordinates, not the model frame; a paper-space selection is clearer. (c)
  read-only viewports (no MSPACE) — rejected: the user requires editing through the
  viewport.
- Consequences: a handful of new command Kinds + a paper-space selection vector;
  MOVE/COPY/DELETE gain a small paper-space branch; the renderer eventually needs the
  per-viewport transform/clip pass for MSPACE drawing and for polygonal clipping
  (REQ-034) — the GL scissor/stencil pass deferred under ADR-006. Delivered
  incrementally (3a ribbon+rect viewport, 3b select+edit, 3c floating mspace,
  3d polygonal) so each slice is verifiable.

### ADR-009 — Native paper-space geometry: per-layout entity store + command routing   (2026-06-16, accepted)
- Context:    REQ-037 adds geometry that lives **on the sheet** (title blocks, notes,
  borders), separate from model space and from viewport content. Paper layouts today
  own only `Viewport`s — there is no place to store sheet-native lines/text, no rule
  for which store a draw/edit command targets, and no `.gs` schema for it. New Domain
  data + ownership + a coordinate-space routing rule = an architectural decision, not a
  Workshop choice (architecture §3, §10.1 single-owner, §11.4 no speculative types).
- Decision:   (a) **`PaperLayout` owns a paper-space entity store** — its own vectors
  of paper-space lines and text (extensible later to polylines/circles/arcs), stored in
  **paper inches** with the sheet origin at (0,0). These reuse the *existing* entity
  value types where practical (line endpoints, a text/annotation record + attributes),
  not new speculative abstractions. One visible owner: the `PaperLayout` (§10.1, §11.5).
  (b) **Command routing by active space.** A single rule decides the target store:
  `activeSpaceIndex == kModelSpaceIndex` **or** floating model space → model store
  (geometry in model/world coords); a paper layout active and *not* floating → that
  layout's paper store (geometry in paper inches). Draw (line, text) and edit (move,
  copy, rotate, delete) and object snapping branch on this rule — reusing the command
  surface, mirroring how ADR-008 routes MOVE/COPY/DELETE. Survey tools (survey points,
  CSV) stay model-only. (c) **Snapping** in paper space resolves against paper-space
  entities only; snapping to model geometry shown inside viewports is deferred.
  (d) **Persistence:** paper-space entities serialize per layout in the native `.gs`
  (REQ-031/037); DXF persistence deferred. No new dependency, no new global.
- Alternatives: (a) one global entity store tagged with a space id — rejected: breaks
  single-owner (§10.1), and per-layout ownership matches how viewports/frozen-layers are
  already owned. (b) a separate parallel command set for paper geometry — rejected:
  duplicates draw/edit logic and diverges UX from model space (the user asked for
  parity). (c) render paper geometry via the (reverted) GL paper pass — rejected for
  now: paper space is drawn by the pan/zoom-aware ImGui overlay (see TASK-008 revert);
  paper entities render there in paper inches through the overlay's `w2s` mapping.
- Consequences: `PaperLayout` gains entity vectors; a small "active store" indirection
  lets draw/edit/snap target model vs paper; GsIo gains a per-layout geometry section;
  the overlay draws paper entities. Delivered incrementally (5a data model + persistence,
  5b render + line/text create, 5c move/copy/rotate/delete + snap) so each slice is
  verifiable. Coordinates never cross spaces implicitly: model stays in world coords,
  paper stays in paper inches (§11 — no silent coordinate-space mixing).

### ADR-013 — Full paper-space primitive store + clipboard copy/paste across spaces   (2026-06-17, accepted)
- Context:    REQ-038 adds clipboard copy/paste that works within and **across** model
  and paper space (e.g. copy a DXF title block from model space onto a sheet). Two
  forces meet the existing design: (1) the in-process clipboard (`CadClipboard` on
  `AppCommandState`) and its cursor-following paste preview were built **model-only** —
  `CopySelectionToClipboard` reads only the model selection/arrays and
  `CommitPasteFromClipboard` writes only the model arrays; (2) paper layouts under
  ADR-009/REQ-037 store **only lines + text**, so the other clipboard primitives
  (circles, arcs, ellipses, polylines) have nowhere to land in paper space. Extending
  the paper data model and crossing coordinate spaces are architectural decisions, not
  Workshop choices (architecture §3, §10.1 single-owner, §11.4, §11 no implicit
  coordinate mixing).
- Decision:   (a) **Extend the `PaperLayout` paper-space entity store** from lines+text
  to the **full primitive set** — circles, arcs, ellipses, polylines — each stored in
  **paper inches** (sheet origin 0,0) with a parallel `EntityAttributes` vector,
  reusing the existing entity value types (no new speculative abstraction; §11.4). The
  `PaperLayout` remains the single visible owner (§10.1). These render through the
  pan/zoom-aware ImGui paper overlay (consistent with ADR-009's revert of the GL paper
  pass), participate in paper selection/snap/edit, and **persist per layout in `.gs`**
  (REQ-031/037 pattern); DXF persistence of the new paper types stays **deferred**.
  (b) **Copy/paste routes by active space** (the ADR-008/009 active-space branch
  pattern): copy reads the model selection+arrays in model/floating-model space, or the
  active layout's selection+stores in paper space; paste writes into whichever space is
  active when the placing click happens. (c) **Cross-space paste is an explicit 1:1 raw
  coordinate transfer** — model local units and paper inches are carried verbatim with
  no scale conversion. This is the **one sanctioned exception** to ADR-009's "no
  coordinate-space mixing": it is user-initiated (Ctrl+V into a deliberately chosen
  space), never implicit. No new dependency, no new global.
- Alternatives: (a) **Skip unsupported types on paper paste** (paste lines+text, drop
  curves with a warning) — rejected by the user: title blocks mix circles/arcs and must
  paste intact. (b) **Block any paste containing unsupported types** — rejected: too
  coarse, defeats the title-block use case. (c) **Convert curves to polylines on paste**
  — rejected: lossy and paper had no polyline store either; (a) full store is cleaner.
  (d) **Auto-scale across spaces** (model units → plotted inches by a viewport scale) —
  rejected: ambiguous (which viewport's scale?) and the user chose predictable 1:1 raw.
- Consequences: `PaperLayout` gains four owned vectors (+ attrs); the overlay, paper
  hit-testing/selection-highlight, `SnapPaperInchPoint`, the paper edit commands
  (translate/rotate/delete), `CopySelectionToClipboard`, `CommitPasteFromClipboard`, and
  the `.gs` per-layout section each extend to the new types; Ctrl+C/Ctrl+V wiring is
  unchanged. Supersedes the lines+text-only limit of ADR-009/REQ-037 for the paper-space store.
### ADR-014 — Paper-space object parity by active-space branching + a shared in-place text editor   (2026-06-18, accepted)
- Context:    REQ-039 requires paper-space objects to have the full model-space interaction surface
  — box selection, grips, Properties display+edit, draw/modify commands, and double-click in-place
  text editing. Three reported defects motivate it: paper box-select does not select objects; paper
  text picks/snaps are offset (the bounds helper treats text insertion as bottom-left while text
  renders top-left); the Properties panel is model-only. Extending the paper interaction model and
  adding a new editing UI are architectural decisions, not Workshop choices (architecture §3, §11.4).
- Decision:   (a) **Continue the established active-space-branch pattern** (ADR-008/009/013): paper
  parity is delivered by extending the existing paper selection set (`selectedPaperEntities`), the
  paper hit-test/box-select code, the Properties panel, and the draw/modify command branches — each
  routed by the active-space rule (`activeSpaceIndex`/floating-model). **No new abstraction, layer,
  global, or dependency**; reuse the existing entity value types and the `PaperLayout` paper stores.
  (b) **Properties panel reads the active space's selection**: in a paper layout it binds to the
  active layout's paper objects (General + per-type Geometry + Text) instead of the model selection,
  reusing the same panel and edit-apply paths. (c) **One shared in-place text-edit helper** opens an
  inline editor over a double-clicked text and writes the committed string back to the target's
  `CadAnnotation`; it is called from **two** concrete sites — the model annotation store and the
  active layout's `paperTexts` — satisfying the §11.4 ≥2-use rule (not a speculative abstraction).
  (d) **Fix the text anchor**: `PaperTextBoundsIn` and `SnapPaperInchPoint` treat the text insertion
  as the **top-left** (matching the renderer), removing the ~one-line pick/snap offset.
- Alternatives: (a) a unified cross-space entity/selection abstraction — rejected (§11.4 speculative;
  the project deliberately chose per-space stores in ADR-006/009). (b) a paper-only parallel
  Properties panel — rejected: duplicates the panel and diverges UX (the user asked for parity).
  (c) edit paper text only through the Properties "Contents" field (no in-place editor) — rejected:
  the user wants a double-click in-place editor, and wants it in model space too.
- Consequences: the paper selection/box-select/Properties/draw/modify branches each grow to cover all
  paper object types; one new shared inline-text-edit helper reused by model + paper; the text-anchor
  fix corrects paper text picking and snapping. DXF persistence of paper objects stays deferred (.gs
  only). Delivered incrementally (Phase 1 selection/text-pick/Properties, Phase 2 in-place editor,
  Phase 3 grips, Phase 4 draw + modify) so each slice is independently verifiable.

- Addendum (2026-06-18): solid fills (`CadFilledRegion`, ADR-011) are now clipboard-copyable too.
  `CadClipboard` and `PaperLayout` each gain a `…FilledRegions` (+attrs) vector. Because filled regions
  are **not** an independently selectable `SelectedEntity` type, copy **includes any filled region whose
  vertices are fully enclosed by the selection's bounding box** (window-copy a title block → its logo fill
  comes along); the base point is unchanged (computed from the explicit selection). Paste applies the same
  1:1 offset. Paper fills render in the **ImGui overlay** (no GL stencil in paper, per the reverted GL paper
  pass) via a **screen-space scanline even-odd fill** over all loops — matching the model GL stencil pass's
  even-odd rule, so concave shapes and island holes (e.g. the logo's counters) are correct. Colour resolves
  like the model (`ResolveEntityRgbaForViewport` with the layer row). DXF persistence of paper fills stays
  deferred (.gs only).

### ADR-020 — Document-owned text-style table + bake-on-write resolution with per-property overrides   (2026-06-21, accepted)
- Context:    REQ-044 adds AutoCAD-style named text styles (font, height, oblique, bold/italic) with a
  live reference from each text to its style, per-text Properties overrides, an active style for new
  text, a management dialog, and `.gs` persistence. The font subsystem already exists (ADR-012/012a:
  `FontReg` TTF + `Shx` strokes; `CadAnnotation` already carries `fontFamily`/`bold`/`italic`/
  `plottedHeightInches`), but there is **no named-style concept** — a DXF STYLE is flattened onto each
  text's own font, and the render/measure pipeline reads each annotation's own fields directly in ~12
  sites. Adding a document-owned style table, a `.gs` format addition, and renderer oblique are new
  Domain data + ownership + a data-format change → architectural, not a Workshop choice (architecture
  §3, §10.1 single-owner, §11.4 no speculative types).
- Decision:   (a) **`TextStyle`** value type (name, fontFamily, heightInches, obliqueDeg, bold, italic)
  in `CadEntities.hpp` so model and paper text reuse it (no circular include, mirrors the
  `CadAnnotation`/`EntityAttributes` sharing). The **drawing/document owns** `std::vector<TextStyle>
  textStyles`, threaded through `DrawingDocument`, the undo `DrawingGeometrySnapshot`, and tab
  save/restore exactly like `drawingLayerTable`; the **active style name** lives on `AppCommandState`
  (the settings pattern — no new global). A reserved **"Standard"** style always exists.
  (b) **`CadAnnotation` gains** `styleName`, `obliqueDeg`, and per-property override flags
  (`ovFont/ovHeight/ovOblique/ovBold/ovItalic`). An **empty `styleName` resolves from the annotation's
  own fields** — so legacy/older-file text and DXF-imported text are unchanged.
  (c) **Bake-on-write** resolution (chosen over resolve-on-read): the annotation's existing fields
  always hold the **effective** values, so the ~12 render/measure/export sites are **untouched**.
  Creating text copies the active style's properties into the new annotation; **editing a style
  re-bakes** every referencing annotation's non-overridden fields from the new style values; a
  Properties edit sets that property's override flag. A pure, unit-tested helper (the
  NumFormat/AngleFormat/SurveyCsvValidate precedent) owns resolve/re-bake/ensure-Standard. The live
  reference (≥2 uses: create + style-edit re-bake + Properties) is a concrete function, not an
  abstraction (§11.4).
  (d) **Persistence:** add an additive top-level `textStyles` array and the new annotation fields to
  the `.gs` JSON, read tolerantly with defaults and **no `kGsFormatVersion` bump**, so older files
  still load (a missing table synthesizes "Standard"). DXF STYLE-table round-trip is **deferred**.
  (e) **Oblique rendering:** true shear for SHX stroke text (transform stroke points by
  `x += y·tan(oblique)`); best-effort/faux for TTF (ImGui has no glyph shear) — a recorded limitation.
  No new dependency, no new global.
- Alternatives: (a) **resolve-on-read** (every render site calls a resolver) — rejected: changes a
  dozen hot/tested sites for no user-visible gain; bake-on-write preserves the live semantic via
  re-bake at far lower regression risk. (b) **template-only styles** (no live reference) — rejected by
  the user (they want editing a style to ripple and to override specific text). (c) **color as a style
  property** — rejected by the user (AutoCAD-faithful: color stays a layer/object property). (d) **a
  unified cross-space style/entity abstraction** — rejected (§11.4 speculative; per-space stores already
  chosen in ADR-006/009). (e) **bump the `.gs` version** — rejected: the strict version-equality check
  (GsIo.cpp) would reject older files; additive tolerant keys keep them loadable (REQ-044 acceptance).
- Consequences: `TextStyle` + one document-owned vector threaded like `drawingLayerTable`; `CadAnnotation`
  grows a style ref + override flags + oblique; one pure resolve/re-bake helper reused by create/edit/
  Properties; `.gs` gains an additive section with no version bump; the renderer learns SHX oblique
  (faux for TTF). Dangling `styleName` (after a delete) is safe — the resolver treats an unknown style as
  legacy (own fields); the dialog blocks deleting "Standard"/in-use styles. Delivered incrementally
  (Phase 1 data model + persistence + active dropdown + create; Phase 2 STYLE dialog + re-bake; Phase 3
  Properties overrides + oblique) so each slice is independently verifiable.

### ADR-024 — DWG support in phases: an external-converter route first, a native codec after   (2026-07-30, accepted)
- Context:    REQ-052 requires opening and saving DWG. DWG is Autodesk's proprietary native format: it is
  bit-packed rather than byte-aligned, paged and compressed with a custom LZ77 variant, CRC-guarded, and
  organised as a handle graph rather than a stream. Autodesk publishes no specification; the only public
  description is the ODA's reverse-engineered document, which stops at R2013. The reference drawing the
  user supplied (`26-084 - Master.dwg`) is **AC1032 / R2018 — undocumented anywhere**. A from-scratch
  reader plus writer is a 10,000+ line, multi-month effort. Choosing how DWG is read at all is an
  architectural decision (§3, §11), not a Workshop choice, so it was escalated as a SPEC GAP and decided
  by the user.
- Decision:
  (a) **Phase 1 converts DWG ↔ DXF out of process** and reuses the existing `io/DxfIo`. `io/DwgIo` owns
  converter discovery (an env override, then ODA File Converter, then any installed AutoCAD
  `accoreconsole`), the temporary working directories, and the conversion; it exposes only
  `ImportDwgFile` / `ExportDwgFile` / `DwgVersionName` / `FindDwgConverter`. **That four-function seam is
  the point**: replacing the converter with a native codec later changes nothing above `io/`.
  (b) **`platform/ProcessRun` is a new Platform-layer module** holding the one thing IO must not know:
  how to launch a child process, quote its arguments, and bound its lifetime. IO → Platform is a downward
  dependency (§2), so `io/DwgIo` may use it.
  (c) **Phase 1's save is explicitly lossy and says so before writing.** The payload is the DXF export, so
  blocks, extra layouts, elevations, attributes and proxies cannot survive. The destination is written only
  after a good converted file exists.
  (d) **Later phases build a native in-tree codec** (see `docs/dwg-plan.txt`), reading R2000→R2018 and
  writing at least R2000. Phase 1 doubles as the **test oracle** for that work: the same drawing can be
  parsed natively and by the converter and the two results diffed.
- Alternatives: (a) native codec first — rejected as the *first* step only: it delays any DWG capability by
  months, and R2018 must be reverse-engineered against samples, which is far easier with a working oracle.
  It remains the destination. (b) vendor **LibreDWG** — was excluded when the licence question was open
  (GPL-3.0 would relicense GoSurvey); the user has since confirmed GoSurvey is open-source and
  GPL-compatible, so LibreDWG is **back on the table for the native phase** and should be reconsidered
  there rather than writing a codec from scratch. (c) licence the **ODA Drawings SDK** — correct and
  complete, but a paid annual membership and a heavy binary SDK in a repo whose `third_party/` is a single
  header. (d) treat the converter route as permanent — rejected: it requires software GoSurvey does not
  ship, and it can never satisfy the user's decision that a save must preserve objects GoSurvey does not
  model, because DXF cannot carry them.
- Consequences: two new modules (`io/DwgIo`, `platform/ProcessRun`); DWG menu entries disable themselves
  with an explanatory tooltip when no converter is present; `AppCommandState` gains two fields for the
  export confirmation. **DWG capability is gated on software the user installs** — acceptable for Phase 1,
  never for the shipped product, which is why the native phase is not optional. The known-lossy save is
  recorded technical debt with an explicit removal condition: it is retired when the native writer plus
  the unknown-object preservation channel land. Risk acknowledged: users may read "GoSurvey saves DWG" as
  lossless, which is why the confirmation dialog enumerates what is dropped rather than warning vaguely.

#### ADR-024 addendum — native codec is LibreDWG   (2026-08-29, accepted)
Phase 1 (converter) is unchanged as **shipped history**. The native phase in (d) is **ADR-041 /
REQ-170**, not a from-scratch codec and not ODA. DWG write in that epic is R2000/R2004 only.
See `spec/file-format-specs.md` and D-2026-08-29-g.

### ADR-023 — WYSIWYG MTEXT editing: an offset-carrying rich-span API + an in-tree rich text edit widget   (2026-07-30, accepted)
- Context:    REQ-051 delivered the "Text Formatting" panel over ImGui's `InputTextMultiline`. That widget
  has **no word wrap**, so the in-place box cannot grow as text reaches the MTEXT's column width, and the
  user edits the raw wire string with `[[b]]…[[/b]]` tags visible. The user chose full WYSIWYG: text wraps
  at the column, the box grows with it, formatting renders as formatting, and tags never appear. No stock
  ImGui widget does this, so a new editing widget is required — a new UI module and a public-API addition
  to `MtextRichFormat`, both architectural decisions rather than Workshop choices (§3, §11.4).
- Decision:
  (a) **`MtextRichFormat` gains an offset-carrying span API.** Its run parser is internal and discards
  where each run came from; the editor needs exactly that. `MtextRichBuildSpans(wire, &spans)` returns each
  text span's `[rawBegin, rawEnd)` byte range in the wire plus its resolved styling. The existing internal
  `BuildRuns` is re-expressed in terms of it, so there is **one** parser, not two that can disagree.
  (b) **A new UI module `ui/RichTextEdit`** owns the widget: layout (wrap the spans' visible characters
  into lines at a column width), the caret/selection model, key and mouse input, and styled drawing. It is
  a concrete widget with a single call site, not an interface or a template — §11.4 governs speculative
  abstraction, and this is the module that *implements* one required behavior, not indirection over two.
  (c) **The caret and selection anchor are VISIBLE character indices**, and the widget publishes the
  corresponding **raw byte offsets** into the existing `mtextRichEditorSelStart/End`. This is the load-
  bearing choice: every toolbar control (B/I/U, caps, the font picker, the colour swatch) already works by
  wrapping a raw byte range, so all of them keep working **unchanged**. It also means an edit may freely
  re-run `MtextRichNormalize` — normalisation preserves visible text, so a visible-index caret survives it.
  (d) **Typed text inherits the styling to its left**: the insertion point for visible index `i` is the end
  of visible character `i-1`, which lands *inside* the preceding run rather than before its opening tag.
  (e) **The editor renders at the MTEXT's own on-screen size** (the size the viewport draws it at, floored
  at a legible minimum), and wraps at the box width the ruler drag sets — so the editing view matches the
  committed result.
- Alternatives: (a) keep `InputTextMultiline` and accept no wrap — rejected by the user. (b) hard-wrap the
  buffer on commit — rejected: it rewrites the user's text, and re-widening the column cannot reflow it.
  (c) vendor a third-party text-editor widget (e.g. ImGuiColorTextEdit) — rejected under the REQ-300
  dependency policy: it is a code-editor with no rich-run model, so it would need as much adaptation as
  writing the layout, plus a dependency. (d) model selection as (run, offset) pairs instead of raw byte
  offsets — rejected: it would force a rewrite of every toolbar control for no gain, since the raw offset
  is recoverable from the visible index anyway.
- Consequences: `MtextRichFormat` grows a public span API (its internal parser refactored beneath it, no
  behavior change); `ui/RichTextEdit` is new; `DrawMtextRichEditorOverlay` swaps the widget and keeps
  publishing raw offsets. **The rich wire format, `CadAnnotation`, `.gs`, DXF, and the PDF plot are all
  unchanged** — this is an editing-surface decision only. The widget must re-provide what the stock one
  gave for free: caret movement, shift/mouse selection, word double-click, clipboard, and an in-editor
  undo stack. Pure layout and index-mapping logic is unit-tested; drawing and input stay manual, per the
  UI convention. Risk acknowledged: this replaces a working editor, so MTEXT editing is the blast radius.

### ADR-025 — 3D model space: additive Z storage, a Camera value type, and ray-based input   (2026-08-11, accepted)
- Context:    REQ-057–061 move GoSurvey from a plan-view 2D drawing surface to a true 3D model space.
  The obstacle is not the camera math — it is that **coordinates are not behind a point type**. They live
  in flat `std::vector<float>` arrays with implicit strides (`userLinesFlat` 4, `userCirclesCxCyR` 3,
  `userPolylineVerts` 2, `CadFilledRegion::verts` 2) plus loose scalar fields (`.cx`/`.cy` on arcs and
  ellipses, `insX`/`insY` on annotations) — roughly **1,450 reference sites**, and each store exists in
  **three** copies (live `AppCommandState`, the undo `DrawingGeometrySnapshot`, and the per-tab struct).
  Input is equally 2D: one plan-view `w2s` mapping with ~40 call sites in `CadUi.cpp`, and picking/snapping
  written against screen-space distance in X/Y. Choosing the storage layout, the camera model, and how a
  click becomes a world coordinate are architectural decisions, not Workshop choices (§2, §5, §11).
- Decision:
  (a) **Z is interleaved, and every geometry store uses the same convention.** *(Amended 2026-08-11 —
  see the correction note below; the original D1 specified parallel sidecar Z arrays and was decided on
  an incorrect reading of the existing strides.)* Two of the four flat stores are **already XYZ**:
  `userLinesFlat` is stride 6 (`x,y,z,x,y,z` per segment) and `userPolylineVerts` is stride 3 (`x,y,z`),
  both writing a hard-coded `0.f` into a Z slot that has always existed — which is why the GL vertex
  format is already 3-component. Those two need **no structural change**, only real values at the append
  sites. The two stores that lack a Z slot are widened to match: `userCirclesCxCyR` (`cx,cy,r`) becomes
  **`userCirclesCxCyZR`** (`cx,cy,z,r` — the centre's XYZ stays contiguous), and `CadFilledRegion::verts`
  (`x,y`) becomes **`vertsXyz`** (`x,y,z`). `CadArc`, `CadEllipse` and `CadAnnotation` gain a scalar `z`
  / `insZ`. **Both widened arrays are renamed as part of the widening**: the rename makes every one of the
  ~52 affected sites a **compile error** rather than a silently-misread stride, which converts the exact
  hazard the original D1 was invented to avoid into a problem the compiler solves. The result is one
  uniform interleaved-XYZ convention across all geometry, honouring §5 (a coordinate is one cache line)
  instead of conceding against it.
  (b) **Z is absolute** — no `worldDocumentOriginZ`. The local-storage invariant (`world = local +
  worldDocumentOrigin`) stays **X/Y-only**, and that asymmetry is documented at the invariant's definition
  in `CadCoordinateFrame.hpp`. The origin exists for 1e6-ft state-plane easting/northing; elevations span
  roughly −1,000…30,000 ft, where float resolves ~0.002 ft against REQ-101's 0.01 ft.
  (c) **A `Camera` value type** (eye, target, up, projection mode, fov/extent, near/far) owned by the
  Renderer layer, producing view and projection matrices. It is a **value, not an abstraction**: it has
  three present-day uses (the model viewport, each paper-space `Viewport` under REQ-061, and the PDF plot),
  satisfying §11.4. There is **no camera interface, no scene graph, and no second rendering backend** — the
  anti-requirement holds, this stays OpenGL.
  (d) **Input becomes ray-based, in a pure module.** Screen → world ray, ray × plane, and ray-to-entity
  distance live in a dependency-free unit-testable module beside `util/geom2d`, so the 3D picking and
  snapping math is tested without a GL context or a window (the ADR-002 layering pressure). The existing
  `w2s` plan-view mapping becomes the degenerate case of the camera transform rather than a parallel path,
  so there is one transform, not two that can disagree.
  (e) **Drawing resolves against an active work plane (UCS)** stored on `AppCommandState` (the settings
  pattern — no new global), defaulting to world XY so plan-view behaviour is unchanged.
  (e·2) **Per-viewport active UCS (REQ-155, D-2026-08-31-c).** The drawing-scoped `activeUcs` on
  `AppCommandState` continues to own the frame for the single non-floating model-space view. In
  addition, each paper-space `Viewport` carries an **active UCS frame** — a `ucs::Ucs` value,
  default World, typically one of the drawing's named UCSs but able to hold an ad-hoc frame built
  while floating (AutoCAD `UCSVP`). While **floating model space** (REQ-036) is entered for a
  viewport, that frame is what coordinate entry, the grid, ORTHO, the readout and `UCSFOLLOW`
  resolve against; `UCSFOLLOW=1` re-plans that viewport's REQ-061 camera only. Named UCS
  **definitions** and named views stay per drawing — one owner, `AppCommandState.ucsNamed` (§10.1).
  This is a value field on an existing owned type (`Viewport`, owned by `PaperLayout`) — the
  viewport holds a working frame exactly as the drawing holds `activeUcs` — not a new abstraction
  (§11.4) and not a new owner.
  Persisted additively in `.gs`. **Split model space (multiple simultaneous model viewports) stays
  out of scope** — a future decision, not this one.
  (f) **Two vendored dependencies** (REQ-300, decision log 2026-08-11): **ImGuizmo** (MIT) for the REQ-060
  manipulator and **ImOGuizmo** for the REQ-059 orientation gizmo. Both consume the matrices (c) produces
  and neither introduces a rendering abstraction. They are `third_party/` code and are not modified in
  place except through a recorded fork decision. **ImOGuizmo ships unmodified**: it draws its stock
  axis-ball, and REQ-059 was amended the same day to drop the labelled-cube + compass-ring mockup as a
  target rather than fork the header (decision log, 2026-08-11 — the user's ruling on FINDING-2). If that
  appearance is wanted later, forking this header is the cheapest route and needs a new decision entry.
  (g) **Paper-space sheet geometry stays 2D.** A sheet is 2D by definition; the ADR-009/013 `PaperLayout`
  stores are untouched. Only `Viewport` gains a camera (REQ-061), persisted additively in `.gs` with no
  `kGsFormatVersion` bump, so older files load with every viewport in plan view (the ADR-020 (d) precedent).
- **Correction note (2026-08-11).** As first written, (a) specified parallel sidecar Z arrays for every
  store, justified by a claim that `userLinesFlat` was stride 4 and `userPolylineVerts` stride 2, putting
  ~1,450 coordinate sites at risk from any stride widening. **That claim was wrong** — it was inferred from
  a grep pattern rather than from reading an append site. `userLinesFlat` has always been stride 6 and
  `userPolylineVerts` stride 3, both already carrying Z. The real structural work is ~52 sites across the
  two stores that genuinely lack a Z slot. The error was caught in TASK-034 step 1, before any storage code
  was written; the task was marked **blocked: SPEC GAP** and the user ruled to widen rather than keep the
  sidecar design. Recorded here rather than quietly rewritten, because the original rationale is what
  justified architecture invariant §11.8, which this amendment deletes.
- Alternatives: (a) **parallel sidecar Z arrays** (the original D1) — now rejected: with lines and polylines
  already interleaved, sidecars for the remaining two stores would leave the codebase with two conventions
  for the same concept, and the desync hazard they introduce is worse than the 52-site edit they avoid.
  (b) **migrate to `std::vector<Vec3>`** —
  safest of all (a missed site is a compile error) but the largest diff by far, rewriting all ~1,450 sites
  plus the GL upload path, `.gs`, DXF and the undo snapshots; swapping the storage model is a bigger change
  than adding 3D. (c) **2.5D — Z as data with the viewport left in plan** — rejected by the user: a ViewCube
  with no orbit is decoration. (d) **write the gizmos in-tree** — rejected by the user under (f). (e) **a
  scene-graph / camera-hierarchy abstraction** — rejected as speculative (§11.4); a camera is a value type.
- Consequences: geometry gains a parallel Z array per store, tripled across the three copies, and all of it
  funnels through one mutation helper — the single most important invariant this ADR adds. The renderer
  learns a matrix pipeline and depth handling; `w2s` collapses into it. Picking and snapping become ray
  tests in a new pure module. `AppCommandState` gains a camera and a UCS. `.gs` gains additive Z and
  per-viewport camera keys with **no version bump**; DXF group 30 stops being discarded. Two small
  third-party files enter `third_party/`. **REQ-100 becomes a real gate** for the first time (decision log,
  same day), because orbit makes framerate user-visible. Blast radius acknowledged: this touches the two
  12.5k-line files, the renderer, all of IO, snapping, picking and paper space — which is why it is split
  into five independently shippable requirements rather than one, each passing Verification on its own.

### ADR-026 — Imported 3D models: a mesh entity, a glTF reader, and visual styles   (2026-08-12, accepted)
- Context:    REQ-063/064/065 exist because of a concrete file: `ENTERPRISE PIPING.dwg`, an AutoCAD
  Plant 3D model of a pipe rack. Analysing it settled what is and is not possible, and the ADR turns on
  those facts rather than on preference:
  - **267 model-space objects, of which 255 (95%) are Plant 3D custom objects** (`ACPPPIPE` 76,
    `ACPPCONNECTOR` 110, `ACPPPIPEINLINEASSET` 59, `ACPPSTRUCTUREBEAM` 10). Only 11 are real `3DSOLID`s
    (the concrete foundations) and one is a point-cloud reference.
  - Those `AcPp*` classes resolve **only** with Autodesk's Plant 3D object enabler. A probe on this
    machine reported zero proxies purely because Plant 3D is installed; to any reader without the
    enabler — LibreDWG, ODA's base SDK, GoSurvey — they are proxy stubs with no geometry. The enabler
    is not licensable to an independent application. **No amount of work on our own DWG codec (ADR-024)
    reaches this geometry.** That is the fact that forces an interchange format.
  - `STLOUT` on the model reported "266 found" and wrote a file containing **952 triangles** — the 11
    foundations. It silently discarded every piping object. A pipeline built on it would look like it
    worked and would lose 95% of the model (the REQ-201 hazard, arriving from outside our code).
  - `EXPORTTOAUTOCAD` (custom objects → plain entities) ran 7+ minutes at 867 MB without producing a
    file before being stopped. Inconclusive, but not a foundation to build on.
- Decision:
  (a) **Interchange, not custom-object decoding.** GoSurvey reads a neutral tessellated format and does
  not attempt to decode vendor custom objects, now or later. This is a boundary, not a staging post:
  the alternative requires a licence we cannot obtain.
  (b) **glTF 2.0 (`.gltf` + `.glb`) is that format** (REQ-065). It carries the four things the target
  actually needs — triangles, vertex normals, per-object names, and base colours — in one binary file,
  and it is the only candidate that needs no second format to be useful. Chosen over: **OBJ** (text,
  verbose, materials in a sidecar `.mtl`, no normals guarantee), **FBX** (proprietary, binary variant
  awkward, far larger surface for the same result), **STL** (triangles only — no colour, no names, no
  usable normals, so every model renders as one grey blob) and **STEP/ACIS** (B-rep: needs a geometry
  kernel to tessellate, which is a larger project than everything else here combined).
  (c) **A mesh is a new entity type, and it is reference geometry** (REQ-063). It stores interleaved XYZ
  positions (§11.8 applies unchanged), one normal per vertex, `uint32` indices, and a per-part colour.
  GoSurvey **does not author or edit meshes**: no command creates one, no grip moves a vertex, and they
  are excluded from DXF/DWG export, which has no lossless representation. They are visible, selectable,
  erasable, layer-controlled, and included in extents. Treating them as draftable would drag mesh
  editing, mesh snapping and mesh export into scope for no requirement that asks for it.
  (d) **A parser is written in-tree; no glTF library is vendored.** glTF is JSON plus a binary buffer,
  and the subset REQ-065 needs — `POSITION`, `NORMAL`, indices, node transforms, `baseColorFactor` — is
  a few hundred lines against a published spec. The dependency policy's three questions (project.md §7)
  answer "yes / marginal / partly": it can be done simply in-tree, and a full library carries texture,
  animation, skin, sparse-accessor and extension handling that REQ-065 explicitly excludes. **The parser
  is a pure module** beside `util/curveintersect` and `util/benchscene` — dependency-free, so it is unit
  tested without a GL context, which is the standing lesson of TASK-035 §11.
  (e) **Visual styles turn depth testing on; 2D Wireframe keeps it off** (REQ-064). This supersedes
  ADR-025 ASSUMPTION-1, which left depth testing disabled precisely until a visual-style requirement
  existed. Style is per-viewport state (each REQ-061 `Viewport` carries its own), and **2D Wireframe
  must stay pixel-identical to today** — the same parity gate REQ-058 was held to, for the same reason:
  every existing drawing is a 2D wireframe drawing.
  (f) **Shading needs a second shader, not a rendering abstraction.** The line shader stays; a triangle
  shader with a camera-space headlight is added beside it. No material system, no scene graph, no render
  graph, no backend abstraction — the ADR-025 (c) anti-requirement stands, this remains OpenGL with
  concrete draw paths. Two shaders are two shaders.
  (g) **Meshes are excluded from object snapping in this ADR.** Snapping to a mesh means snapping to
  vertices or faces of a tessellation, which is a different question from snapping to CAD geometry
  (a tessellated cylinder has no centre, and its "vertices" are artefacts of the export resolution).
  If it is wanted it is its own requirement, and the REQ-062 pairwise-cost analysis applies with far
  more geometry.
- Alternatives: **(1) decode Plant 3D objects directly** — impossible without Autodesk's enabler, as
  above. **(2) Route via `EXPORTTOAUTOCAD` → `3DSOLID` → our DWG reader** — still ACIS B-rep at the end,
  so it needs a kernel; and the export step did not complete here. **(3) STL only** — the smallest
  parser, but it discards colour and object identity, which is most of what makes the reference
  screenshot readable; and on this file it silently drops the piping. **(4) Vendor a glTF library**
  (cgltf/tinygltf) — reconsider if the in-tree parser exceeds ~600 lines or a real file needs sparse
  accessors or Draco; that is the trigger to revisit, recorded here so the choice is not re-litigated
  from scratch. **(5) Point-cloud import instead** — the source project is a scan and does reference a
  point cloud, which for a survey tool may be worth more than piping solids; it is a different
  requirement and is not displaced by this one.
- Consequences: a new entity store, a new `.gs` section (additive, no version bump — the ADR-020 (d)
  precedent), a second shader and the first use of the depth buffer that `msDepthRbo_` has always
  allocated. Selection, extents, layer state and the undo snapshot all grow a mesh case; DXF/DWG export
  grows an explicit, logged exclusion rather than a silent one. **REQ-100 gains a second dimension** —
  the budget is defined on 250k line segments, and a shaded mesh scene is a different cost profile, so
  the bench needs a mesh case before REQ-064 can claim the budget. Not addressed here and deliberately
  left open: mesh snapping (g), textures, and any editing of imported geometry.

#### ADR-026 addendum — DWG as an import route   (2026-08-12, accepted)
- Context: ADR-026 (a) ruled out decoding vendor custom objects and (b) chose glTF as the interchange
  format. Both hold. What the original ADR got wrong was **assuming a glTF producer would be
  available**: it named Navisworks, which is not installed on the reference machine, so the decision
  left the user with a format they had no way to produce. That is the gap this addendum closes.
- Decision: **GoSurvey converts DWG 3D content itself, by driving an installed AutoCAD.** The chain
  is EXPLODE (the vendor's own object enabler emits plain 3D solids — the one thing it will do for
  us without a licence) → STLOUT (tessellation) → STL → mesh. This is **not** a new mechanism: it is
  ADR-024's converter route applied to 3D content, reusing that ADR's `FindDwgConverter` discovery
  and the existing `RunProcessAndWait`. STL becomes a supported input format in its own right,
  because it is the chain's intermediate and costs one small parser.
- Consequences: importing a DWG now depends on an installed AutoCAD **at import time**, which is a
  runtime dependency on software we do not ship — stated to the user when absent, and specifically:
  an ODA File Converter is *not* sufficient (it translates DWG→DXF but cannot tessellate solids) and
  says so by name. The conversion runs on a **copy**, because it explodes the model and must never
  touch the user's file. What survives is geometry, position and scale; what does not is per-object
  colour and naming, since STL carries neither — so a DWG import is one grey part where a glTF import
  keeps its structure. **glTF remains the preferred route** and the one to use when a producer
  exists. Recovering colour by grouping exploded solids per colour is the obvious next step and is
  deliberately not attempted here.

### ADR-027 — Stable entity identity   (2026-08-12, accepted)
- Context:  Raised as a **blocking Verification finding against REQ-069**, before any code was
  written. A dynamic surface must reference the polylines used as its breaklines and boundaries.
  GoSurvey has no way to do that safely: `EntityAttributes` carries layer, colour, linetype,
  lineweight and transparency — **no identity** — and entities are addressed by their index into flat
  parallel arrays that **compact on erase** (`ErasePolylineByIndex`, `EraseCadAnnotationAtIndex`).
  A stored index therefore does not survive the deletion of any earlier entity; it silently comes to
  mean a *different* entity. The failure is invisible — the surface rebuilds, against the wrong
  breakline.
  The codebase already demonstrates both the pattern and its ceiling. `SurveyPoint::labelMtextAnnIndex`
  points at an annotation by index, and correctness is bought with a decrement loop inside the erase
  function plus roughly **46 maintenance sites across 7 files** for that one reference. It is
  survivable at one reference maintained at one erase site; REQ-069 would have added several more,
  each needing fix-up at every erase path of every entity type, plus undo restore, DXF-import
  replacement, and paste. A missed site does not crash.
- Decision:
  (a) **Every entity carries a per-drawing `uint64` id**, assigned from a monotonic counter at
  creation, persisted in `.gs`, and **never reused within a drawing** — so a reference to a deleted
  entity resolves to *nothing*, which is the whole point. The counter is per drawing, not global, so
  ids are stable across sessions and independent of tab order.
  (b) **Cross-object references are stored by id.** Storing an index across an object boundary
  becomes architecture invariant §11.9, a blocking finding.
  (c) **Resolution is by an index built on demand**, not a stored per-entity map. The dominant access
  is "resolve a definition's handful of ids at rebuild", not "look up an id every frame", so a map
  kept permanently in sync would be cost and desync risk paid for nothing (§5).
  (d) **Legacy drawings are assigned ids at load**, deterministically by entity order, so nothing
  above the IO layer has a legacy case to handle.
  (e) **`labelMtextAnnIndex` migrates to an id and the decrement loop is deleted.** The existing
  index reference is not left beside the new mechanism — two conventions in one codebase is exactly
  what ADR-025's stride correction was reversed to avoid.
- Alternatives: **(1) surfaces snapshot breakline geometry at add time** — no identity needed, but
  breaklines become static while point groups stay dynamic, a split model the user would feel on
  every grade-break adjustment; offered and declined. **(2) Defer breaklines entirely** — smallest
  scope, but a surface without breaklines cannot represent a curb, swale or ridge, which is most of
  grading; offered and declined. **(3) Tombstones — mark erased entries dead instead of compacting**
  — keeps indices valid without adding a field, but leaks memory over a session, complicates every
  iteration site in the renderer, and makes `.gs` files grow with deletions. **(4) Generational
  handles (index + generation)** — the standard game-engine answer, and a good one, but it only pays
  off with slot reuse, which (3) already rejected; a plain monotonic id is simpler and enough.
- Consequences: this touches entity creation for every type, `.gs` (an additive per-entity field),
  copy/paste (a pasted entity gets a **new** id — it is a different object), DXF/DWG import, and undo
  snapshot restore. It is a **prerequisite for REQ-069** and is sequenced ahead of it. It also pays a
  debt: the `labelMtextAnnIndex` sprawl gets deleted rather than extended, and every future
  cross-reference — dimensions to their measured entities, labels to their objects, future feature
  lines — becomes free rather than being another 46-site obligation. Ids are **not** exposed in the
  UI and are **not** a user-facing handle in this ADR; if a `SELECT id` or scripting surface is
  wanted later, that is its own requirement.

### ADR-028 — TIN surfaces: a definition-driven model, in-tree constrained Delaunay, and style-generated display geometry   (2026-08-12, accepted)
- Context:  REQ-066…075 add terrain modelling for grading and drainage. Three properties of the
  existing codebase decide most of the design before preference enters: the undo snapshot deep-copies
  every geometry array across 50 frames (so a large payload must be shared, not copied — §11.5);
  coordinates are interleaved XYZ floats in local storage space (§11.8, plus the local-storage
  invariant); and REQ-064 already put a triangle shader and the depth buffer in place, so shading a
  surface needs no new rendering mechanism.
- Decision:
  (a) **A surface is a definition plus a derived triangulation, and the two have different
  lifetimes.** The definition (ordered point groups, breaklines, boundaries — REQ-069) is small,
  editable, and lives as a plain member. The triangulation is large, **immutable, and replaced
  wholesale on rebuild**, so it is held as `shared_ptr<const CadTin>` exactly as `CadMesh` is
  (§11.5). At the REQ-100 surface profile — 100k points, ~200k triangles — a by-value TIN would cost
  roughly 7 MB per undo frame, ~350 MB of undo stack, re-paid by every unrelated edit. This is the
  same trap TASK-041 found for meshes, seen coming this time rather than after the fact.
  (b) **Contours, bands, arrows and the border are display geometry generated from the style, never
  entities** (REQ-070). They are not stored in `.gs`, are not selectable, and do not appear in entity
  counts. This is what makes "change the interval" instant and keeps a 1-ft interval on a large topo
  from putting hundreds of thousands of polylines into the file and into every undo snapshot.
  REQ-071's EXTRACT is the deliberate, explicit escape hatch when real polylines are wanted, and what
  it produces is **unlinked** — a bake, not a live view.
  (c) **Constrained Delaunay is written in-tree, in `util/`, as a pure GL-free module** beside
  `curveintersect`, `gltfimport` and `benchscene` — so it is unit tested without a GL context, the
  standing lesson of TASK-035 §11. The REQ-300 three questions answer yes / yes / yes: it is a
  well-published algorithm, the realistic libraries are either licence-incompatible (Triangle is
  non-commercial) or enormous (CGAL), and it is the one part of this feature we most need to be able
  to debug ourselves. **Revisit trigger, recorded so it is not re-litigated from scratch:** if the
  module exceeds ~1,200 lines, or fails robustness on real survey data, reconsider a small
  header-only CDT library.
  (d) **Geometric predicates are computed in `double`; storage stays `float`.** Orientation and
  in-circle tests are the classic float-instability case: a sign flip yields a visibly wrong triangle
  or a non-terminating edge-flip loop, and REQ-101's ±0.01 ft leaves no margin for it. Coordinates
  are widened at the predicate, not in the store — §11.8 is unchanged.
  (e) **Rebuild is a §8 one-shot worker, coalesced per command.** The definition is marked dirty by an
  edit and **at most one** rebuild is issued per command / undo boundary, so a MOVE of 500 points
  rebuilds once. The worker gets a **copy** of its inputs and holds no pointer into
  `AppCommandState`; its result carries the definition generation it was computed from and is
  **discarded** if that generation is stale on completion. This is the existing `AsyncBuild` pattern
  (`AppCommandState::pdfAttachAsync`) as its second concrete use — which is what makes writing it
  into §8 legitimate rather than speculative (§11.4).
  (f) **Surfaces are not written to DXF or DWG, and the exclusion is logged** (REQ-068) — the ADR-026
  (c) precedent for meshes, for the same reason and with the same REQ-201 obligation. Extracted
  contours are ordinary polylines and export normally.
  (g) **Point groups are rules, not lists, and are not entities** (REQ-067). No geometry, no layer, no
  selection, not drawn. They resolve against the current point set on demand, which is what makes a
  surface pick up points imported after it was defined. `SurveyPoint` already carries a stable `id`,
  so groups needed no part of ADR-027.
  (h) **No new abstraction.** A surface is a concrete type, triangulation is a free function over
  arrays, the style table is the ADR-020 document-owned-table pattern, and shading reuses the REQ-064
  triangle shader. There is no surface interface, no analysis-plugin seam, and no scene graph — the
  ADR-025 (c) / ADR-026 (f) anti-requirement stands.
- Alternatives: **(1) contours as real entities** — editable and exportable immediately, but they go
  stale against the surface the moment it rebuilds, and the entity count is punitive; declined by the
  user in favour of (b) + EXTRACT. **(2) A static surface — snapshot the inputs, rebuild on command**
  — much simpler (no identity requirement, no worker, no coalescing) but the definition is not
  editable afterwards and every change is a manual rebuild; offered and declined. **(3) Rebuild
  synchronously on the UI thread** — keeps the codebase single-threaded, at the price of freezing the
  UI on every edit to a large surface; offered and declined. **(4) Grid/DEM surfaces instead of a
  TIN** — cheaper to contour and analyse, but a grid cannot represent a breakline, which is the
  feature's whole point. **(5) Grading objects and feature lines in the same release** — the Civil 3D
  workflow; explicitly out of scope, and a separate milestone once surfaces are trustworthy.
- Consequences: new pure modules under `util/` (triangulation, contouring, surface analysis); new
  domain stores for surfaces, point groups and surface styles, each growing a case in selection,
  extents, layer state, the undo snapshot and `.gs`; a second use of the REQ-064 triangle shader; and
  a **third REQ-100 cost profile** with its own bench case, since contour regeneration is a per-frame
  cost that neither the segment nor the mesh profile measures. **Sequencing is forced**: REQ-076 /
  ADR-027 precedes REQ-069, and REQ-068 precedes everything that analyses a surface. Deliberately
  left open and not designed for: contour smoothing (linear contours only), proximity / wall /
  non-destructive breaklines, surface import from Civil 3D, DEM and point-cloud sources, and grading
  design objects.

### ADR-029 — Distribution: a CI-built installer, a manifest asset, and an updater with no updater binary   (2026-08-15, accepted)
- Context: releases are built by hand today — CMake bumped locally, a fresh `<version>.iss` copied
  from the previous one with absolute `C:\Users\chetj\...` paths inside it, ISCC run on the developer
  machine, the result uploaded manually. Two `.iss` files already exist (`0.3.1`, `0.4.0`) and the
  `installer/` directory is **gitignored**, so the script that produces the shipped artifact is not
  under version control. Meanwhile an installed copy has no way to learn that a newer one exists, and
  the executable is named `GoSurvey-<version>.exe`, so the path to the running program changes with
  every release. REQ-077, REQ-078 and REQ-202 are the response. The binding constraint is REQ-300:
  the project has **no networking code and no HTTP dependency anywhere**, and an updater needs one.
- Decision:
  (a) **The CMake `project(VERSION)` is the single source of the version.** A generated
  `Version.hpp` (`configure_file`) gives the application its version; CI reads the same value to
  drive the installer's `AppVersion`, the git tag, and the manifest. Nothing else stores a version
  number. The value names the release being *worked toward*, so it is bumped after a stable release,
  not before one, and every beta in the cycle is `<version>-beta.<n>`.
  (b) **The executable is renamed to a stable `GoSurvey.exe`**, with `[InstallDelete]` sweeping
  `GoSurvey-0.*.exe` out of existing installs. A version-stamped filename breaks shortcuts, the `.gs`
  association's `shell\open\command` path, and any form of self-replacement — it is incompatible with
  REQ-078 as written.
  (c) **One tracked, parameterized `installer/GoSurvey.iss`** replacing the per-version copies. Paths
  are relative to the script; version and source root arrive as ISCC `/D` defines with `#ifndef`
  defaults so a local double-click still works. `installer/` is un-ignored except its `Output/`.
  (d) **The update manifest is a release asset, not the GitHub API.** `stable` reads
  `releases/latest/download/latest.json`, which GitHub defines to exclude prereleases — so a stable
  install is *structurally* unable to see a beta, rather than filtering one out in client code.
  `beta` reads `releases/download/channel-beta/latest.json`, a fixed tag whose assets CI clobbers.
  Both are permanent URLs needing no authentication and subject to no rate limit.
  (e) **HTTPS via WinHTTP** (`winhttp.lib`). It ships with Windows and brings TLS, so it satisfies
  REQ-300 without adding a dependency at all. JSON parses with the already-vendored nlohmann.
  (f) **There is no updater executable.** Applying an update means running the downloaded Inno
  installer with `/SILENT /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS`; Inno already knows how to close
  the app, replace files, and relaunch. This requires an `AppMutex` shared by the application and the
  script so Inno can find the running instance. Writing a separate updater binary would mean a second
  program to build, sign, version and debug in order to do what the installer does already.
  (g) **The version comparison and manifest parse are a pure `util/` module**, testable with no
  window and no network, following the `DwgProbe.cpp` / `EntityId.cpp` precedent. The WinHTTP call
  and the process launch stay in `platform/` and are not unit-tested. Prerelease ordering is the part
  most likely to be quietly wrong, and it is the part that decides whether a user is offered an
  update at all.
  (i) **The manifest carries the `.gs` format version, and the client does the comparison.**
  Added 2026-08-15 alongside REQ-079. CI reads `kGsFormatVersion` from `io/GsMigrate.hpp` into the
  manifest; the running build compares it with its own. Doing it client-side rather than having CI
  diff against the previous release keeps the pipeline stateless and puts the comparison where both
  numbers are actually known. A genuine break — existing drawings will not open — is **declared by
  the author** with a `BREAKING-DRAWINGS:` line in the commit message, because such a break need
  not move the format version at all and therefore cannot be inferred.
  (h) **The REQ-077 check is the project's one sanctioned silent failure.** REQ-201 forbids empty
  error paths; a background update check that reports its own failures would show a network error to
  every user who opens the program on a job site with no signal. The failure is logged and not
  surfaced. The narrowness matters: REQ-078's user-initiated download reports failures normally.
- Alternatives: **(1) The GitHub Releases REST API** — one code path for both channels, but it is
  rate-limited to 60 requests/hour per IP unauthenticated (an office behind one NAT shares that
  budget), returns a large payload for a five-field question, and requires client-side prerelease
  filtering that (d) gets from the platform for free. **(2) libcurl or cpr** — a real HTTP library,
  rejected under REQ-300 because WinHTTP answers all three policy questions on a Windows-only
  product. **(3) A dedicated updater binary** — the conventional design, and the right one if the
  installer could not close the running app; Inno can, so it would be a second artifact earning
  nothing. **(4) Silent background updates** — rejected by the user; this program holds unsaved
  drawings. **(5) Publishing a prerelease per push to a feature branch** — accumulates dozens of
  release rows; the rolling `channel-beta` tag in (d) gives the same dogfooding with one row.
  **(6) Deriving the version bump from conventional-commit prefixes** — `.gitmessage.txt` prescribes
  them but the actual history does not use them (`3D model import: …`, `Task logs for TASK-044..047`),
  so it would misfire; the bump is a recorded human decision instead.
- Consequences: a new `src/update/` module and the project's **first outbound network call** — with
  it, the first failure mode that depends on a machine we do not control, and a new class of
  requirement whose acceptance cannot be checked purely offline. `winhttp` joins the link line. The
  executable rename is a one-time compatibility event for installed 0.4.x copies, handled by
  `[InstallDelete]`. CI build time becomes a standing cost, mitigated by caching `build/_deps` (glfw,
  imgui, glew, Catch2 and pdfium are all `FetchContent`). **The integrity/authenticity gap is
  accepted, not closed:** SHA-256 detects corruption but the hash ships beside the binary, so the
  trust anchor is TLS plus GitHub account security. Authenticode signing is recorded as technical
  debt and the pipeline carries a no-op signing step so it can be filled in without restructuring.
  Deliberately not designed for: delta/patch updates, rollback to a previous version, per-user
  (non-elevated) installation, staged rollouts, and any platform other than Windows x64.

### ADR-030 — `.gs` forward migration on the JSON tree   (2026-08-15, accepted)
- Context: `.gs` has written a `version` field since the beginning, and the reader compared it with
  `!=`. Bumping `kGsFormatVersion` would therefore have made **every existing drawing unopenable**,
  so the field was unusable in practice and the version has never moved off 1. Eleven changes
  across REQ-044…REQ-076 were instead forced through a "tolerant key, additive only, no version
  bump" workaround, each carrying a comment saying so. That worked while every change was purely
  additive, and it has no answer for a change that is not — a renamed field, a changed unit, a
  restructured store. The project was one non-additive change away from either breaking every file
  or abandoning the version field entirely.
- Decision:
  (a) **The reader accepts any version at or below its own** and refuses only *newer* files, naming
  both versions. A newer file is a downgrade problem, not a corruption problem, and guessing at it
  risks silently misreading data.
  (b) **Migration runs on the parsed JSON, before the typed loader.** The alternative — loading into
  the typed model and fixing it up afterwards — means every migration is written against the
  *current* structs, so a migration written today silently changes meaning when those structs change
  tomorrow. A JSON step is frozen against the shape it was written for and stays correct forever.
  (c) **One step per version increment, composed in order.** A v1 file reaching a v4 build runs
  v1→v2→v3→v4. Each step is written and tested against exactly one change, which is the only way the
  cost stays constant as versions accumulate.
  (d) **Steps are pure `json → json` functions** and live in `io/GsMigrate.*`, testable without a
  window, a GL context, or the command layer — the `DwgProbe.cpp` / `EntityId.cpp` precedent.
  (e) **The step table is passed in to the applying function**, which gives the chaining logic two
  present-day concrete uses (the production table and synthetic tables in tests) and so satisfies
  REQ-301. Without that, the composition logic — the part most likely to be wrong — would be
  reachable only through whatever migrations happen to exist.
  (f) **Additive changes still do not bump the version.** The tolerant-key pattern remains correct
  and cheapest for them; this ADR exists for the changes it cannot express.
- Alternatives: **(1) Keep the tolerant-key pattern forever** — zero new code, but it cannot rename,
  restructure or change the meaning of a field, and the version stays decorative. **(2) Migrate the
  typed model after load** — no JSON plumbing, but migrations rot against struct changes as in (b).
  **(3) Convert files in place on open** — one-time cost per file, but it mutates the user's data as
  a side effect of opening it, and a crash mid-write loses the drawing. Migration is in memory; the
  file changes only when the user saves. **(4) Refuse old files and ship a converter tool** — honest
  but hostile, and the conversion logic has to exist either way.
- Consequences: a new pure module and its tests; the reader's version check becomes a range plus a
  migration call. **The version field becomes usable for the first time**, which means future
  non-additive changes have a route that does not break existing drawings. The genuinely
  unmigratable change remains possible, and REQ-078/REQ-079 require it to be declared and shown to
  the user before they accept the update rather than discovered afterwards. `samples/` becomes a
  regression corpus that must keep opening. Deliberately not designed for: backward migration
  (writing an older version), partial or best-effort migration, or repairing corrupt files.

### ADR-031 — Headless verification: a second target, a link-time dialog seam, and invariants beside the state   (2026-08-16, accepted)
> **AMENDED 2026-08-16 — parts (b) and (c) below are superseded, and the Context's measurement was
> partly wrong.** The original Context was derived from `grep`; re-deriving it from `dumpbin
> /SYMBOLS` on the built objects contradicted two of its claims. Corrections, in the honest
> direction: `DxfIo.cpp` needs **8** symbols from `CadCommands.cpp` and `GsIo.cpp` needs **9** — not
> one and none — so the parsers are **not** free-standing and drag in `CadCommands.cpp`. And
> `CadCommands.cpp` being the sole ImGui caller did **not** mean the domain layer was: it is also
> reached from `SurveyPoints.cpp:666`, where sizing a survey label box calls `ImGui::GetFont()` and
> measures text, writing the result into stored annotation geometry — which `GsIo.cpp` invokes on
> every load. **Opening a drawing therefore requires a font.** That is not a movable outlier, and it
> is escalated as a SPEC GAP in `workshop/tasks/TASK-056`; the resolution amends (b), (c), and
> REQ-203's "links with no imgui" acceptance condition. The record below is left standing rather
> than rewritten, because the *method* is the lesson: a link surface is measured with a symbol
> dumper, never with `grep`, and this ADR is the evidence for that rule.

- Context: REQ-203/204 require the Commands layer to run with no window. The measurement that makes
  this cheap was taken before the decision: `CadCommands.hpp` and its **entire header closure**
  (`CadEntities`, `Camera`, `PaperSpace`, `SurveyPoints`, `PdfAttach`, `traverse/*`) include no
  imgui, no GLFW, and no GL — `AppCommandState` is plain data. `CadCommands.cpp` is 13,662 lines and
  contains exactly **one** ImGui call, `LoadApplicationFont` at line 13609, which is a UI concern
  sitting in the wrong layer. ~~`DxfIo.cpp` calls one symbol from `CadCommands.cpp`
  (`ComputeWorldExtents`) and `GsIo.cpp` calls none.~~ *(false — see the amendment above.)* The
  layering §2 already describes is, in
  practice, almost achieved — it has simply never been *proven*, because nothing links the lower
  layers on their own. The forces are therefore not "how do we decouple this" but "how do we hold on
  to a decoupling we already have," and the answer wants to be enforcement, not refactoring.
- Decision: six parts.

  **(a) A second CMake target, `gosurvey_headless`, not a `--headless` flag on `GoSurvey.exe`.**
  The flag would keep imgui/glfw/GL on the link line, so the one property worth proving — that
  Commands names nothing above it — would stay unproven, and the layering would drift again. A
  separate target makes the boundary a build error. The two targets share one
  `GOSURVEY_DOMAIN_SOURCES` CMake list so a source added to the app is not silently absent from the
  driver.

  **(b) The file-dialog seam is resolved at link time, not behind a new interface.**
  `platform/WinFileDialogs.hpp` is eleven free functions returning a UTF-8 path or `false`, and it
  is the *entire* modal surface reachable from Commands. The headless target links a second
  implementation of that same header which answers from the transcript. No virtual, no injection, no
  new abstraction — REQ-301 is satisfied because nothing new is introduced; an existing header
  simply gains a second implementation, which is what a platform header is for.

  **(c) `LoadApplicationFont` moves from `CadCommands.cpp` to the UI layer.** It is the codebase's
  one upward dependency and §2 already forbids it. This is the whole refactoring cost of the ADR.

  **(d) The invariant checks live in a new pure module, `util/docinvariants`,** not inside the
  driver. They have two present-day concrete uses (the Catch2 suite and the headless driver), so
  §11.4 is met on the day they are written rather than aspirationally; a third use — asserting them
  in debug builds of the real application — is available later without moving anything.

  **(e) Two fuzzers of different shapes, because the inputs are different in kind.** Command
  sequences get **structure-aware generation** from `kRegistry`: a byte-mutating fuzzer would spend
  its entire budget rediscovering the command grammar before reaching a second command. File parsers
  (`DxfIo`, `GsIo`, glTF, STL, CSV) get **byte mutation** over a seed corpus, which is exactly the
  input model those parsers actually face. Coverage-guided fuzzing (MSVC `/fsanitize=fuzzer`) is
  where the parser half should end up, and is deliberately left as a later step so the corpus and
  triage discipline exist before the input rate rises.

  **(f) ASAN (`/fsanitize=address`) is enabled on the headless and fuzz targets only,** never on the
  shipped binary. The fuzz targets are TEST-ONLY under REQ-300 in the same sense Catch2 is.
- Alternatives: **(1) Drive the real GUI** with a UI-automation tool or screenshot diffing —
  rejected: it needs an interactive desktop session, it is slow enough that a fuzzer is pointless,
  it is flaky by construction, and most of what it exercises is ImGui rather than GoSurvey.
  **(2) Extract a "core" library** and have both the app and the driver link it — rejected as the
  same thing with more moving parts: the measurement above shows the sources are already separable,
  so a shared CMake source list achieves it without inventing a library boundary that would then
  need defending (REQ-301). **(3) In-process libFuzzer straight onto `ProcessCommandLineSubmit`** —
  rejected for the command path per (e), accepted in spirit for the parser path.
  **(4) Record and replay real user sessions** — rejected for now: it needs a recorder in the
  shipped UI (shipping code that exists only for testing) and it can only reproduce what a user
  already did, which is the case that already has a bug report.
- Consequences: a second target to keep green, and a CMake source list that must be edited instead
  of an `add_executable` — the cost of (a), and the failure mode if it is ignored is a link error,
  not a silent gap. One function moves layers. It becomes possible to attack the `DxfIo`/`GsIo`
  coverage gap that this project has carried unresolved. The layering in §2 stops being a review
  convention and becomes a linked artifact. **The real risk is not technical but editorial:** a
  fuzzer produces findings faster than a person can act on them, so without minimization and
  deduplication the output becomes noise that is rationally ignored, which is worse than no fuzzer
  at all — REQ-204 therefore makes minimization an acceptance condition rather than a nicety.
  **Deliberately out of scope of the spec:** the triage and issue-filing automation built on top of
  this. That is developer tooling operating on the driver's output, it ships to no user, and it
  belongs in `docs/fuzz-harness.md` and `verification/`, not in the product specification.

#### ADR-031 amendment — the font belongs to the domain layer   (2026-08-16, accepted)

Resolves the SPEC GAP raised by TASK-056 §3. **Supersedes (b) and (c) above.**

- **(b′) There are two platform seams, not one, and both are resolved at link time.** The mechanism
  is unchanged — a second implementation of an existing pure header, selected by which target links
  it — but the membership was measured wrong. It is:
  - `platform/WinFileDialogs.hpp` — of its 11 functions, exactly **one** (`BrowseOpenFileGltfUtf8`)
    is reachable from `CadCommands.cpp`; the rest are called from the UI layer, which headless does
    not link. The headless implementation answers from the transcript and returns `false` otherwise.
  - `pdf/PdfAttach.hpp` — **three** symbols (`PdfAttach_Build`, `PdfAttach_ReleaseTexture`,
    `PdfDraftCache_Free`) are reachable from `CadCommands.cpp`, and `PdfAttach.cpp` includes
    `<GL/glew.h>` and calls `glGenTextures`/`glDeleteTextures`. This is how GL reaches the Commands
    layer, and it is the seam that keeps it out of the headless link. The header is already pure
    (`cstdint`/`string`/`vector`), so no change to it is required.

- **(c′) `LoadApplicationFont` stays in the domain layer, and the headless target links ImGui core.**
  The reversal of (c) is not a concession — it follows from a fact (c) did not know: **text metrics
  are an input to stored geometry.** `SurveyPoints.cpp:666` sizes a survey label's box by measuring
  its text through the current ImGui font and writes `boxMinX/boxMaxX/boxMinY/boxMaxY` onto the
  annotation; `GsIo.cpp` calls that on every load. Font availability is therefore a **domain**
  concern, and (c) had it exactly backwards when it moved font loading *up* to the UI.

  The headless target links `imgui.cpp`, `imgui_draw.cpp`, `imgui_widgets.cpp`, `imgui_tables.cpp`
  — **no backends, no GLFW, no GLEW** — creates a context with a dummy `DisplaySize`, and loads the
  same Tahoma at the same size the application loads. Building a font atlas is CPU work; only
  *uploading* it needs a GPU, and headless never uploads. **Validated before the decision** rather
  than assumed (TASK-056 §5): the probe builds a 512×128 atlas, `ImGui::GetFont()` returns a real
  font, text measures identically, and `dumpbin /DEPENDENTS` on the binary lists no `opengl32.dll`.

  This is what makes REQ-203's "a transcript yields exactly what a user performing the same steps
  yields, compared by saving `.gs` and diffing" a meaningful condition instead of an unmeetable one:
  headless and GUI measure text with **the same code and the same font**, so a diff means a defect
  rather than a font mismatch. Alternatives (a measurement seam; removing the font dependency from
  label sizing) were rejected — see TASK-056 §3, but in short: the seam is an abstraction REQ-301
  would refuse and would hide the fidelity problem instead of solving it, and removing the
  dependency is a `.gs` format change made to serve a test harness.

- **Consequence for (a): the enforcement claim weakens, and is restated honestly.** The linker no
  longer proves "the Commands layer touches no UI library", because ImGui is on the headless link
  line. What it still proves — and what REQ-203 is amended to require — is **no GLFW, no GLEW, no
  `gl*`, and no ImGui backend**: no window, no GPU, no display. That is a real boundary, it is
  enough for CI, and it is the boundary that was actually load-bearing for this work.

### ADR-032 — Anonymous telemetry: WinHTTP POST, pure rate-limit logic, and local persistence   (2026-08-16, accepted)
- Context: REQ-080 requires the application to report an anonymous install ID and active-usage
  pings to a user-owned backend without gating the session or adding a network dependency (REQ-300).
  The precedent is ADR-029 (the update checker), which already uses WinHTTP for HTTPS, fires
  off-thread, and has a sanctioned silent-failure mode (REQ-201 exception). This ADR reuses that
  pattern.
- Decision:
  (a) **WinHTTP is extended with a `PostJson` helper**, alongside the existing `GetToString` and
  file-download functions. HTTP POSTs a JSON body and returns success on 2xx, failure otherwise.
  This touches only `platform/HttpFetch.hpp/.cpp`, which is already owned by ADR-029. The project
  carries no new HTTP dependency.

  (b) **The telemetry logic is a pure `util/` module (`TelemetryPing.hpp/.cpp`), not in `platform/`
  or anywhere near the network.** It builds the JSON payload, decides install-vs-active, and
  decides whether 24 hours have elapsed since the last active ping. This is the same `util/`/
  `platform/` split as `UpdateCheck.hpp`/`src/update/UpdateService.cpp` (ADR-029(g)), so it is
  testable without a window or network using Catch2/ctest.

  (c) **Persistence is an extension to the existing `gosurvey-user.json` file,** via `src/io/UserPrefs`.
  Two new fields: `installId` (a random 128-bit hex string, generated once on first load if absent)
  and `lastActivePingDate` (a `YYYY-MM-DD` string, updated after each active ping). No new storage
  file or registry usage — the existing UserPrefs pattern is reused.

  (d) **The worker thread is spawned at startup, alongside `UpdateService` (in `src/app/`), but
  independent of it.** The update check gates the session on purpose (unsaved-work safety); the
  telemetry ping fires asynchronously and must never add latency to that gate. Both fire in their
  own worker threads using the architecture's one-shot-worker pattern (§8).

  (e) **Any network error is dropped silently**, mirroring REQ-077/ADR-029(h). Timeout, DNS
  failure, TLS error, unreachable host — all are caught, logged at trace level (REQ-201: a failure
  is recorded, not silent), and swallowed. The application proceeds. This is the one place
  REQ-201's "no silent failures" rule is deliberately waived, for the same reason as the update
  check: a background call initiated by the system has no user-initiated failure recourse.

  (f) **No new abstraction.** A telemetry service is instantiated once at startup (the same pattern
  as `UpdateService`), configured with the endpoint URL and passed the current `AppVersion` and
  `updateChannel` from settings. The TelemetryEndpoint is a compile-time constant string (in
  `util/TelemetryPing.hpp`), changeable only by recompile, so it survives a settings reset and
  cannot be redirected by user preference.
- Alternatives: **(1) Extend the update-check modal to report telemetry** — couples two unrelated
  concerns, and the update check gates the session while telemetry cannot be gated. **(2) Post to
  GitHub Releases download-count statistics** — GitHub already tracks these freely; if desired,
  pull them separately. This requirement measures first *run*, which GitHub does not. **(3)
  Third-party analytics vendor (Plausible, PostHog)** — rejected per user decision; self-hosted
  backend only, so data stays within the user's infrastructure. **(4) Opt-in toggle** — rejected
  per user decision; PII-free pings are sent always, no opt-out. A consent preference is flagged as
  a follow-up (not in this ADR) if legal/regulatory requirements change.
- Consequences: new `src/telemetry/` module (TelemetryPing, pure logic); extension to
  `platform/HttpFetch` with a `PostJson` helper; extension to `src/io/UserPrefs` with two new
  JSON fields; new worker thread at startup (§8, one-shot pattern). The endpoint URL is a
  compile-time constant and is the only knob that moves without a rebuild. No new third-party
  dependency, no new abstraction, no wire-format change. The user is responsible for standing up
  the receiving endpoint (e.g. a Cloudflare Worker + KV/D1); this ADR specifies only the client
  side.

### ADR-035 — Feature lines: a dedicated store, elevation points as flagged vertices   (2026-08-19, accepted)
- Context: REQ-087 and REQ-088 add feature lines — named 3D linework with editable per-vertex
  elevations, elevation points, and "add to surface as a breakline". ADR-028 alternative (5) deferred
  them as "a separate milestone once surfaces are trustworthy"; decision D-2026-08-19-a opened that
  milestone. Two structural questions have to be answered before any code: **where the geometry
  lives**, and **what an elevation point actually is**. The second was recorded as the one genuinely
  unsolved problem when M-Grading was planned.
- **The measurement that frames this.** "A new entity store grows a case in selection, extents, layer
  state, the undo snapshot, `.gs`, DXF export, render, snap, pick, grips and properties" (ADR-028) was
  quoted to the user as roughly a dozen touch points. Counted rather than remembered, it is not:

  | entity kind | sites | files |
  |---|---|---|
  | `cadMeshes` / `EntityKind::Mesh` — **display-only** | 32 | 5 |
  | `userPolyline*` / `EntityKind::Polyline` — **editable** | **612** | **11** |

  A feature line is editable — it must move, copy, rotate, scale, trim, offset, grip-edit and plot —
  so Polyline is the honest comparison, not Mesh. The estimate the storage decision was taken on was
  low by roughly fifty times, which is why that decision is re-opened below rather than built on.
- Decision:
  (a) **An elevation point is a flagged vertex in the ordinary vertex chain**, not a separate
  structure. This is the question that looked hard and is not: an elevation point lies **on** the
  line by construction, so including it in the plan vertex array is geometrically a no-op — the
  polyline through the vertices has the identical plan shape either way. Rendering, extents, snapping,
  picking and plotting therefore need to know nothing about it. Only PI-level operations (insert PI,
  delete PI, grips) consult the flag. Storage stays stride-3 XYZ, so architecture §11.8's `% 3`
  invariant is untouched and `docinvariants` needs no new rule.
  (b) **The obligation this creates, stated so it is not discovered later:** moving a PI must
  re-project the elevation points on its adjacent segments back onto the line. Skip that and the
  feature line grows a visible kink. That failure is **loud** — it is wrong on screen in plan view —
  which is the whole reason (a) is preferred to storing elevation points as (station, elevation)
  pairs materialised on demand: that alternative cannot drift off the line, but a consumer that
  forgets to materialise them loses elevation detail *silently*, and this project's own note on 3D
  entity work records that silent-in-plan-view is the failure mode that costs weeks.
  (c) **A feature line's identity is its stable entity id** (REQ-076 / ADR-027). Its name, description
  and per-vertex elevation-point flags are **parallel arrays in its own store** (see (g)), indexed the
  same way `featureLineAttrs` is — not fields on `EntityAttributes`, which is clipboard-copied and
  DXF-exported and would carry them somewhere they do not belong. Had the geometry shared the polyline
  store, this would instead have been an id-keyed side table (the ADR-020 / ADR-034 pattern); (g)
  makes that unnecessary.
  (d) **Surface integration reuses REQ-069 unchanged.** "Add to surface as breakline" designates the
  feature line by its stable id exactly as `DESIGNATEBREAKLINE` designates a polyline; the
  triangulator already folds a breakline's per-vertex elevations in. No new surface machinery.
  (e) **The elevation editor (REQ-088) is a view, not a store.** Station, length, grade back and
  grade ahead are all derived from the vertex chain on demand. Storing a grade would create a second
  source of truth for the same elevation, and the two would disagree the moment geometry moved.
  (f) **Not in this ADR and not designed for:** feature lines from an alignment, from a corridor, or
  from a stepped offset; grading objects; feature line styles beyond a name; weeding and supplementing
  factors. `Create Feature Line` and `Create Feature Lines from Objects` are the two creation paths.
- (g) **The plan geometry lives in its own store** — `featureLineVerts` / `featureLineOffsets` /
  `featureLineClosed` / `featureLineAttrs`, behind a ninth `EntityKind::FeatureLine`. **Decided by the
  user 2026-08-19 with the measurement below in front of them**, after this ADR recommended the
  cheaper alternative and was overruled on it. A feature line is then never mistaken for a polyline at
  the type level, which is the property being bought. The two options as weighed:

  **(1) Its own store** (`featureLineVerts` / `featureLineOffsets` / …). What the user chose on
  2026-08-19, on the ~12-site estimate. Conceptually cleanest — a feature line is never mistaken for
  a polyline. Costs a Polyline-shaped footprint: on the order of 600 sites across 11 files, because
  every modify command, the snap engine, the transform preview, the DXF and PDF writers and the
  invariant checks each need a new case. Each missed one is a feature line that silently cannot be
  moved, snapped to, plotted or exported.

  **(2) The existing polyline store, plus the (c) side table.** A feature line **is** a polyline in
  the geometry arrays, and the side table is what makes it a feature line. Every existing command —
  move, copy, rotate, trim, offset, grips, plot, DXF — works on it the day it ships, with no new case
  anywhere. `EntityKind` stays at eight; "is this a feature line?" is a lookup, not a tag. The cost is
  that the distinction is a convention rather than a type, so a command that *should* refuse a feature
  line (or treat it differently) has to ask.

  This ADR recommended (2) and the user chose (1), informed by the measurement rather than the
  estimate it replaced. **The consequence is accepted deliberately, so it must be managed rather than
  rediscovered:** the risk in (1) is not the volume of work, it is that a missed case is SILENT — a
  feature line that cannot be trimmed, or is skipped by the PDF writer, looks like nothing at all
  until someone needs it. The task plan therefore works from an enumerated checklist derived from the
  `userPolyline` sites, and treats "every modify command names FeatureLine or deliberately refuses it"
  as an acceptance condition rather than a review habit.
- Alternatives: **(A) Elevation points as (station, elevation) pairs** — cannot drift off the line,
  but every consumer must materialise them and a consumer that forgets loses elevation silently; see
  (b). **(B) Feature lines as 3D polylines with no new concept at all** — REQ-085 already ships that;
  it gives per-vertex elevation but no name, no elevation points, no elevation editor, and no way for
  a command to know a line is a design object. Declined as not meeting REQ-087. **(C) Storing grades
  alongside elevations** — makes the editor trivial and creates two sources of truth; see (e).
- Consequences: a ninth entity kind with its own geometry store, attribute array and per-vertex flag
  array; its `.gs` arrays (additive — a legacy drawing has none of them); new commands for creation,
  conversion, PI editing and elevation editing; a panel for REQ-088. Every existing modify command,
  the snap engine, the transform preview, the invariant checks and the DXF and PDF writers gain a
  FeatureLine case or an explicit refusal, and **that enumeration is an acceptance condition, not a
  review habit** — see (g). REQ-088's elevation editor is consequently sequenced AFTER the store is
  complete and exercised, rather than beside it.

### ADR-036 — Surface styles: a document-owned table, a display-geometry cache, and a Mesh-tier selectable surface   (2026-08-21, accepted)
- Context: REQ-070 (surface styles) and REQ-072 (elevation/slope banding + slope arrows) are the
  roadmap's current step, and the user asked for both together with "a surface should be a selectable
  entity". Reading the code first changed what this ADR has to decide:
  - **REQ-068 already requires selection** ("surfaces participate in layers, visibility, selection,
    erase, undo and view extents") and it was never implemented. `SelectedEntity::Type`
    (`CadCommands.hpp:30`) has no `Surface`, and `PickClosestCadEntity` never enumerates surfaces.
    That half of the request is an unimplemented acceptance condition, not new authority.
  - **A surface already carries an `EntityAttributes`** (`cadSurfaceAttrs`, persisted in `.gs` as
    `surfaceAttrs`), so layer and colour are in place — but `EntityKind` has no `Surface` member, so
    `EnsureEntityIds` never sweeps that array and **every surface's stable id is 0**. REQ-084's
    isolation gate and REQ-076's reference model are both keyed on that id.
  - **ADR-028 already pre-decided most of the style design**: (b) contours/bands/arrows/border are
    display geometry generated from the style and never entities; (c) the generator is a pure GL-free
    `util/` module; (h) the style table is the ADR-020 document-owned-table pattern and shading reuses
    the REQ-064 triangle path. This ADR records the concrete shapes those decisions imply, and the
    three genuinely open questions ADR-028 left: where the generated geometry is cached, how it
    reaches the renderer, and what a selected surface can be *done to*.
- **The measurement that frames this** (ADR-035's lesson: count, do not estimate). A new
  `SelectedEntity::Type` costs whatever the tier it joins costs:

  | tier | example | selection-related sites |
  |---|---|---|
  | **editable** — moves, rotates, trims, grips | `Polyline`, `Ellipse` | 40 (Ellipse); ~612 total store footprint (Polyline) |
  | **display-only** — selects, highlights, erases | `Mesh` | 54 across 5 files, of which the *selection-specific* funnels are ~12 |

  A surface is display-only by construction: its geometry is **derived from its definition**, so
  dragging it would be a lie — the next rebuild would undo the drag. It therefore joins at the Mesh
  tier, and the ~12 funnels are enumerated in (c) as an acceptance condition rather than left to be
  found one silent gap at a time.
- Decision:
  (a) **`EntityKind::Surface` is appended to the sweep, and a surface's identity is its stable entity
  id** (REQ-076 / ADR-027). `kEntityKindsInSweepOrder` gains `Surface` **at the end**, which is what
  keeps legacy id assignment for the existing nine kinds bit-identical — a legacy `.gs` loads with the
  same ids it loaded with yesterday, and surfaces pick up ids after them. `AttrsForKind` gains the one
  case. `SurfaceRebuildAsync::surfaceName` — commented "surfaces have no entity id" — becomes the id,
  which also closes a latent defect: a rename while a rebuild is in flight orphans the result, and two
  renames could land a result on the wrong surface.
  (b) **`SelectedEntity::Type::Surface` is appended as the tenth kind**, and a click anywhere on a
  visible component — a triangle edge, a contour, the border — selects the **whole surface object**,
  as Civil 3D does. Component-level selection is refused: REQ-070 states contours "never appear in
  selection", and a contour has no persistent identity to select *by*.
  (c) **A surface is display-only, and every transform command refuses it explicitly.** The funnels it
  joins, enumerated so a miss is a failed test rather than a discovery: `PickClosestCadEntity`,
  `ComputeSelectionFromRect`, `ExecuteDeleteSelection`, `CadSelectedEntityIdOf`,
  `CadSelectedEntityHidden`, the hover/selection highlight buffers, the Properties panel,
  `SelectSimilarToCurrentSelection`, the selection-cycling window, `CopySelectionToClipboard`, the
  grip path, and the MOVE/COPY/ROTATE/SCALE/MIRROR/STRETCH/ALIGN snapshot. Those last **refuse with a
  stated reason** (REQ-201) rather than silently dropping the surface from the operation — "silently
  does nothing" is the exact failure mode ADR-035 (g) was written about.
  (d) **`SurfaceStyle` is a document-owned table, the ADR-020 pattern**, referenced by
  `CadSurface::styleName`. Editing a style changes every surface using it, which is REQ-070's stated
  point. A name that no longer resolves falls back to a built-in default rather than failing to draw
  (REQ-070 acceptance), so a deleted style is never a load error. "Standard" always exists and is not
  deletable, exactly as `TextStyle`'s does. **Resolution is on read, not bake-on-write** — the
  opposite of ADR-020's choice for text, and deliberately: a text style is baked because ~12 render
  sites read the baked value, whereas a surface style is read at exactly one place, the
  display-geometry generator, and baking it would defeat REQ-070's "changing a style property must not
  re-triangulate."
  (e) **Generated display geometry is a cache on `AppCommandState`, keyed by the surface's stable id,
  with a staleness key of `(tin pointer, style revision)`** — and **never** in
  `DrawingGeometrySnapshot`, `DrawingDocument` or `.gs`. This is what makes REQ-070's two hard
  constraints structurally true rather than conventionally true: contours cannot enter the undo stack
  or the file because there is nowhere for them to go, and changing an interval cannot retriangulate
  because the staleness key does not include the definition. The `shared_ptr<const CadTin>` is the
  natural first half of that staleness key — a rebuild replaces the pointer wholesale (ADR-028 (a)),
  so pointer identity **is** triangulation identity.

  **Two placement rules, both load-bearing, both found by reviewing this plan rather than the code
  after it:**
  - The cache is a **separate parallel container, never a member of `CadSurface`.** `cadSurfaces` is
    assigned wholesale into the document and into every geometry snapshot
    (`CadCommands.cpp:88, 148, 1126, 1190`), so a cache field on the surface would be deep-copied into
    all 50 undo frames — precisely the cost this decision exists to avoid, arriving through the one
    door nobody would think to check.
  - It is keyed by **stable entity id, not by array index** (§11 invariant 9), which is why ADR-036
    (a) must land before any of this. `cadSurfaces` compacts on erase, so an index-keyed cache would
    silently start drawing one surface's contours over another's triangulation after a delete.
  (f) **Contour generation is marching-triangles in a new pure `util/contourgen`**, beside `tinbuild`
  and for the same reasons (ADR-028 (c)): unit-testable with no GL context, no new dependency. Linear
  contours only — ADR-028 left smoothing explicitly undesigned, so the Civil 3D "Contour smoothing"
  slider is **not** built. A contour crossing a triangle is one segment between two edge
  interpolations; chaining those segments into polylines uses the shared-edge adjacency rather than
  leaving a segment soup. **Elevations exactly on a vertex** are the known degenerate case and are
  resolved by a single documented rule — a vertex Z equal to a contour level is treated as
  infinitesimally above it — not by an independent float comparison at each of the three edges, which
  is how a contour ends up half-open. This is the same "define the boundary case and test it"
  obligation REQ-072 states for band breakpoints.
  (g) **Banding and slope arrows reuse the existing unlit line program with per-band batching**, not a
  new shader and not the REQ-064 shaded program. ADR-028 (h) said "shading reuses the REQ-064 triangle
  shader"; that is **amended here** on a concrete ground found in the code: `shadedProgram_` applies
  `abs(dot(N,V))` two-sided lighting, so a triangle would not display the colour its band prescribes —
  and REQ-072's acceptance is that a triangle "takes the colour their band prescribes", read against a
  legend showing that same colour. Lighting would make the legend a lie. A band table is at most a few
  dozen colours, so the geometry is grouped into one CPU batch per band and drawn with one
  `glUniform4f` + `glDrawArrays(GL_TRIANGLES)` each — no new shader, no new uniform, no per-vertex
  colour attribute.
  (h) **`RenderScene`'s `surfaceEdges` parameter is REPLACED, not joined, by a
  `CadSurfaceDisplayGeometry*`** holding the coloured line batches, the coloured triangle batches and
  the selection/hover state. The signature is already 24 parameters; adding four more for components
  that are always built together and always consumed together would be the wrong shape as well as the
  wrong length. Net parameter count is unchanged.
  (i) **Which style tabs are built, and which are refused.** The user chose "spec scope + Analysis" on
  2026-08-21. Built: **Information**, **Borders**, **Contours**, **Points**, **Triangles**,
  **Analysis** (REQ-072), **Display** (per-component visible / colour / linetype / lineweight), and
  **Summary**. **Not built, and each for a stated reason rather than an oversight:** **Grid** and
  **Watersheds** have no requirement behind them — Watersheds in particular is a drainage-basin
  *algorithm*, not a display option, and would be a SPEC GAP; contour **smoothing** is undesigned by
  ADR-028; the Display tab's per-component **Layer** and **Plot Style** columns are omitted because a
  surface has exactly one layer (its own `EntityAttributes`) and GoSurvey has no plot-style table — a
  column that cannot be honoured is worse than an absent one.
- Alternatives: **(1) Per-surface style copies instead of a table** — simpler, and loses REQ-070's
  stated behaviour that editing a style changes every surface using it; declined by the requirement
  itself. **(2) Bake-on-write style resolution**, mirroring ADR-020 — rejected in (d): the bake would
  have to be invalidated on every style edit, which is precisely the work resolve-on-read avoids, and
  the site count that justified baking for text (~12) is 1 here. **(3) Contours as cached entities on
  a hidden layer** — makes them selectable and exportable for free, and puts hundreds of thousands of
  polylines into every undo frame; this is ADR-028 alternative (1), already declined by the user, and
  REQ-071's EXTRACT remains the escape hatch. **(4) Component-level selection** (click a contour, get
  that contour) — Civil 3D does not do this either, and REQ-070 forbids it. **(5) Making a surface
  transformable** — offered to the user on 2026-08-21 and declined: the triangulation is
  `shared_ptr<const>` and shared with every undo frame (ADR-028 (a)), so a transform would either
  deep-copy it per edit or write through a const pointer, and either way the next rebuild discards the
  result.
- Consequences: a tenth `EntityKind` and a tenth `SelectedEntity::Type`, both **appended** (TABLE later
  appends an eleventh under D-2026-08-28-i / REQ-148; still append-only); a new
  document-owned `surfaceStyles` table with an additive `.gs` section and a legacy-load default; a new
  pure `util/contourgen` module and a `util/surfaceanalysis` for band assignment and downhill vectors;
  a live-only display-geometry cache that is deliberately absent from the snapshot, the document and
  the file; a replaced `RenderScene` surface parameter; a Surface Style editor dialog and a band
  legend overlay; and **a third REQ-100 profile obligation** — ADR-028 already predicted this
  ("contour regeneration is a per-frame cost that neither the segment nor the mesh profile measures"),
  and (e)'s cache key is what keeps it off the per-frame path, so the bench case must prove **the
  cache holds**, not merely that one regeneration is fast. Deliberately left undesigned: contour
  smoothing, contour labelling, watershed analysis, grid display, surface legends in paper space, and
  REQ-071's EXTRACT (deferred to its own task by the user, 2026-08-21).

### ADR-037 — Accounts: Auth0 identity, native-app PKCE + loopback redirect, Credential Manager storage, a separate authenticated Worker   (2026-08-23, accepted)
- Context: REQ-091/REQ-092 require signing in via Google, Microsoft, or email/username/password, for
  eventual license-tier enforcement. REQ-080's anonymous telemetry deliberately has no identity to
  reuse, and nothing in the codebase does password hashing, OAuth, or credential storage today — this
  is new authority, not an extension of an existing subsystem.
- Decision:
  (a) **Auth0 is the identity provider, answered against the REQ-300 three-question policy.** (1) Can
  it be done simply in-tree? No — password hashing, OAuth client-secret handling, email verification
  and password-reset delivery are all security-critical and each is a way to leak credentials if
  gotten subtly wrong; rolling them in-house is not "simple." (2) Is it maintained and worth the cost?
  Yes — Auth0 is an established, actively-maintained identity platform, and its free tier (25,000 MAU,
  unlimited social + database connections, verified live at auth0.com/pricing 2026-08-23) costs
  nothing at GoSurvey's scale. (3) Does it solve a problem we have today? Yes — REQ-091/092 exist
  because license enforcement needs real identity now. All three clear; the dependency is accepted.
  One Auth0 tenant is configured with three connections: **Google** (social), **Microsoft** (social —
  covers personal Outlook/Live accounts), and a **Database connection** (email + username + password,
  Auth0-hosted hashing/verification/reset). The application never builds its own provider buttons or
  password form — it sends the user to Auth0's hosted Universal Login, which shows all three
  configured options already. Landing this dependency (vendoring/config, once implemented) gets its
  own decision-log entry, the same split ImOGuizmo's did (2026-08-11 entries).
  (b) **Native-app auth is system browser + loopback redirect + PKCE (RFC 8252), never an embedded
  webview.** Google and Microsoft do not permit collecting credentials in an app's own embedded UI for
  their OAuth flows, so this is not a style choice. The app opens the system browser to Auth0's
  `/authorize` endpoint with a `redirect_uri` of `http://127.0.0.1:<port>/callback`, listens on that
  port for the single redirect carrying the authorization code, and exchanges it for tokens
  server-side (POST to `/oauth/token`, reusing the existing `HttpPostJson` shape in
  `src/platform/HttpFetch.cpp`). This is a new, small platform surface
  (`src/platform/OAuthListener.hpp/.cpp` — accept one connection, parse the query string, respond,
  close) built on raw Winsock, the same tier as the existing WinHTTP usage; it adds no dependency.
  **Amended 2026-08-23 (discovered configuring the real tenant, TASK-090/091):** RFC 8252
  recommends an OS-assigned ephemeral port, and that is what was originally built and specified
  here. It does not work with Auth0 — the Allowed Callback URLs field's own documented placeholder
  support is "subdomain or domain name only," never the port, so a wildcard port cannot be
  pre-registered there at all; Auth0 rejected it outright (`"callbacks" must be a valid uri`) when
  the user tried to configure it. `OAuthListener::Start` now binds a **caller-supplied fixed
  port**, and `AuthService::BeginInteractiveSignIn` tries a short list of candidates
  (`kOAuthCallbackPorts` in `AuthConfig.hpp`, currently `{53682, 53683, 53684}`) in order, using
  the first one it can bind — a single port already in use by something else on the user's
  machine does not block sign-in outright. Every candidate must be registered as its own exact
  entry in Auth0's Allowed Callback URLs (`docs/auth0-setup.md`); the two lists are kept in sync
  by hand, which is the residual cost of this amendment.
  (c) **The refresh token is stored via Windows Credential Manager**
  (`src/platform/CredentialStore.hpp/.cpp`, new — `CredWriteW`/`CredReadW`/`CredDeleteW`), never in
  `gosurvey-user.json`. This is a deliberate departure from the plaintext-JSON pattern REQ-080's
  `installId`/`lastActivePingDate` use: those are anonymous and low-value if read; a refresh token is
  a live credential and gets the OS-native protected store instead. Access and ID tokens are kept in
  memory only and are never written to disk. Later launches renew silently from the stored refresh
  token; an expired or revoked one falls back to interactive sign-in (b).
  (d) **The auth logic is a pure `src/auth/` module (`AuthPing.hpp/.cpp`) plus an orchestration
  service (`AuthService.hpp/.cpp`), mirroring the `TelemetryPing`/`TelemetryService` split** (ADR-032
  (b)): PKCE pair generation, the `/authorize` URL, and the silent-refresh-vs-interactive decision are
  pure and unit-tested (Catch2, no network/window, same style as `TelemetryPingTests`); the listener,
  browser launch, and token exchange are orchestration on a one-shot worker thread (architecture §8),
  independent of `UpdateService` and `TelemetryService`. Sign-in state is fields on
  `AppCommandState`, not a new global (§11.3). PKCE's SHA-256 needs a byte-buffer overload beside the
  existing file-hashing `ComputeFileSha256` in `HttpFetch.cpp` (same BCrypt call, different input),
  plus a small base64url-encode helper — neither exists today.
  (e) **A new, separate Cloudflare Worker (`tools/accounts-worker/`) serves REQ-092's license lookup**,
  not the existing `telemetry-worker`. The two have opposite trust models: the ping endpoint is
  public and unauthenticated by design (regex-whitelisted anonymous fields); this one requires a
  verified Auth0 JWT (checked against Auth0's JWKS — signature, `exp`, `aud`/`iss`) on every request.
  Keeping them as separate Workers, with **separate D1 databases** (`gosurvey-accounts` alongside the
  existing `gosurvey-telemetry`), means a defect in one cannot read or corrupt the other's data —
  "one visible owner per resource" (§11.5) applied to backend state, not just in-process types. The
  `users` table is minimal: `auth0_sub` (PK), `email`, `tier` (defaulted, e.g. `'free'`), `created_at`.
  `GET /v1/license` returns the caller's tier; nothing yet sets it to anything but the default —
  billing, an admin path, or a manual grant are explicitly future work (REQ-092).
  (f) **No feature is gated by tier yet.** REQ-091/092 deliver the mechanism only — which commands or
  capabilities require which tier is undecided product scope, and naming them here would be the spec
  inventing business authority it doesn't have.
- Alternatives: **(1) Hand-rolled auth backend on the existing telemetry Worker** — no new third-party
  dependency, but the team would own password hashing, OAuth client-secret handling, email delivery,
  and reset/verification flows, all security-critical; declined per (a)'s three-question answer.
  **(2) Embedded webview for login** — rejected outright: Google and Microsoft both block or disallow
  this for OAuth on native apps, so it is not a viable alternative, only a non-compliant one. **(3)
  Store the refresh token in `gosurvey-user.json`** like the telemetry ids — rejected because a
  refresh token is a live, reusable credential, unlike an anonymous install id; the plaintext-JSON
  pattern is right for the latter and wrong for the former. **(4) One shared Worker/D1 for telemetry
  and accounts** — saves a small amount of provisioning, costs the trust-model separation (d)/(e)
  depend on; declined.
- Consequences: three new small modules (`src/auth/`, `src/platform/OAuthListener.*`,
  `src/platform/CredentialStore.*`), one new backend service and database, one new third-party
  dependency (Auth0, recorded per REQ-300), a new sign-in entry point beside the existing "Anonymous
  Usage Data" box in `src/ui/CadUiSettings.cpp` (~line 387), and two Catch2 unit-test additions
  (`AuthPingTests`) plus a Worker-side test file (`accounts-worker/test.mjs`) mirroring
  `telemetry-worker/test.mjs`. Deliberately left undesigned: billing/Stripe, trial periods, which
  features are gated, team/org accounts, and any change to REQ-080's ping (unaffected).

### ADR-038 — Ribbon responsive layout: measure-then-decide breakpoints + a shared overflow popup   (2026-08-25, accepted)
- Context: REQ-302 increment 1 (TASK-104) re-homed the ribbon's existing sections under 7 tabs but
  left the underlying layout untouched — each tab's `DrawRibbonBar` body (`CadUi.cpp:2377`) still
  computes one fixed pixel width per section from hardcoded button/column metrics (`largeW`, `rowH`,
  `gridCell`, `colW(...)`) and lays sections left-to-right inside `RibbonToolsLeft`, sized to
  `min(wideW, available)` since D-2026-08-25-d. That closes clipping into a scrollbar but not into
  anything else — a tab whose content is wider than the actual window still clips, and there is no
  mechanism anywhere in the codebase for a control to change appearance based on available width.
  REQ-302 itself flags this increment as likely needing its own ADR, since one responsive-layout
  mechanism reused by all 7 tabs is new, reusable UI abstraction, unlike increment 1's tab strip
  (which reused the existing Model/Layout toggle style and added no new abstraction).
- Decision: **Measure-then-decide breakpoints, computed at render time, with a shared overflow
  popup for whatever doesn't fit** — no persisted state, no new dependency, no rewrite of section
  bodies.
  (a) `enum class RibbonBreakpoint { Wide, Medium, Narrow }`. Per tab, two candidate total widths are
  computed the same way `ribbonToolsW` already is today: `wideW` (today's existing formula,
  untouched) and a new `mediumW` computed with compact metrics — smaller button width, small buttons'
  `RibbonLabel::Right` becomes `RibbonLabel::None` (icon only, matching the grid buttons' existing
  label mode), and tighter `colW`/inter-section gap constants. Available width is
  `ImGui::GetContentRegionAvail().x` at the point `RibbonToolsLeft` would otherwise be sized (window
  width minus the existing 500px `kLayerPanelW` strip and paddings, same value already computed
  today). `available >= wideW` → Wide; `mediumW <= available < wideW` → Medium; `available < mediumW`
  → Narrow.
  (b) In Narrow, sections render left-to-right at Medium metrics, accumulating width, until the next
  section would not fit; every remaining section for that tab is not rendered inline — instead a
  single trailing "More ▼" button opens an `ImGui::BeginPopup` that renders those same overflowed
  sections' bodies unchanged (Wide metrics, full labels) inside the popup. This reuses each section's
  existing lambda-bodied render code verbatim, invoked once inline (fits) or once inside a popup
  (doesn't fit) — no section's internal logic is duplicated or branches on breakpoint.
  (c) `RibbonToolsLeft`'s width becomes `min(fittedW, available)` where `fittedW` is whatever the
  chosen breakpoint actually consumed (Wide/Medium: the full tab; Narrow: the fitted prefix plus the
  "More" button) — always `<= available`, so no clip can occur at any width; `NoScrollbar`/
  `NoScrollWithMouse` (already in place since D-2026-08-25-d) stop being a fallback and become simply
  correct, since content can no longer exceed the child window's bounds.
  (d) Narrow's "prioritize frequently used commands" (issue #83) falls out of the existing
  left-to-right section order for free — Edit/Draw/Modify before Layout on Home, Text before
  Dimensions on Annotate, etc. — no separate priority ranking is introduced.
  (e) No change to increment 1's tab strip, persistence, or command wiring; no change to any command's
  availability or behavior — this increment only changes how a tab's own sections are laid out and,
  in Narrow, how the overflow subset is reached (popup instead of always-inline).
- Alternatives: **(1) A generic reusable `ResponsiveContainer` widget** usable by any future toolbar
  or panel, not just the ribbon — rejected per CLAUDE.md's "no abstraction without 2+ present-day
  concrete uses": nothing else in the codebase needs this today, and the ribbon-specific version in
  (a)-(c) is smaller and doesn't speculate about future callers. **(2) Compact metrics only, no
  popup** — a Narrow tab would simply stop rendering sections that don't fit, with no way to reach
  them; rejected outright since it violates "preserve access to all commands" (issue #83 Narrow
  Layout requirement) and REQ-302's own explicit list of acceptable strategies (overflow menu,
  compact group). **(3) True reflow (wrap sections onto a second row within the ribbon band)** —
  the closest to issue #83's item 7 ("reflowing buttons into additional rows"), but the ribbon's
  total height is currently a caller-supplied constant (`139.f + kRibbonTabStripH + ...`,
  `main.cpp:405`) that increment 1 explicitly kept unchanged from the pre-increment-1 baseline
  (REQ-302 Acceptance — Increment 1, "existing section button sizing unchanged"); growing it
  per-tab, per-breakpoint is a larger, independently-scoped change and is left to a future increment
  if the popup approach proves insufficient in practice — recorded here as deliberately deferred, not
  silently dropped.
- Consequences: `DrawRibbonBar` (`CadUi.cpp`) gains the breakpoint enum, the `mediumW` computation
  parallel to each tab's existing `wideW` computation, the fit-until-overflow loop, and one popup
  block per tab; no new file, no new dependency, no new persisted field. Every section's own
  lambda-bodied content is unchanged. Manual GUI pass required at multiple window widths (this
  project's own no-UI-automation constraint, ADR-031) to confirm Medium/Narrow read correctly on
  screen, the same pattern increment 1's own GUI pass followed.

### ADR-039 — Surfaces epic #119: stay a definition-driven TIN, query cache not on CadTin, drainage as util modules   (2026-08-27, accepted)
- Context: GitHub issue #119 asks for a complete Civil 3D-shaped terrain system: empty surfaces,
  stats, elevation queries, OSNAP, data clip, contour inputs, aspect banding, bounded volumes,
  watersheds, water-drop, catchments, volume surfaces, grid/corridor types, and a shared surface
  interface. REQ-068…075 + ADR-028/036 already shipped the TIN core. D-2026-08-21-a refused
  watersheds and grid *as style tabs*. ADR-028 (h) forbids a surface interface and an analysis-plugin
  seam. The issue cannot be implemented as written without reversing those decisions; D-2026-08-27-a
  records which parts reverse, which stay refused, and how the accepted parts attach.
- Decision:
  (a) **`CadSurface` is still the document row.** Kinds are a discriminator (`SurfaceKind`). Queries
      go through `ISurfaceQuery` (REQ-137) with TIN-interpolation and grid-bilinear implementations
      (REQ-301: two present-day uses). Corridor and volume kinds reuse the TIN query path once built.
      D-2026-08-28-a reverses the "no ISurface" clause of (a) as originally written.
  (b) **A named surface is a definition that may have `tin == nullptr`.** Fewer than three
      non-collinear points still produces **no triangulation** (REQ-069's "no partial TIN" is
      unchanged). The *object* is created anyway (REQ-124), so the user can add sources afterwards.
      `SurfacesCovering` / hover / SURFELEV / OSNAP skip a null TIN; they do not crash.
  (c) **The spatial index is a live-only cache on `AppCommandState`, keyed by stable surface id and
      `weak_ptr<const CadTin>`**, the ADR-036 (e) display-cache pattern. It is not a member of
      `CadTin` (that would pull `tinbuild.hpp` into `CadEntities.hpp`, breaking §11.4) and is not
      written to `.gs`. `TinElevationAt` remains the linear-scan source of truth for tests;
      `TinElevationAtIndexed` is the hot path. Indexed and scan answers must agree.
  (d) **Boundaries gain `Clip`.** A clip ring excludes **input points** outside it *before*
      triangulation (union of clips if several exist). Outer/hide/show still cull triangles after
      the build. Clip rings are also constrained edges, so the rim is exact. Legacy `.gs` without
      `"clip"` is unchanged.
  (e) **Contour *sources* are definition items, not display geometry.** A 3D polyline (or feature
      line) designated as a contour contributes its vertices and constrained edges at their stored
      Z — the same path breaklines already use. Display contours remain style-generated (REQ-070).
      EXTRACT remains the bake of *display* contours (REQ-071).
  (f) **Direction/aspect banding is a third `SurfaceAnalysisMode`.** Aspect is the downhill azimuth
      in degrees, 0 = +Y (northing), increasing toward +X (easting), range [0, 360). The existing
      one-table-per-style rule stands: a triangle has one colour. Slope arrows stay an independent
      toggle.
  (g) **Statistics are derived, never stored.** A pure `util/surfacestats` module reads a TIN and
      reports counts, extents, 2D/3D area, min/max/mean slope. Volume statistics belong to REQ-073 /
      REQ-131, not to this module.
  (h) **Surface OSNAP interpolates the triangle plane** at the cursor's plan position when that
      position is on a visible surface, returning XYZ. Miss = no snap. It reuses the query cache
      (c). It is a new `CadSnap::Kind`, not a hijack of Endpoint.
  (i) **Bounded volume is a clip on the existing sampler.** A closed polyline (and later a named
      boundary) limits which sample cells contribute. **D-2026-08-27-b** separately accepts a
      **TIN volume surface** as an ordinary `CadSurface` whose definition is two parent surface
      names (REQ-136). That is not `ISurface`, not a grid/corridor type, and not a second volume
      algorithm: Z is comparison minus base at samples covered by both TINs, then unconstrained
      Delaunay. REQ-073 `VOLUMES` / the dashboard stay the numeric report.
  (j) **Watershed / water-drop / catchment live in `util/watershed`**: GL-free, Catch2-tested,
      synthetic basins as fixtures. Display of watershed polygons is style-generated cache geometry
      (ADR-028 (b)), never entities, never stored in `.gs`. Water-drop bake to a 3D polyline uses
      the EXTRACT pattern (unlinked). This **supersedes** D-2026-08-21-a only for *analysis*; there
      is still no Watersheds *style tab* as a substitute for the algorithm.
  (k) **Surfaces in paper-space viewports and PDF plot** consume the same display-geometry batches
      model space already builds. PdfPlot currently draws none; that is a gap, not a decision.
      Filling it does not add a second contour engine.
  (l) **D-2026-08-28-a brings in** grid, corridor, contour smoothing, contour labels, TIN edge swap
      as definition, masks, slope-angle banding, volume MTEXT, Analyze ribbon, water-drop feature
      line. Still out: proximity / wall / non-destructive breaklines, Civil 3D surface import,
      DEM / point-cloud sources.
- Alternatives: **(1) A surface interface** — accepted 2026-08-28 as `ISurfaceQuery` over TIN and
  grid, not a polymorphic entity store. **(2) Persist the TIN query index and watershed polygons in `.gs`**
  — declined; they are derived and would go stale against a rebuild. **(3) Put the spatial index on
  `CadTin`** — declined; `CadEntities.hpp` stays free of `tinbuild.hpp`. **(4) Manual TIN edge swap**
  — accepted 2026-08-28 as a **definition** list (`SURFSWAPEDGE`) reapplied after each rebuild
  (D-2026-08-28-a / REQ-139), not as a mutation of the live `shared_ptr<const CadTin>`.
- Consequences: REQ-124…141; additive `.gs` fields (`clip`/`mask` kinds, `contourSources`, surface
  `kind`, grid samples, swap picks) that a legacy file omits; `CadBoundaryKind::Mask`;
  `SurfaceAnalysisMode::SlopeAngle`; `ISurfaceQuery` in util with TIN and grid implementations;
  a `mutable` query cache beside `surfaceDisplayCache`; commands `SURFACESTATS`, `DESIGNATECONTOUR`,
  `WATERSHED` / `WATERDROP` / `CATCHMENT`, `VOLUMESURFACE`, `VOLREPORT`, `SURFSWAPEDGE`,
  `SURFACECREATEGRID` / `SURFACECREATECORR`. Sequencing: Phase 1 (124–130, 135) before Phase 2 (131)
  before Phase 3 (132–134) before Phase 4 (136–141).

### ADR-040 — Developer Shell: compile-gated Test Engine, split ImGui link, Debug-only UI   (2026-08-29, accepted)
- Context: REQ-161 needs a live chrome tuner, an activity log, and a **full ImGui GUI driver** in
  Debug, with **zero** of that in Release. ADR-031 forbids a `--headless` flag that keeps GLFW/GL on
  the windowed binary's proof, and the 2026-08-16 anti-requirement forbade UI automation entirely.
  Dear ImGui Test Engine requires `IMGUI_ENABLE_TEST_ENGINE` on the **same** `imgui.cpp` the window
  links, which is incompatible with sharing one `imgui_core` (no hooks) between `GoSurvey` and
  `gosurvey_headless` if Debug `GoSurvey` enables the define.
- Decision:
  (a) **CMake option `GOSURVEY_DEVELOPER_SHELL`.** Default ON iff `CMAKE_BUILD_TYPE` is Debug;
      **forced OFF** for Release. Debug-only sources live under `src/devshell/` and are listed on
      `GoSurvey` only when the option is ON. No runtime “hidden window” in Release.
  (b) **Dear ImGui Test Engine** via FetchContent, **GIT_TAG pinned** (REQ-200), same three-question
      policy as REQ-300. User recorded the Test Engine license as in-tier. `IMGUI_ENABLE_TEST_ENGINE`
      is a compile definition on the Debug windowed ImGui objects only (`imgui_core_testengine`).
  (c) **Two ImGui static libraries:** `imgui_core` (no Test Engine — headless, tests, Release
      `GoSurvey`) and `imgui_core_testengine` (imgui sources + Test Engine sources, Debug `GoSurvey`
      only). Debug `GoSurvey` links `imgui_core_testengine` **instead of** `imgui_core`. Never link
      both into one image (duplicate `imgui.cpp`). When Developer Shell is ON, `GoSurvey` compiles
      domain sources into the windowed executable so they share the Test Engine ImGui ABI;
      `gosurvey_domain` remains the no-hook library for headless and tests.
  (d) **Ownership:** Test Engine context and the activity log are owned by Application/`devshell`
      and passed explicitly. No `DevShell::Get()`. Chrome tuner writes the existing ADR-033
      `UiChrome` instance through accessors in the UI layer; it does not add a second palette.
  (e) **Log stream is discrete events**, appended from command submit, Test Engine item actions,
      and viewport pick entry points. Per-primitive viewport draw logging is out (REQ-100 / §11.7).
  (f) **REQ-203 stands.** Headless transcripts stay the Release/CI command driver. `--devshell-run`
      is Debug `GoSurvey` only.
- Alternatives: **(1) In-tree mouse injection, no Test Engine** — declined by the user.
  **(2) Runtime `#ifdef` hide with sources still in Release** — declined; leak risk.
  **(3) Enable Test Engine hooks on shared `imgui_core` and stub them in Release** — declined;
      hooks would still exist in the shipped imgui objects.
  **(4) A `--headless` GUI on `GoSurvey.exe`** — still declined (ADR-031 (a)).
- Consequences: REQ-161; FetchContent `imgui_test_engine`; Debug-only CLI; a Release ctest that
  `dumpbin`s `GoSurvey.exe`. Screenshot golden images remain out of scope.

### ADR-041 — LibreDWG is the DXF/DWG codec; DWG write stops at R2004   (2026-08-29, accepted)
- Context:    ADR-024 shipped Phase 1 (DWG↔DXF via ODA File Converter or `accoreconsole`) and left
  the native codec as later work. `docs/dwg-plan.txt` PART 4 listed Route A (in-tree), B (LibreDWG),
  C (ODA SDK), D (converter). The user chose **B** for File Format Specs (D-2026-08-29-g): a full
  DXF/DWG codec in-process, DWG **write up to 2004**, no ODA membership. Linking LibreDWG is GPL-3.0,
  which the 2026-07-30 open-source decision already allowed. R2018 write still fails CRC upstream;
  the user accepted down-convert rather than waiting for it.
- Decision:
  (a) **GNU LibreDWG is the CAD interchange codec.** IO owns a wrapper (`io/` beside `DwgIo` /
      `DxfIo`) that talks to LibreDWG. Commands/UI keep the existing File Import/Export entries.
      ADR-024’s four-function seam may be kept as names; the **implementation** behind Import/Export
      DWG/DXF becomes LibreDWG, not `DxfIo` + a child process.
  (b) **Read:** every DWG version LibreDWG decodes (through AC1032 / R2018) and DXF (ASCII and
      binary as the library supports). No converter required for the happy path.
  (c) **Write DWG:** R2000 (AC1015) and R2004 (AC1018) only. **Default R2004.** R2007+ emit is
      refused with a message, not a Recover-bait file. DXF write uses LibreDWG’s DXF writer for
      versions it supports; the log still names every GoSurvey type that has no DXF/DWG
      representation (meshes, TIN, clouds, PDF).
  (d) **Write is synthesized from the GoSurvey document**, not a bit-exact rewrite of an unread
      R2018 database. Unknown-object preservation (DM-08) remains a **future** requirement.
      Opening an R2018 file and saving R2004 **must** list what will be dropped (REQ-052 honesty,
      REQ-201).
  (e) **Licence:** the application that links LibreDWG is **GPL-3.0-or-later**. Headers, COPYING,
      and the installer licence text follow. No dual-licence carve-out in this ADR.
  (f) **Phase 1 converter** remains until REQ-170 acceptance is green, then is removed from the
      user-facing open/save path. It may stay as a **test oracle** (diff native parse vs converted
      DXF) without being a runtime dependency for customers.
  (g) **MSVC:** LibreDWG is built with the pinned `cl` + Ninja presets (project.md §7). If
      upstream CMake is untested on MSVC, that is integration work in IO/Build, not a second
      compiler.
- Alternatives: **(1) Route A in-tree codec** — rejected; months of bit packing for a writer we
  are capping at R2004 anyway. **(2) ODA Drawings SDK** — rejected by the user (cost + proprietary
  SDK). **(3) Keep converter forever** — rejected (ADR-024 (d)). **(4) Write R2018 anyway** —
  rejected; upstream CRC/AUDIT failure is worse than an honest R2004 file.
- Consequences: REQ-170; `spec/file-format-specs.md`; GPL-3 on the product; a large C dependency
  in `third_party/` or FetchContent with a **pinned** revision (REQ-200). Domain mapping limits
  (exploded INSERT until REQ-107, one paper layout, no DM-08) stay visible in the import/export
  log. REQ-112 (binary DXF) is **subsumed** by LibreDWG’s DXF path when REQ-170 lands.

### ADR-042 — Point clouds, IMAGE underlays, and IFC-as-mesh   (2026-08-29, accepted)
- Context:    File Format Specs adds E57/LAS/LAZ/PTS/PTX, JPEG/PNG/BMP, and IFC viewing. None of
  those are CAD entities today. REQ-063 meshes are the only large immutable geometry precedent.
  Surfaces (ADR-028) are **not** the home for LiDAR (REQ-068 D4). PDF underlays are the precedent
  for a georeferenced raster that is not draftable CAD.
- Decision:
  (a) **Point cloud is a new entity kind** (reference geometry: visible, selectable, erasable,
      layer-controlled, in extents; **not** grip-edited, not a TIN definition source in this epic).
      Payload is `shared_ptr<const …>` (§11.5), interleaved XYZ plus optional RGB and intensity.
      Coordinates obey local storage + `worldDocumentOrigin` (REQ-101). Multiple setups from one
      PTX become multiple clouds or one cloud with recorded setup metadata — Workshop picks the
      smaller option that preserves per-setup transforms; it must not invent a “scan project”
      document type.
  (b) **IMAGE is a new underlay kind** analogous to PDF attach: path, insertion, rotation, scale
      in drawing units, layer. Decode via existing stb_image. Not a second renderer. Missing file
      on open → logged unload, drawing otherwise loads (REQ-001 does not mean abort the whole
      `.gs`).
  (c) **IFC import tessellates to REQ-063 meshes.** No IFC object graph in the domain, no IFC
      write, no second BIM layer. IfcPlusPlus (MIT) is the planned parser; Open CASCADE is **not**
      added unless a later recorded decision proves tessellation cannot be done without it.
  (d) **Libraries** per `spec/file-format-specs.md` §5. PTS/PTX and a LAS reader are in-tree unless
      LAS coverage fails. No “IPointCloudFormat” plugin API (§11.4).
  (e) **DXF/DWG export** of clouds and IMAGE in this epic: IMAGE may be written when the DWG/DXF
      mapping exists (IMAGEDEF + IMAGE); clouds are **logged exclusions** (no native point-cloud
      object in R2004 that we will emit). IFC-sourced meshes follow REQ-063’s DXF/DWG exclusion.
- Alternatives: **(1) Feed clouds into TIN** — rejected (density + REQ-068 D4). **(2) IFC as
  native BIM** — rejected (view only). **(3) ODA Scan-to-BIM for RCP/E57** — rejected (this epic
  uses open libraries). **(4) Reuse mesh entity for clouds** — rejected; a cloud is not indexed
  triangles and would overload REQ-063.
- Consequences: REQ-171–REQ-174; renderer gains a point splat or chunked point path (not specified
  here beyond “must not break REQ-100 line/mesh/surface profiles on drawings **without** a huge
  cloud”); a later REQ-100 **cloud profile** is required before claiming city-scale E57. `.gs`
  gains additive sections (no version bump if keys are tolerant, ADR-020 (e) precedent) or a
  REQ-079 migration if the loader cannot ignore unknown sections — Workshop must not bump
  `.gs` version without amending REQ-079.

### ADR-044 — DWG drawing document with a GoSurvey JSON trailer   (2026-08-29, accepted)
- Context:    dwg-plan PART 11 listed per-field EED + GOSURVEY dictionary XRECORDs + native LAYOUT
  objects as the native-format design, blocked on DM-08 and layout/block mapping. The user asked
  to make File Open/Save DWG **now**, keep `.gs` code, and preserve survey-specific data. LibreDWG
  already synthesizes R2000 from the domain (ADR-041 (d)).
- Decision:
  (a) **The drawing file is `.dwg`.** File Open/Save/Save As and headless OPEN/SAVEAS use it.
      Drawing choosers do not list `.gs`.
  (b) **Preservation is the existing `.gs` JSON** (`BuildRoot` / `LoadGoSurveyFromJsonUtf8`),
      appended after a valid LibreDWG file as a versioned trailer (`GOSURVEY_DOC` magic + length +
      UTF-8 JSON). GoSurvey Open prefers the trailer when present; otherwise REQ-170 CAD import.
  (c) **No second schema and no new dictionary/EED mapping in this increment.** Per-field EED /
      XRECORD (dwg-plan P-01..P-05) remains a later option. DM-08 is still not claimed.
  (d) **`.gs` APIs stay.** Workspace template and an explicit `.gs` path still use `GsIo`.
      **Amended D-2026-09-03-h / issue #264 (2026-09-04):** standalone `.gs` as an openable
      document is retired outright — no explicit `.gs` path anywhere, including WBLOCK/BLOCKIMPORT
      (block-library replacement spun to issue #284). The workspace template moved to
      its own narrower format, `.gst`, with a dedicated reader/writer
      (`SaveGoSurveyTemplateFile`/`LoadGoSurveyTemplateFile`) — same JSON shape, no file
      association. `BuildRoot`/`LoadGoSurveyFromJsonUtf8` (clause (b)) are unaffected.
  (e) **Headless SAMEFILE** on two GoSurvey DWGs compares **trailer JSON**, not the synthesized CAD
      body, because LibreDWG encode is not the document-identity oracle (REQ-079 still applies to
      the JSON tree).
- Alternatives: **(1) Wait for EED/XRECORD + DM-08** — rejected by the user (ship DWG save now).
  **(2) Dual-write sidecar `.gs`** — rejected; one drawing file. **(3) JSON-only `.dwg` with no
  CAD entities** — rejected; AutoCAD must still see LINE/CIRCLE/etc.
- Consequences: REQ-175; `io/DwgIo` owns trailer attach/extract; UI uses DWG dialogs; fuzz and
  transcript SAVEAS paths become `.dwg`.

### ADR-043 — Block editing in place: a model-store swap, not a fourth active space   (2026-08-29, accepted)
- Context:    REQ-107's block-editor slice requires editing a `CadBlockDefinition`'s geometry in
  **isolation** — the block's entities only, model and paper space hidden and unpickable — with the
  full draw/modify/snap command set operating on that content, and a Save/Don't-Save/Cancel prompt
  on close that returns to the ribbon tab and camera the user left. Today BEDIT sets
  `blockEditorName` and edits `blockDefs[i].content` through dedicated `BEDITADD`/authoring commands
  only; `main.cpp` does not know a session is open and no ordinary draw/modify command can reach
  block content. Adding an editing surface + hiding the other spaces + a close gate touches the
  command/coordinate flow and session lifecycle — architectural, not a Workshop choice (§3, §11).
- Decision:   (a) **A block-edit session swaps the model store, it does not add a routing branch.**
  On enter, the live model geometry (lines, circles, arcs, ellipses, polylines, model TEXT/MTEXT,
  filled regions, selection, camera) is saved to an off-document **stash** on `AppCommandState`
  (session-only, never serialized), the model arrays are cleared, and the definition's
  `CadBlockContent` primitives are loaded **into the model arrays** in the block's local
  coordinates (base point already baked to the origin, `CadBlockBakeBasePoint`). Every existing
  draw, modify, and object-snap command then operates unchanged — there is **no per-command
  `activeSpaceIndex` branch** for block edit, unlike ADR-009's paper routing. Rationale: the paper
  branch is ~15 scattered call sites and growing; a store swap is one enter path and one close
  path and cannot drift per command (CLAUDE.md "prefer simple").
  (b) **The other spaces are hidden by the same swap** — model geometry is gone from the arrays
  while the session is open, so the viewport, picking, and snapping already show and hit only the
  block content. `main.cpp` gains one guard: while `blockEditorName` is non-empty, paper-space
  entry (`PSPACE`/layout tabs) and survey-only tools are refused, and block INSERT overlays are
  not drawn (the definition being edited is the scene, not a reference).
  (c) **Close is a gate.** `BCLOSE` with `blockEditorDirty` raises a modal
  (Save / Don't Save / Cancel). Save harvests the model arrays back into
  `blockDefs[i].content`, restores the stash, and re-renders every `CadBlockRef`. Don't Save
  restores `blockEditorSnapshot` (already captured on enter) into the definition, then restores the
  stash. Cancel dismisses the modal, session stays open. Either completion restores the saved
  camera and `ribbonTabBeforeBlockEditor` (both already partly wired).
  (d) **Scope of the round-trip is primitive geometry.** Nested blocks, meshes, attribute
  definitions, parameters, and actions on the definition are stashed on enter and restored on close
  **unchanged** — this slice edits drawable primitives, not the dynamic-block authoring model
  (which keeps its existing `BPARAM`/`BACTION` commands). A session is **model-space only**:
  entered from model space, and BEDIT is refused while a paper layout is active.
  (e) **Session state lives on `AppCommandState`**, beside the existing `blockEditor*` and
  `floatingViewportIndex` fields — the established pattern for interactive-mode state (ADR-008/009).
  No new file-scope global (§11.3), no new abstraction (§11.4 — the stash is a value struct with
  one use), no new dependency, no `.gs` format change.
- Alternatives: **(1) A fourth `activeSpaceIndex` value with per-command routing** (the literal
  "fourth active space" framing) — rejected: replicates ADR-009's scattered-branch cost across
  every draw/modify/snap handler for no behaviour the store swap does not already give, and each
  new command becomes another integration point that can forget the branch.
  **(2) A separate MDI drawing tab** (the user's first framing) — rejected by the user: grafts
  non-document state onto `drawingTabs`/`documents[]` and their per-tab renderers, a new ownership
  model for a resource that is not a drawing.
  **(3) Keep in-place `BEDITADD`-only editing** — rejected: no isolation, no ordinary draw tools,
  fails the requirement.
- Consequences: `AppCommandState` gains a `blockEditModelStash` value struct + a saved-camera
  field; `CadBlocksEnterNamedEditor` / `BCLOSE` gain the swap and harvest logic; `main.cpp` gains
  one visibility/entry guard; the block-editor contextual ribbon (already built) gets the close
  modal. Undo inside a session operates on the model arrays as usual; the enter/close swap itself
  pushes an undo boundary so a mis-started session is recoverable. A crash mid-session loses the
  session (stash is not persisted) but never corrupts the `.gs` — the definition is only written on
  an explicit Save. If a later requirement needs simultaneous multi-block editing or editing a
  block from a paper layout, this swap does not extend to it and a new decision is required.

### ADR-045 — The B-rep solid kernel: analytic faces, a remembered recipe, derived tessellation   (2026-09-01, accepted)
- Context:    GitHub issue #146 (Phase 3 of #120) asks for a boundary-representation kernel and the
  seven primitive solids on top of it. There was no solid kernel in GoSurvey at all: no B-rep, no
  face or edge topology, no solid entity. `CadMesh` (ADR-026 (c)) is explicitly import-only
  reference geometry and cannot answer any of the questions a solid must — it has no volume, no
  centre, and no faces that mean anything. Everything in issue #120's Phases 4–6 (extrude, revolve,
  sweep, loft, boolean union/subtract/intersect, slice, fillet, chamfer, sectioning, mass
  properties) is built on whatever is decided here, so this is the decision the rest of the epic
  inherits. Issue #120 states one constraint directly: *"do not tightly couple geometric
  calculations to the renderer. The geometry engine should be usable without a graphics context."*
  ADR-026 (b) already recorded the counterpart fact — that a B-rep format "needs a geometry kernel
  to tessellate, which is a larger project than everything else here combined."
- Decision:
  (a) **Topology is the stored truth, and it is a real B-rep**: solid → shells → faces → loops →
  edges → vertices, exactly the hierarchy issue #146 names. Edge uses are directed, so a loop is an
  ordered ring of (edge, reversed) pairs. This is what gives a Phase 4 boolean somewhere to write its
  result: a subtraction produces a shape that is not any of the seven primitives, and no parametric
  description can express it.
  (b) **Every face carries an ANALYTIC surface, never a facet**: `Plane`, `Cylinder`, `Cone`,
  `Sphere`, `Torus`, each with the frame it lives in. A whole sphere is ONE face. Every edge
  likewise carries a `Line` or an `Arc`. Three things follow, and each is a requirement rather than
  a nicety: volume and surface area are *integrated in closed form*, so a sphere reports
  `4/3 pi r^3` rather than a facet sum that drifts with display settings (REQ-101 is ±0.01 and a
  faceted sphere cannot meet it without an absurd facet count); a stored solid is a handful of faces
  rather than megabytes of triangles; and tessellation quality becomes a pure display setting, which
  is what issue #120 means by *"changing tessellation quality should not modify the underlying
  solid."*
  **Amended 2026-09-02 (D-2026-09-02-i): closed-form is the rule for every *analytic* face; a face
  bounded by a procedural intersection curve (B2b-2, `CurveKind::Intersection`) is integrated by
  adaptive numerical quadrature** to a tolerance far inside REQ-101's ±0.01 ft.
  **Widened 2026-09-03 (D-2026-09-03-b, ADR-048): a face whose surface is `SurfaceKind::Nurbs`
  (the freeform loft / sweep face) is integrated by the same adaptive quadrature.** The quadrature
  grid stays independent of the display chord tolerance, so tessellation quality is still not part of
  the model; every *analytic* face keeps its exact closed form. The quartic where two
  non-coaxial cylinders meet has no elementary integral, so a `T`-pipe's saddle face is the one face
  type that cannot be exact — every other face still is, and the quadrature tolerance keeps the
  answer's *error* below the display-drift a facet count would cause anyway. Tessellation quality is
  still not part of the model: the quadrature grid is independent of the display chord tolerance.
  (c) **A primitive also remembers the recipe it was built from** — kind, placement frame, and its
  dimensions. It is *not* the geometry: `Validate`, `ComputeMassProperties` and `Tessellate` all read
  the topology and never the recipe, so a recipe that disagreed with its solid could not silently
  change an answer. It exists so the Properties panel can say "Radius 12" instead of "one
  cylindrical face", and so #120's parametric-modelling section is not designed out. A solid with no
  recipe (`PrimitiveKind::None`) is a first-class citizen, which is the case that proves the
  topology and not the recipe is the truth.
  **Amended 2026-09-02 (D-2026-09-02-h): `CurveKind` is `{Line, Arc, Ellipse}`.** An oblique plane
  cutting a cylinder meets it along an ellipse — closed-form (centre, semi-major, semi-minor,
  parameter span), so an `Ellipse` edge carries the same `frame + radius + sweep` an `Arc` does plus
  one field, `radius2` (the semi-minor axis). This is Boolean increment B2b-1 (ADR-046). It is the
  first stored geometry kind an older `.gs` reader cannot tolerate, so it bumps `kGsFormatVersion`
  (the bump ADR-045 (e) said would land "in the increment that first" needs it); a drawing with no
  ellipse edges still serializes byte-identically. The general intersection curve (a
  cylinder∩cylinder quartic) is a *procedural* `CurveKind` still deferred, to B2b-2.
  (d) **Curved surfaces are split at seams into faces that each bound normally.** A cylinder side is
  two half-faces, a sphere two half-spheres cut by a meridian, a torus four patches. The alternative
  — one face with a seam edge used twice by itself — makes "every edge bounds exactly two faces" a
  special case rather than an invariant, and that invariant is the single most useful thing
  `Validate` has.
  **Amended 2026-09-02 (D-2026-09-02-c): a curved face may carry `Surface::inward`.** As originally
  built, a curved face's outward direction was fixed by its surface (+radial for cylinder/cone/
  sphere/torus) with no reversed form — "one fewer thing that can disagree with the topology", and
  `Problem::ProfileArcReflex` refused the extrude shapes that would need one. The Booleans (increment
  B2a) force the general answer, exactly as this ADR's own alternatives note anticipated: subtracting
  a cylinder leaves a hole wall whose material is on the −radial side. A single `bool inward` on
  `Surface`, default false and never set by the seven primitives, marks that. The normal evaluators
  negate, the tessellator reverses winding, and the volume integrand flips sign, so an inward face
  correctly *subtracts* the void it bounds; `ClosestPointOnSurface`, `Validate` and `.gs` are
  unaffected (`.gs` gains an additive tolerant key — no `kGsFormatVersion` bump). `ProfileArcReflex`
  stays for now; a reflex profile arc in an extrude is a separate feature, now unblocked.
  **Taken up 2026-09-03 (D-2026-09-03-e): `Extrude` no longer raises `Problem::ProfileArcReflex` and builds
  a reflex arc.** It needed nothing but the flag above. After the builder's walk the loop runs CCW
  about the extrusion direction, so an arc whose sweep is still positive has its centre on the
  interior side and sweeps an ordinary outward cylinder, while a negative one has its centre outside
  and sweeps precisely the inward face B2a already defined. The span is stored increasing and the
  orientation carried by `inward`, which is the convention the bore walls set — a negative
  `uEnd - uStart` would make the face's own AREA come out negative, and an area is a magnitude. Both
  halves are load-bearing and measured: with the flag left false, or the span left decreasing,
  `Validate`'s geometric closure probe rejects the solid outright and `Extrude` returns false. The
  reason no other code changed is that (d) above had already done the work — this is the caller that
  collects it.
  (e) **`Validate` proves manifoldness, orientability and closure — including GEOMETRIC closure.**
  Beyond the index/ring/tally checks, the volume integral is taken about two different reference
  points and required to agree. On a closed surface it must (the closed integral of `n dA` vanishes);
  on a face whose parametric span disagrees with its own boundary loop it does not. That case is
  topologically flawless and geometrically a hole, it is exactly what a Phase 4 trim can produce,
  and nothing else in the check can see it.
  (f) **Self-intersection is refused at construction, not detected afterwards.** **Amended
  2026-09-01 (D-2026-09-01-f): a torus whose tube EXCEEDS its ring radius is now BUILT** - the
  self-intersecting shape AutoCAD makes and users draw deliberately - and only the exactly-equal case
  stays refused, where the inner equator collapses to a point and the rim edges have zero radius. Such
  a solid is valid topology and draws correctly, but reports NO volume or surface area
  (`brep::SelfIntersects` gates `ComputeMassProperties`): a surface that encloses part of space twice
  makes the closed form a number rather than an answer, and printing it would be the silent wrong
  answer REQ-201 exists to prevent. The original clause read: For the seven
  primitives the only route to a self-intersecting shell is a bad parameter — a torus whose tube
  swallows its own axis — and each such parameter is refused by name (REQ-201). A general
  surface-surface intersection test belongs with the Phase 4 booleans, which are the first operation
  that can actually produce one; building it now would be an untested engine with no caller.
  (g) **The kernel is `double`, frame-agnostic, and knows nothing about the document.** It is a pure
  `util/` module beside `ray3d`, `ucs` and `tinbuild` — no GL, no ImGui, no `AppCommandState` — which
  is the ADR-002 layering that makes the whole suite reachable without a window. Narrowing to the
  `float` local storage the GPU wants happens **above** this layer, once, where the document origin
  is known (REQ-101, architecture §11.8). Numerical stability at state-plane magnitudes comes from
  integrating every face about a reference point ON the solid, so no term is a difference of two
  large nearly-equal numbers.
  (h) **`ucs::Ucs` is the frame type throughout** — for a surface, for an arc edge, and for
  placement. REQ-311 already settled that there is exactly one plane/frame type in this project, and
  a kernel that introduced a second would reopen the disagreement that decision closed.
  (i) **Solids are EXCLUDED from DXF/DWG export, with an explicit message naming what was skipped.**
  A real solid in DXF/DWG is an ACIS `3DSOLID` — a proprietary binary B-rep we cannot write without
  a large third-party kernel that REQ-300 does not permit. This is the same boundary ADR-026 (c)
  drew for `CadMesh` and for the same reason, and it is stated out loud rather than dropped
  silently (REQ-201). Writing a tessellated approximation instead was considered and rejected by the
  user: it hands back a picture of the solid that round-trips as an uneditable bag of triangles with
  an approximate volume. If that is wanted it is an explicit opt-in export and its own issue.
- Alternatives: **(1) Recipe-only parametric primitives** (no topology; faces generated on demand) —
  smallest possible kernel, exact volumes for free, tiny files. Rejected because Phase 4 has nowhere
  to put a boolean result, so the real kernel would have to be built anyway, *and* every solid
  already saved in a customer's file would then need migrating. The saving is borrowed, not earned.
  **(2) Faceted B-rep** (real topology, curved surfaces baked to triangles at creation) — simplest
  maths, and booleans are conceptually easier. Rejected on three counts, any one of which is fatal:
  a faceted sphere's volume misses REQ-101 unless the facet count is enormous; the file grows by
  orders of magnitude; and the tessellation quality becomes part of the model, which #120 explicitly
  forbids.
  **(3) Vendor an existing kernel** (OpenCASCADE, or ACIS/Parasolid under licence) — the honest
  comparison, and the reason it is not taken is REQ-300 and project.md §7: OCCT is a dependency an
  order of magnitude larger than everything in `third_party/` combined, and the commercial kernels
  are not licensable on this project's terms. Recorded so the choice is not re-litigated from
  scratch; the trigger to revisit is Phase 4 booleans proving intractable in-tree, which is a real
  possibility and a far better place to make that call than here, with a working primitive kernel
  already in hand.
  **(4) Reuse `CadMesh`** — a mesh has no faces, no edges and no volume; ADR-026 (c) already ruled it
  reference geometry precisely so that mesh editing, mesh snapping and mesh export stayed out of
  scope. Nothing about it is closer to a solid than starting from nothing.
- Consequences: a new pure `util/brep` module (header + one TU) and its Catch2 suite. **Increment 1
  changes no existing source file at all**, which is what makes it unable to regress anything — the
  only edits outside `src/util/brep.*` are two CMake source-list entries. Increment 2 is where the
  blast radius lands: a `CadSolid` entity and its store, seven commands, `.gs` persistence, the
  REQ-064 shaded/hidden render path, a tessellation cache keyed so it is not rebuilt per frame
  (REQ-100), face and edge snapping in `CadSnap`, and the DXF/DWG exclusion message from (i).
  **Deliberately not addressed here**, each to be decided when it has a caller: general polygon
  triangulation for non-convex or holed plane faces (the centroid fan used now is correct for every
  face the seven primitives make, and is refused rather than guessed for anything else); centroid and
  moments of inertia (#120 Phase 6); a general self-intersection test (Phase 4, per (f)); and any
  interchange format for solids (STEP/STL/OBJ), which ADR-026's interchange discussion covers and
  which no accepted requirement asks for.

#### ADR-045 addendum — the document-facing half   (2026-09-01, accepted)
- Context: ADR-045 settled the kernel and named increment 2's blast radius without deciding its
  shape. These are the calls made building it, recorded here rather than left in the code, because
  four of them are visible to the user and one changes the `.gs` format.
- Decision:
  (a) **A solid is a `shared_ptr<const brep::Solid>` in the store, in STORAGE coordinates** — X/Y
  local, Z absolute, the ADR-025 D2 convention every geometry store uses. Shared and immutable for
  the reason `CadMesh` and `CadTin` are (architecture §11.5): an undo snapshot is a refcount bump.
  It is the one store held in `double` rather than `float`, and the exception is narrow and earned:
  §11.8's float convention exists for arrays with millions of entries headed for a vertex buffer,
  where a solid's B-rep is a handful of vertices — narrowing would throw away the exactness the
  closed-form volume depends on and buy nothing. The **tessellation**, which really is GPU-bound and
  really can be large, is narrowed to float in exactly one place.
  (b) **Authoring is one typed line per primitive, with the active UCS supplying the orientation.**
  `BOX <X,Y[,Z]> <length> <width> <height>` and its six siblings. This is what REQ-313's acceptance
  asks for — "exact dimensions typed at the command line" — and no more. Reusing the UCS is what
  gives a cylinder or cone an arbitrary 3D axis with no new command and no axis argument, which is
  the rule REQ-312 already settled for tilted arcs and circles. **No interactive pick-and-drag flow**
  in this increment: rubber-banding a solid needs a 3D draft preview, that is #120's Phase 5
  direct-modelling work, and inventing it here would be scope no requirement asks for. The usage
  text says so, so a bare `BOX` explains what the command wants rather than opening a prompt that
  never comes.
  (c) **Solids render in EVERY visual style, and "Hidden" means hidden-line.** This is the opposite
  of ADR-026 (e)'s mesh rule and for the reason ADR-026 (c) itself gives: a solid HAS real edges,
  where a mesh's "edges" are artefacts of an exporter's resolution. 2D Wireframe draws the edges
  only; **Hidden writes the faces into the depth buffer with colour writes off** and then draws the
  edges on top; Shaded lights the faces and draws the edges over them. Without the depth-only pass,
  "Hidden" would mean nothing for a solid — there would be nothing to hide behind. A polygon offset
  separates an edge from the face it bounds; that is load-bearing, not a tweak, because an edge lies
  exactly ON its face and without a bias half of every silhouette drops out in speckles.
  (d) **The tessellation cache is keyed on `(solid pointer, chord tolerance, isoline count)` and
  nothing else** (isoline count added 2026-09-01, D-2026-09-01-g, per (j) below). A
  solid is immutable, so an unchanged pointer means unchanged geometry; the early-out sits before any
  allocation (the §11 invariant 7 lesson the surface cache already learned). The cache lives on
  `AppCommandState` and is **outside every undo snapshot**, exactly as ADR-036 (e) put the surface
  display cache outside one, and for the same reason: it is derived. Entries key on a `weak_ptr`, so
  an erased solid's entry expires and is reaped rather than being matched by a new solid allocated at
  the freed address.
  (e) **REQ-100 gains a fourth profile, `BENCH SOLID`.** Not implied by the mesh profile: a solid
  scene is many small stream-uploaded batches with a cache lookup each, where the mesh profile is one
  large indexed upload, and those are different frames. It is also the only instrument that can catch
  the failure #120 names directly — a tessellation being regenerated per frame would show up here and
  nowhere else. The scene is many solids rather than one big one for exactly that reason.
  (f) **`.gs` gains a `solids` section carrying the TOPOLOGY, not the recipe.** Rebuilding from the
  recipe on load would mean a Phase 4 boolean result — which has no recipe — could not be saved at
  all. Additive and omitted when there are none, so every pre-REQ-313 drawing still serializes
  byte-identically (the ADR-020 (d) tolerant-key precedent). Every solid is **validated on load** and
  refused with the kernel's own reason (REQ-201): an invalid solid does not crash, it quietly reports
  a wrong volume and hands Phase 4 a shape that is not closed. Frames are written through the
  `UcsFrameToJson` pair REQ-154 already defined, and that reuse is worth more than the saved lines —
  its reader refuses a frame that is not right-handed orthonormal, so a hand-edited file cannot
  present a skewed surface frame that would silently shear a solid.
  (g) **Two new object-snap kinds, `Edge` and `Face`, behind ONE `objectSnapSolid` preference.** A
  solid's VERTICES answer the existing Endpoint toggle and its edge MIDDLES answer Midpoint — those
  snaps already mean exactly that, and a user with Endpoint on expects a box corner to snap. Edge and
  Face are the two halves of "snap to a solid" and no requirement asks to enable one without the
  other, so a second preference would be an unearned option (REQ-301). **The face answer is projected
  onto the analytic surface**: the ray finds the triangle, `Tessellation::triFace` says which face it
  belongs to, and `ClosestPointOnSurface` puts the point on the real surface — so on a cylinder it
  lands on the cylinder rather than a sagitta short of it on the tessellator's chord (#120: "the
  resulting point should lie exactly on the selected face"). Face snapping needs a pick ray and is
  skipped without one; in a plan view there is no "under the cursor" to resolve, and answering with
  the work-plane point would be an invention.
  (h) **A solid is selectable and erasable; every transform REFUSES it with a stated reason.** The
  click funnel picks against the solid's EDGES — what is drawn in every style, and in 2D Wireframe
  the only thing on screen — and the box-selection walk uses the analytic bounds, because a sphere's
  two stored vertices describe almost none of it. MOVE/COPY/ROTATE/SCALE/MIRROR/ARRAY/STRETCH each
  drop solids from the selection and say how many and why, the rule Surface already established:
  a solid silently left behind while everything selected with it moves is the outcome that must not
  happen (REQ-201). Transforming a solid means transforming every surface frame and every arc-edge
  frame in its topology — the same class of work REQ-312 needed for one tilted arc — and belongs with
  #120's Phase 5.
  (i) **Solids are not captured into block definitions**, matching surfaces, tables and feature lines,
  which are not either. They ARE cleared when the block editor isolates the model (ADR-043's store
  swap), so a solid from the drawing cannot leak into a block being edited.
  (j) **A curved face's wireframe carries ISOLINES, generated in the kernel and appended to the EDGE
  buffer** (added 2026-09-01, D-2026-09-01-g). They live in `brep::TessellateIsolines` rather than in
  the renderer because they are geometry: they come from the same analytic `SurfacePointAt` the shaded
  triangles use, so an isoline and the shading beside it cannot disagree about where the surface is,
  and putting them in the render layer would be a second surface evaluator to keep in step. **The
  directions are per surface kind** — cylinder and cone rulings along the axis only, sphere meridians
  and latitudes, torus tube and ring circles, plane none — because one blanket rule would draw a ring
  part way up a cylinder, which reads as an edge that is not there: a seam, or the join of two stacked
  solids. **The grid is global to the surface's own frame and sampled strictly inside each face's
  span**, since every curved primitive here is seamed into half-faces per the parent ADR: a per-face
  grid bunches the lines where two faces meet, and a non-strict test doubles a seam edge that is
  already drawn. They go into the **same** vertex buffer as the edges rather than a batch of their
  own, because they are the same colour and weight as the object — a separate stream would be another
  thing to keep in step for no visible difference, and the evidence the seam is in the right place is
  that **the renderer needed no change at all**. The count is per full turn (AutoCAD's `ISOLINES`
  semantics), is a viewport setting rather than a per-solid property — it is a display preference like
  the visual style, and per-solid would mean a `.gs` change and a property no one asked to vary — and
  it joins the cache key in (d) because a derived representation that ignores an input that changes it
  is a stale one.
- Consequences: `.gs` grows one additive section and two settings keys (`objectSnapSolid`,
  `viewportSolidIsolines`); no `kGsFormatVersion` bump. `RenderScene` gains one parameter, not four — the faces and edges of a
  solid are always built together and always consumed together, the same argument
  `CadSurfaceDisplayGeometry` records for itself. **Still not addressed**, each waiting for a caller:
  interactive placement and 3D grips (#120 Phase 5), transforming a solid (same), booleans (Phase 4),
  centroid and moments (Phase 6), any interchange format for solids, and **view-dependent silhouette
  curves** — AutoCAD draws those too, they move as the view orbits, and being a render pass rather
  than geometry they do not belong in the kernel alongside (j)'s isolines.

### ADR-046 — Feature operations on the solid kernel: analytic extrude / revolve / slice, and phased analytic Booleans   (2026-09-02, accepted)

- **Status:** accepted (2026-09-02, D-2026-09-02-a). The Boolean *method* (analytic B-rep, not
  mesh-based) and the spec-first, sliced delivery were chosen by the user, who then accepted this
  ADR text as written. Backs REQ-314 and REQ-315. GitHub issue #147, Phase 4 of #120.

- **Context.** REQ-313 / ADR-045 gave GoSurvey a boundary-representation kernel whose defining
  choice is *analytic faces*: a face is a plane, cylinder, cone, sphere or torus, an edge is a line
  or an arc, and volume and area are closed-form integrals so they do not move when the display
  changes. Phase 4 (issue #147) must build on that kernel: extrude and revolve a profile into a
  solid, slice a solid with a plane, and union / subtract / intersect two solids. Issue #147 calls
  the Booleans *"the highest-risk item in all of #120 — where solid modellers classically fail on
  degenerate input,"* and REQ-201 forbids ever storing a solid that fails validation.

- **Decision.**

  **(a) Extrude and revolve are analytic and exact, and they are first.** Every face an extrude or
  revolve of a line-and-arc profile can produce is already one of ADR-045's five surface kinds, and
  every edge is a line or an arc:
  - extrude: line → plane, arc → cylinder, tapered line → plane, tapered arc → cone;
  - revolve: line ∥ axis → cylinder, line skew to axis → cone, line meeting axis → plane/cone,
    arc centred on axis → sphere, arc centred off axis in the axis plane → torus.
  So these two need **no new surface or curve type** — only a builder that walks the profile,
  emits the swept face for each segment, and closes the ends with cap faces. They are the first
  increment because they deliver visible value with zero kernel-representation risk.

  **(b) Slice is next, and it is the stepping stone to Booleans.** Cutting a solid by a plane needs
  plane-∩-face intersection, face splitting, and inside/outside classification against a
  half-space — every ingredient a Boolean needs, minus surface-∩-surface intersection between two
  curved operands. Slice is where that machinery is built and tested against a case whose answer is
  easy to hand-check (the two pieces' volumes sum to the original).

  **(c) Booleans are analytic B-rep, and phased by intersection-curve difficulty.** The user chose
  the analytic route over mesh-based Booleans, keeping faith with ADR-045: a subtracted cylinder
  leaves a true cylindrical hole, not a faceted one, and the result's volume stays closed-form.
  The cost is that a general analytic Boolean needs to represent the curve where two surfaces
  cross, and that curve is often **not** a line or an arc — a plane cutting a cylinder obliquely
  gives an ellipse; two non-coaxial cylinders give a quartic space curve. The kernel's `CurveKind`
  is `{Line, Arc}` today. Rather than block all Boolean work on a general intersection-curve
  representation, the Booleans are delivered in two increments:
  - **Increment B1** — operand pairs whose every intersection curve is already a line or an arc:
    box ∩ box, box ∩ axis-aligned cylinder, coaxial cylinder ∩ cylinder, sphere ∩ plane, and the
    like. A pair that would need a curve outside `{Line, Arc}` is **refused by name** (REQ-201),
    never approximated. This ships a working, verifiable Boolean. **Refined 2026-09-02
    (D-2026-09-02-b):** within B1, curved operands are supported for **UNION and INTERSECT only** —
    those create only outward-facing curved faces. A curved **SUBTRACT** (a round hole / bore) leaves
    a cylindrical wall facing *inward*, which ADR-045's `Surface` cannot express (no reversed flag),
    so it moves to B2 — the increment that adds the general answer to inward-curving faces. Curved
    SUBTRACT is refused by name in B1.
  - **Increment B2** — lifts B1's refusals. **Split 2026-09-02 (D-2026-09-02-c) into B2a then B2b:**
    - **B2a** — **inward-facing analytic faces** (`Surface::inward`, see ADR-045 (d) amendment). No
      new curve type: the operand pairs B1 already recognises geometrically (cylinder / sphere /
      coaxial-cylinder cut) have circular intersection curves, already `CurveKind::Arc`. This lifts
      **curved SUBTRACT** — round through-holes, blind pockets, spherical dimples, counterbores —
      the highest-value refusal, since "subtract a cylinder to drill a hole" is the defining Boolean.
    - **B2b** — a **general analytic intersection-curve type** (a parametric procedural curve
      evaluated from its two surfaces, tessellated on demand), lifting the oblique / non-coaxial
      refusals (ellipse, quartic) pair by pair.

  **(d) Operands are consumed only after the result validates.** A feature operation computes its
  result into a fresh `Solid`, runs `brep::Validate` (and, for Booleans, `brep::SelfIntersects`),
  and only then does the command layer replace the operands in the `CadSolid` store — as one undo
  snapshot. A failure returns a named reason and the document is untouched. This is REQ-201 applied
  to geometry that can fail in a hundred subtle ways.

  **(e) A feature result stores topology, and optionally a recipe.** ADR-045 already made the recipe
  optional and named the Boolean result as the recipe-less case. Extrude and revolve **may** record
  an operation recipe (source-profile entity id + parameters) for future parametric edit, but it is
  never consulted by validity, mass properties or tessellation, and a re-opened solid whose recipe
  will not resolve still loads from its stored topology. Booleans and slice store topology only.
  `.gs` persistence reuses REQ-313's solid serialization unchanged; `kGsFormatVersion` bumps only
  in the increment that first actually writes a recipe, if any does.

  **(f) A disjoint Boolean result is a multi-shell solid when valid, else refused.** `SUBTRACT` can
  split one solid into two. ADR-045's `Solid` already carries *shells* (plural). A result with more
  than one shell that passes `brep::Validate` is stored as one multi-shell `CadSolid`; one that does
  not is refused. An **empty** result (`INTERSECT` of disjoint operands) stores nothing and is
  reported.

- **Rejected alternatives.**
  - **Mesh-based Booleans** (tessellate both operands, cut the triangle meshes, keep triangles).
    Far more robust on degenerate input and realistic to ship — but it makes the display mesh part
    of the model, which ADR-045 and #120 forbid in as many words, turns every Boolean result's
    faces flat and its volume approximate, and would need its own carve-out from ADR-045. The user
    weighed this explicitly and chose fidelity.
  - **A full general analytic Boolean in one step.** This is commercial-CAD-kernel work — years of
    specialist effort, and the degenerate cases are exactly where it breaks. Phasing by
    intersection-curve difficulty (c) lets a real Boolean ship and be trusted before the hardest
    geometry is attempted.
  - **A third-party kernel (OpenCascade, ACIS).** REQ-300 dependency discipline, and REQ-313 already
    committed to an in-tree kernel; bolting on a foreign B-rep now would mean two solid
    representations and a translation layer between them.

- **Open question — RESOLVED 2026-09-03 by ADR-048 (D-2026-09-03-b): freeform surfaces (was blocking
  REQ-315).** Sweep and loft produce surfaces that are none of ADR-045's five kinds. The resolution:
  a new `SurfaceKind::Nurbs` — a hand-rolled, in-tree, **minimal-subset** rational B-spline patch
  (degree ≤ 3, untrimmed, split at seams like the analytic curved faces), its volume and area
  integrated by the adaptive numerical quadrature D-2026-09-02-i already opened for the
  procedural-intersection face, `.gs` bumped to version 4. REQ-315 is unblocked; **loft ships before
  sweep**, each its own increment. Everything below in this ADR is unchanged. See ADR-048 for the
  full decision.

- **Consequences.**
  - `src/util/brep.{hpp,cpp}` grows a feature-operation section: `Extrude`, `Revolve`, `Slice`,
    `BooleanUnion` / `BooleanSubtract` / `BooleanIntersect`, plus internal face-split and
    point-classification helpers. It stays graphics-free and directly unit-tested, per ADR-045.
  - Increment **B2a** adds `Surface::inward` (a `bool`, ADR-045 (d) amendment) — no new curve type,
    no `.gs` version bump. Increment **B2b-1** adds `CurveKind::Ellipse` + `Edge::radius2` — closed
    form, and bumps `kGsFormatVersion` (ADR-045 (d) amendment). Increment **B2b-2** adds a procedural
    `CurveKind::Intersection` carrying its two surface references, and a tessellator for it — the
    quartic case; not built until then.
  - The command layer gains `EXTRUDE`, `REVOLVE`, `SLICE`, `UNION`, `SUBTRACT`, `INTERSECT`, each in
    the typed / prompted shape the primitive commands already use, each one undo step.
  - No renderer change — feature results tessellate through REQ-313's cached path and REQ-100
    profile (d) is unaffected.
  - DXF / DWG export is unchanged: ADR-045 (i) already excludes every `CadSolid` with a counted,
    named message.
  - **Still not addressed here** (sweep / loft moved to ADR-048, accepted 2026-09-03): multi-loop profiles; fillet / chamfer
    on a solid edge (#120 Phase 5); sectioning, centroid, moments of inertia (#120 Phase 6);
    interactive placement and 3D grips for a feature result (#120 Phase 5).

- **Delivery order (increments, each independently shippable and verifiable):**
  1. **Extrude** — straight, single-loop profile, no taper. (b) of REQ-314.
  2. **Revolve** — line and arc profiles, full and partial, plus the extrude taper option.
  3. **Slice** — by plane, one side or both.
  4. **Booleans Increment B1** — line/arc-intersection operand pairs only, others refused by name.
     Curved operands: UNION / INTERSECT only (D-2026-09-02-b); curved SUBTRACT deferred to B2.
  5. **Booleans Increment B2a** — inward-facing analytic faces (`Surface::inward`): curved SUBTRACT
     for the pairs B1 already recognises (round hole, blind pocket, spherical dimple, counterbore).
     No new curve type. (D-2026-09-02-c.)
  6. **Booleans Increment B2b-1** — `CurveKind::Ellipse` (closed-form): oblique plane ∩ cylinder, for
     SLICE then Boolean; plus a **Steinmetz coda** — perpendicular equal-radius cylinders meet along
     two ellipses (D-2026-09-02-i). Bumps `kGsFormatVersion`. (D-2026-09-02-h.)
  7. **Booleans Increment B2b-2** — procedural `CurveKind::Intersection`: cylinder ∩ cylinder
     (quartic), sphere ∩ cylinder, non-elliptical cone sections. A face bounded by one is integrated
     numerically (ADR-045 (b) amendment, D-2026-09-02-i). Refusals lifted pair by pair.
  8. *(separate REQ-315, ADR-048 — accepted 2026-09-03)* — `SurfaceKind::Nurbs` freeform surface,
     then **loft**, then **sweep**.
  9. *(REQ-319, amendment (i) below — accepted 2026-09-04)* — **push/pull a planar face**, the first
     operation that edits an existing solid rather than building one.

- **Amendment (i) — a MODIFYING operation, and a precondition `Validate` cannot enforce**
  (2026-09-04, D-2026-09-04-c, REQ-319, GitHub issue #148 Phase 5).

  Every operation this ADR planned *builds*: from a profile (extrude, revolve, loft, sweep), from two
  solids (the Booleans), or by cutting one (slice). Phase 5's direct modelling needs the other kind —
  take a solid, change part of it, return a solid. `brep::PushPullFace` is the first, and the shape
  it establishes is:

  **A modifying operation copies, edits the copy, and validates before returning.** Not an in-place
  mutation. `CadSolidPtr` is `shared_ptr<const brep::Solid>` precisely so that undo snapshots are a
  refcount bump (architecture §11.5), and a solid is *replaced*, never edited. Decision (d)'s
  compute-validate-replace therefore applies unchanged; only the input differs.

  **The new part is a geometric precondition that `Validate` does not cover, and the case proving
  it was measured rather than argued.** Moving a face's vertices along its normal leaves each
  neighbouring face's *surface* untouched — correct when the neighbour is a plane parallel to the
  push, wrong for anything else, because that neighbour's own vertices then leave its own surface.
  `Validate` checks topology and degeneracy: closed shells, edges used twice with consistent
  orientation, no degenerate face or edge, finite coordinates, positive volume. **It has no check
  that a face's vertices lie on that face's surface.**

  Removing the precondition and measuring:

  - a **cylinder's flat cap** pushed by 3 ft **builds**, `Validate` returns **Ok**, and the analytic
    volume comes out **863.938 against a true 1021.02** — 15% wrong — because the wall surface still
    reports `height = 10` while its top boundary sits at 13. A closed, manifold, positive-volume
    solid whose volume is a lie, and the case this decision rests on;
  - a **wedge's slanted plane** neighbour, by contrast, `Validate` *does* reject, at every distance
    from 0.001 ft to 2 ft. There the pre-check buys an accurate refusal rather than safety — without
    it the user is told "that push would turn the solid inside out", which is false — which matters
    under REQ-201 but is the smaller claim.

  **The first draft of this amendment asserted the wedge as the proof and was wrong.** It is
  recorded that way because the distinction is the whole content of the decision: some geometric
  breakage happens to trip a topological check, and some does not, and only measurement tells them
  apart.

  **Amendment (i), revised the same day — corners are RE-SOLVED, not translated.** The first
  implementation moved each corner of the pushed face ALONG the push. That is correct only where
  every neighbouring face contains the push direction, and measured against the shipped primitives
  it managed **box 6/6, wedge 2/5, pyramid 0/6** — a pyramid is entirely flat-faced and could not be
  pushed at all, which is what showed the algorithm was a special case wearing the name of a general
  one. Each corner is now recomputed as the point where the planes of the faces meeting there cross,
  which gives **box 6/6, wedge 5/5, pyramid 6/6** and identical answers on the box.

  Two user-visible consequences follow from the neighbours keeping their planes, and both are the
  correct behaviour rather than side effects: extending a wedge's end face makes the wedge TALLER
  (the ramp keeps its slope), and raising a pyramid frustum's top makes that top NARROWER (the walls
  keep theirs). A translation would have produced a wedge whose corners no longer touch its own
  slope, and a frustum whose walls bend.

  The refusal set changed with it. `PushPullNeighbourNotParallel` is gone — parallelism is no longer
  required — replaced by `PushPullNeighbourCurved` (a curved surface is not a plane to intersect)
  and `PushPullVertexUnsolvable` (the planes at a corner do not meet in one point, or more than
  three faces meet there and moving one would split it). A true pyramid's apex is the second case:
  four planes, and pushing a side face would break it into several points — a topology change, and a
  different operation. Its base still pushes.

  **Amendment (i), extended — a curved neighbour is RE-PARAMETERISED, not refused** (2026-09-04,
  D-2026-09-04-d). The user reported push/pull not working on cylinders or cones. A cap is a plane,
  so it always passed the face test; what refused it was the wall beside it. A curved surface cannot
  be intersected the way a plane can, but it CAN be re-parameterised, and that is a third kind of
  move this ADR did not have:

  - **translate** a plane (the face being pushed);
  - **re-solve** a corner as the meeting point of the planes around it (amendment (i) proper);
  - **re-parameterise** a curved wall: a cylinder's stored height, a cone's end radius.

  The taper choice was put to the user and is the substance of the decision: **a cone keeps its
  slope and lets the radius change**, so pushing its cap extends the same cone rather than bending
  its wall. The alternative - keep the radius, change the slope - produces a volume 13% different on
  the worked example, so it is a real fork rather than a rounding preference.

  This is also the case that measures worst when got wrong rather than refused, and it is the one
  the earlier amendment cited: leave the wall's height alone while its boundary moves and the push
  still builds and still passes `Validate`, reporting **863.938 against a true 1021.02**. The
  guarantee is now positive rather than defensive - the wall is made to match its boundary instead
  of the move being declined because it might not.

  Still refused, each by name: a curved wall pushed along its own normal (a radius change, not a
  translation), a sphere or torus (no height or taper to follow), an axis oblique to the push, and a
  cap whose corners also touch an unrelated plane (two constraints, a different solve).

  **Amendment (i), extended again — a cylinder WALL is a radius change** (2026-09-04,
  D-2026-09-04-e). The fourth kind of move, and the one that is least like a push: a wall's outward
  normal points a different way at every point of the surface, so there is no single direction to
  translate along. The gesture means every point moves along its OWN normal by the same amount,
  which is what adding to the radius does. So the operation's four moves are now:

  - **translate** a plane; **re-solve** a corner; **re-parameterise** a wall's height or taper;
    and **re-radius** a cylinder wall.

  `Surface::inward` decides the sign, and it is the detail worth writing down: a hole's wall has
  its outward normal pointing AT the axis, so pushing it outward makes the hole SMALLER and the
  solid heavier. Got wrong, the feature still appears to work — on a boss. The test builds its hole
  with a real `BooleanSubtract` rather than by setting the flag on a hand-made solid, because the
  flag alone is not the case: what has to hold is that this operation reads the same geometry the
  Booleans produce.

  A CONE wall stays refused, and the reason is a genuine second decision rather than effort:
  offsetting a cone along its own normal moves both radii by `d / cos(half-angle)` and leaves the
  apex where it was. That is an offset surface, not a radius change, and which of the two a drag
  should mean is exactly the kind of fork D-2026-09-04-d had to put to the user for the cap.

  Adding such a check to `Validate` was considered and rejected: for a Boolean result it is a
  tolerance question rather than a boolean one, and making every existing operation pay for it —
  and possibly newly fail on it — to guard one new operation is the wrong place to put the cost.

  So **a modifying operation owns its own preconditions**, checked before it builds anything, and
  refuses by name. This is the same conclusion #148's fillet planning reached independently about
  the over-radius case: `SelfIntersects` is documented as not general, and the refusal has to be a
  pre-check against adjacent face extents rather than an after-the-fact test. Two operations, one
  lesson: **the kernel's safety nets catch the topology, not the geometry, and a new operation that
  can break the geometry must bring its own net.**

### ADR-047 — Curved polyline segments: a per-vertex bulge array, arc-aware POLYLINE and JOIN   (2026-09-02, accepted)

- **Status:** accepted (2026-09-02, D-2026-09-02-e). Storage is a parallel per-vertex bulge array —
  corrected before any storage code from an initially-accepted stride 3→4 rename-widen (see the
  correction in (a)) — and the phased delivery were chosen by the user, who accepted this ADR text.
  Backs REQ-316. Paired with a new requirement because the feature request had no accepted
  `REQ-NNN` behind it. Increments 1 (storage + POLYLINE arc mode + render + DXF/`.gs`), 3 (JOIN of
  lines + arcs), and the pick/box-select/arc-grip work of increment 2 delivered 2026-09-02
  (D-2026-09-02-f keyword choice, D-2026-09-02-g hover aperture; TASK-180..183).

- **Context.** A feature request asks for two things that are one thing: (1) POLYLINE should switch
  between "line mode" and "arc mode" mid-command so a single polyline can contain straight *and*
  curved segments, and (2) JOIN should weld lines and arcs together into one such polyline. The
  obstacle is storage. A polyline is `userPolylineVerts` (stride-3 XYZ) + `userPolylineOffsets` +
  `userPolylineClosed` + `userPolylineAttrs`, and it holds **only corner points** — the renderer,
  snap engine, pick, extents, length, OFFSET, TRIM and both file writers all assume the piece
  between vertex *i* and *i+1* is a straight chord. The CAD term for "how much this segment bows"
  is a **bulge** (`tan(θ/4)`, θ = the arc's included angle; the DXF `LWPOLYLINE` group-42 value,
  one per vertex, 0 = straight). Nothing in the domain stores it:
  - `requirements.md` REQ-085 (3DPOLY) states in as many words that the ordinary POLYLINE command
    is "unchanged" and that adding curvature would be "a storage change" it is avoiding.
  - `io/DxfIo.cpp` **already parses** group-42 bulges and then discards them — it tessellates each
    arc into short straight chords on import because "the polyline store carries no per-vertex
    bulge." The exporter emits no bulges at all (group 72 `0` on HATCH boundaries).
  - The polyline arrays have ~**922 reference sites across 17 files** (490 in `CadCommands.cpp`
    alone, 29 in `DxfIo.cpp`); ADR-035's measured count for the comparable `userPolyline*`
    footprint was **612 sites across 11 files**.

  Choosing the storage layout and the file-format change is an architectural decision (§2, §5,
  §11.4, §11.8), not a Workshop choice — hence this ADR.

- **Decision.**

  **(a) A per-vertex bulge is a parallel array beside the vertex store** —
  `std::vector<float> userPolylineVertsBulge`, one entry per vertex, so
  `userPolylineVertsBulge.size() == userPolylineVerts.size() / 3` always. The vertex store keeps
  its stride-3 XYZ layout unchanged. The bulge at vertex *i* describes the segment **leaving**
  vertex *i*; the last vertex's bulge is consulted only when `userPolylineClosed` is set (the
  closing segment). All three copies of the store — live `AppCommandState`, the undo
  `DrawingGeometrySnapshot`, and the per-tab `DrawingDocument` — gain the parallel array, mirrored
  at the handful of vertex-mutation sites (append, erase-polyline, clear, undo restore, `.gs` load,
  DXF load). `docinvariants` gains `userPolylineVertsBulge.size() * 3 == userPolylineVerts.size()`
  and "every bulge is finite".

  *Correction (2026-09-02, before any storage code landed).* As first accepted, (a) chose to
  **widen the vertex stride 3→4 with a rename** (`userPolylineVerts` → `userPolylineVertsXyzB`) on
  the stated ground that "renaming makes every one of the ~600 access sites a compile error rather
  than a silently-misread stride." **That ground is false.** The store is `std::vector<float>`;
  widening it to `x,y,z,bulge` leaves it `std::vector<float>`, so a renamed site that still
  computes `verts[i*3 + 2]` compiles cleanly and reads the wrong float — the rename catches the
  *name*, not the *stride arithmetic*. With the rename-widen's only advantage over the parallel
  array gone, and 401 `userPolylineVerts` reference sites across 16 files to hand-audit with no
  compiler net, the parallel array is the lower-risk choice: existing XYZ code is untouched, only
  the ~12 arc-aware sites read the new array, and the ~6 vertex-mutation sites that must mirror it
  are enumerable and invariant-checked. This is the same call ADR-035 (c) made for feature-line
  per-vertex data. Recorded rather than silently rewritten, per the ADR-025 correction-note
  precedent.

  **(b) An all-zero-bulge polyline is bit-for-bit today's behaviour.** Every existing consumer that
  walks vertex pairs keeps working; it reads a 4th float it can ignore. Curvature-aware behaviour
  is added as an **`if (bulge != 0)` arc branch** at a fixed, enumerated set of sites — render,
  snap, pick, extents, length/area, OFFSET, TRIM, FILLET/CHAMFER, transform-preview, `docinvariants`,
  DXF/DWG/`.gs` IO. That enumeration is an **acceptance condition, not a review habit** (the
  ADR-035 (g) discipline): a missed site is a curve that silently renders or snaps as a chord.

  **(c) Arc geometry is derived, never stored.** The bulge→(centre, radius, start/end angle, sweep)
  math already inside `DxfIo.cpp`'s import path is promoted to a pure, unit-tested helper in
  `util/geom2d` (`BulgeArc(p0, p1, bulge) → ArcSpan`). It has ≥3 present-day uses — the DXF
  importer, the renderer's tessellation, and the snap engine — so it is a value helper, not a
  speculative abstraction (§11.4). The renderer tessellates each arc segment to a chord-height
  tolerance at draw time and feeds the **existing** line/GL path; **no new GL code, no new shader.**

  **(d) POLYLINE gains `ARC` / `LINE` sub-modes.** While drawing (`polylinePhase == NeedNextPoint`)
  the keywords `ARC` and `LINE` (full words — `A` / `ANGLE` are the existing segment-bearing lock,
  D-2026-09-02-f) toggle the mode carried on the polyline draft state. Arc mode's default is an arc
  **tangent to the previous segment**, its far end at the next pick; `RADIUS` and `CANGLE` (included
  angle) set the next arc segment. `CEnter`, `Second point`, `Direction` are deferred past increment
  1. `UNDO` removes the last segment. 3DPOLY stays line-only (arc + independent per-vertex Z is out
  of scope).

  **(e) JOIN becomes arc-aware.** The edge walk in `ExecuteJoinSelection` carries a bulge per edge.
  An `ARC` entity in the selection contributes one bulge segment; a bulge polyline contributes its
  per-segment bulges; a `CIRCLE` is refused (no endpoints — the existing pattern). Tangency between
  joined pieces is **not required** (AutoCAD JOIN does not require it). Non-contiguous selections
  continue to report which pieces were left out (REQ-201), and the whole operation stays one undo
  step.

  **(f) File formats.** DXF/DWG export emits group 42 per vertex; the importer **stops
  tessellating** and stores the parsed bulge directly (removing the straight-chord fallback).
  `.gs` gains an **additive** per-vertex bulge array read tolerantly with a default of 0 and **no
  `kGsFormatVersion` bump** (the ADR-020 (d) / ADR-030 precedent) — a legacy drawing loads with
  every polyline straight. `RECT` and contour/EXTRACT output write bulge 0 and are unaffected.

  **(g) Snapping** on arc segments covers endpoint, midpoint, nearest, **centre** and **quadrant**
  (the last two new for polylines), plus tangent/perpendicular where the engine already offers them
  for arcs.

  **(h) Grips:** an arc segment gets a midpoint grip that edits its bulge (AutoCAD behaviour).
  **Deferred to phase 3.**

- **Rejected alternatives.**
  - **Widening the vertex stride 3→4 with a rename** (the originally-accepted (a)) — rejected on the
    correction above: `std::vector<float>` stays `std::vector<float>` through the widening, so the
    rename does not turn stride arithmetic into compile errors, and 401 reference sites would be
    hand-audited with no compiler net.
  - **A distinct `PolyArc` / arc-polyline entity kind with its own store** — rejected: ADR-035 (g)
    measured this at ~600 sites across 11 files for exactly this store shape, and it still would
    not satisfy the request, which is lines **and** arcs in **one** entity. A second store makes
    JOIN's output ambiguous (which store does a mixed join land in?).
  - **"Faked" arcs — arc mode inserts many short straight segments** — rejected: fails REQ-316's
    tangent-arc, arc-centre-snap and DXF-bulge-round-trip criteria, bloats every file, and is not
    editable as a curve. It would be redone as this ADR.
  - **Bump `kGsFormatVersion`** — rejected: the strict version-equality check would reject older
    files; additive tolerant keys keep them loadable (the ADR-020 (e) reasoning).

- **Consequences.** The polyline vertex store and its two shadow copies gain a parallel
  `userPolylineVertsBulge` array, mirrored at the ~6 vertex-mutation sites; one pure `BulgeArc`
  helper enters `util/geom2d` with tests; ~12 consumer sites gain an `if (bulge != 0)` arc branch,
  and *every modify command names the arc case or deliberately refuses it* as an acceptance
  condition; POLYLINE grows `Arc`/`Line` sub-modes and arc-option parsing; JOIN's edge walk carries
  bulges; the DXF importer's tessellation fallback is removed and the exporter gains group 42;
  `.gs` gains an additive bulge array with **no version bump**; `docinvariants` gains
  `userPolylineVertsBulge.size() * 3 == userPolylineVerts.size()` and "every bulge is finite".
  **Blast radius acknowledged** — this touches
  the two big command files, the renderer, all of IO, snapping, picking, extents and the invariant
  checks — which is why it is split into four independently shippable increments, each passing
  Verification on its own:
  1. **Storage + POLYLINE arc mode + render + DXF/`.gs` round-trip.**
  2. **Snap + pick + extents + length/area + Properties** on bulge segments.
  3. **JOIN of lines + arcs**, and arc-segment grips.
  4. **TRIM / OFFSET / FILLET / CHAMFER** of bulge polylines.

- **Out of scope and not designed for:** spline / fit-curve / smoothed polylines; polyline segment
  width and taper (DXF group 40/41); variable global width; arc segments in 3DPOLY; DWG *write* of
  bulges beyond what ADR-041's R2004 writer already supports.

### ADR-048 — The kernel's freeform surface: a hand-rolled minimal NURBS patch, numerically integrated; REQ-315 delivers loft then sweep   (2026-09-03, accepted)

- **Status:** accepted (2026-09-03, D-2026-09-03-b). The representation (NURBS), its scope (a minimal
  subset), its implementation (hand-rolled, in-tree), its mass-property method (numerical quadrature)
  and the delivery order (loft before sweep) were each explained to the user in plain English and the
  recommendation accepted. This is the "separate REQ-315, separate ADR revision" ADR-046's delivery
  order named as item 8, and it resolves ADR-046's open question *"freeform surfaces (blocks
  REQ-315)"*. Backs **REQ-315**. GitHub issue #147, Phase 4 of #120. No code has landed under this ADR
  yet — the increments are filed separately, loft first.

- **Context.** REQ-315 (sweep and loft) has been accepted-but-blocked since 2026-09-02. ADR-045 (b)
  built the kernel on *analytic faces*: every face is a plane, cylinder, cone, sphere or torus, so
  volume and area are closed-form and do not drift when the display changes. ADR-046 established that
  extrude and revolve of a line-and-arc profile stay inside those five kinds, and delivered analytic
  Booleans in increments. But a **general swept or lofted surface is none of the five** — dragging a
  profile along a curved 3D path, or skinning between two dissimilar profiles, produces a smooth
  freeform sheet. ADR-046 deferred *how the kernel represents that* as an explicit open question,
  parked REQ-315, and said sweep / loft are not built until it is answered. This ADR answers it.

  D-2026-09-02-i already opened the one crack in ADR-045 (b)'s closed-form rule: a face bounded by a
  procedural intersection curve (Boolean increment B2b-2) is integrated by **adaptive numerical
  quadrature** to a tolerance far inside REQ-101's ±0.01 ft, because the cylinder∩cylinder quartic
  has no elementary integral. A NURBS face is the second citizen of that same carve-out.

- **Decision.**

  **(a) The freeform surface is a NURBS patch — `SurfaceKind::Nurbs`.** A new sixth `SurfaceKind`
  carries a **rational tensor-product B-spline patch**: degree `pu, pv` (each ≤ 3), knot vectors
  `Uu, Uv`, and an `(nu × nv)` grid of control points each with a weight (`std::vector` of
  `{ucs::Vec3 P; double w;}`, row-major). Its `frame` (an `ucs::Ucs`, per ADR-045 (h)) is the patch's
  local frame; control points are stored in that frame so a patch at survey-coordinate magnitude is
  built from small numbers (ADR-045 (g)). Evaluation is Cox–de Boor basis functions and the standard
  rational patch sum `S(u,v) = Σ Nᵢ(u)Nⱼ(v)wᵢⱼPᵢⱼ / Σ Nᵢ(u)Nⱼ(v)wᵢⱼ`; the first derivatives come from
  the same recurrence for the surface normal and the tessellation grid.

  **(b) Minimal subset — only what loft and sweep generate.** Degree is capped at 3 per direction;
  weights are non-unit **only** where a lofted or swept *circular arc* profile edge requires them
  (a quarter circle is exact as a rational quadratic). Patches are **untrimmed** — a face's boundary
  is its four patch edges, and the patch is split at any internal seam into faces that each bound
  normally, exactly as ADR-045 (d) splits a cylinder into two half-faces and a sphere into two.
  **Explicitly not built:** trimmed NURBS (a hole cut in the middle of a patch), degree > 3,
  surface–surface intersection *against* a NURBS face (so a NURBS solid is not yet a Boolean
  operand), and NURBS *curve* edges (`CurveKind` is unchanged — a loft/sweep between line-and-arc
  profiles has line, arc and ellipse edges only, and the profile-to-profile "rail" edges are lines
  or arcs of the profiles themselves). Each of these becomes its own decision if a later feature
  needs it.

  **(c) Hand-rolled, in-tree.** The basis functions, patch evaluation, derivatives, bounding box and
  adaptive tessellation are written in this repository — either extending `src/util/brep.cpp` or a
  sibling pure module `src/util/nurbs.{hpp,cpp}` beside it — with direct unit tests, no graphics.
  **No third-party NURBS or geometry library.** REQ-300, ADR-045's alternative (3) and ADR-046's
  rejected alternatives all already refused a foreign kernel; a NURBS *evaluator* (as opposed to a
  NURBS *modeller* with trimming and intersection) is a bounded, well-documented few hundred lines,
  and the project's dependency policy (project.md §7) answers "can this be done simply in-tree?" with
  yes.

  **(d) A NURBS face's volume and area are numerically integrated.** ADR-045 (b) as amended by
  D-2026-09-02-i already says a face bounded by a non-analytic curve falls back to adaptive
  quadrature; this ADR widens that clause to read *a face whose surface is `SurfaceKind::Nurbs`, or
  whose boundary loop contains a procedural intersection edge, is integrated by adaptive numerical
  quadrature* to a tolerance far inside REQ-101's ±0.01 ft. The divergence-theorem volume integrand
  (∫ x·n dA over the face, summed over the shell) is evaluated on a Gauss–Legendre grid refined
  until it converges; area is ∫ |Sᵤ × Sᵥ| du dv the same way. **The quadrature grid is independent
  of the display chord tolerance** — tessellation quality is still not part of the model (#120).
  ADR-045 (e)'s two-reference-point closure check still applies and still catches a lofted shell
  that does not actually close.

  **(e) `.gs` gains a `SurfaceKind::Nurbs` encoding and bumps `kGsFormatVersion` 3 → 4.** The patch
  serializes its degrees, knot vectors and weighted control net as additive JSON keys under the
  surface object. This is a geometry kind an older reader cannot tolerate (it would not know the
  face's shape at all), so — as with the B2b-1 ellipse bump (2 → 3) and the B2b-2 procedural-curve
  bump (already 2 → 3; this is the next integer) — `kGsFormatVersion` goes to **4**. A drawing with
  no NURBS face still serializes byte-identically to a version-3 build. A malformed patch (knot
  vector not non-decreasing, control count disagreeing with knots and degree, non-finite weight) is
  refused on load with the kernel's own reason (REQ-201), not clamped.

  **(f) Loft is delivered before sweep.**
  - **Loft (increment 1)** — a closed solid skinned between **two or more coplanar-or-not planar
    profiles**, each a closed loop of line / arc / ellipse edges with the **same edge count**
    (matched in order; a divided-profile / point-cap loft is out of scope for increment 1). Each
    corresponding pair of profile edges spans one NURBS patch (a ruled patch for a straight span, a
    rational patch where a profile edge is an arc); the profiles themselves cap the ends as planar
    faces. Volume is checked against hand-computed prism / frustum / barrel values within REQ-101.
  - **Sweep (increment 2)** — a single closed planar profile run along an arbitrary 3D path (a line,
    an arc, or a bulge polyline), the profile's orientation carried by a **rotation-minimizing frame**
    (double-reflection method) with an **optional constant twist** and an optional "keep profile
    normal to path" vs. "keep profile vertical" choice. A straight path is the existing extrude
    (asserted to agree); a circular-arc path with the profile in the plane of the arc is a torus /
    revolve (asserted to agree where analytic); every other path produces NURBS side faces.
  - Each increment is its own `workshop/tasks/` entry and its own PR, exactly as REQ-314's seven
    increments were.

  **(g) A loft / sweep result stores topology only.** Like the Booleans (ADR-046 (e)), a feature
  result carries no recipe by default; it may optionally record `{profile entity ids, path entity
  id, parameters}` for future parametric edit, never consulted by `Validate`, `ComputeMassProperties`
  or `Tessellate`. Operands (the source profiles / path) are consumed only after the result validates
  (ADR-046 (d)), as one undo step (REQ-314 acceptance, unchanged).

- **Rejected alternatives.**
  - **A tessellated freeform surface** (store the loft/sweep as a triangle mesh face). This is the
    fallback ADR-046's open question named. Rejected for the same three reasons ADR-045 alternative
    (2) rejected a faceted B-rep: the volume misses REQ-101 without an enormous facet count, the file
    grows by orders of magnitude, and tessellation quality becomes part of the model — which #120
    forbids in as many words. The user weighed this and chose NURBS.
  - **A full general NURBS modeller now** (arbitrary degree, trimmed patches, NURBS–NURBS
    intersection so a lofted solid is a Boolean operand). Rejected as speculative: loft and sweep
    generate none of it, it multiplies the test surface, and CLAUDE.md §7 forbids an abstraction
    without two present uses. Added incrementally if a real feature needs it.
  - **Vendor a NURBS library** (OpenNURBS, tinynurbs, …). Rejected: REQ-300 dependency discipline and
    the standing in-tree-kernel commitment (ADR-045, ADR-046). An evaluator is small enough to own.
  - **Approximate loft with analytic faces** (fit a cone / cylinder frustum between each profile pair
    and refuse the rest). Rejected: it silently mis-reports a barrel or a twisted hull as a straight
    frustum, the REQ-201 failure the analytic-Boolean phasing exists to avoid, and it does not
    generalise to sweep at all.

- **Consequences.**
  - `SurfaceKind` gains `Nurbs`; `Surface` gains the patch payload (degrees, knot vectors, weighted
    control net) — additive, defaulted, never set by the seven primitives or by extrude / revolve /
    slice / Boolean.
  - `src/util/brep.cpp` (or a new `src/util/nurbs.*`) gains: Cox–de Boor basis, rational patch
    evaluation + first derivatives, adaptive tessellation, an adaptive Gauss–Legendre area / volume
    quadrature, and a patch validator. All graphics-free, all directly unit-tested (ADR-045).
  - `brep::ComputeMassProperties` routes a `Nurbs` face (and, already, a procedural-intersection
    face) through the quadrature path; every analytic face keeps its exact closed form.
  - The command layer gains `LOFT` then `SWEEP`, each in the typed / prompted shape the primitive and
    REQ-314 commands use, each one undo step.
  - `io/GsMigrate.hpp` `kGsFormatVersion` → **4**; `.gs` reader/writer gain the `Nurbs` surface
    encoding; CI's format-version check updates.
  - **No renderer change of substance** — a NURBS face tessellates through REQ-313's cached path and
    the existing GL; isolines on a NURBS face are iso-parameter curves from the same evaluator
    (REQ-313 isoline precedent). REQ-100 profile (d) is measured on a scene with loft/sweep solids
    and must still hold.
  - DXF / DWG export is unchanged — ADR-045 (i) already excludes every `CadSolid` with a named,
    counted message.
  - **Still not addressed:** trimmed NURBS; a NURBS solid as a Boolean operand; multi-loop / divided
    profiles; point-capped loft; fillet / chamfer / section / moments (#120 Phases 5–6); interactive
    3D placement and grips for a loft / sweep result (#120 Phase 5).

- **Delivery order:**
  1. **Loft** — `SurfaceKind::Nurbs`, the evaluator, the quadrature, `.gs` v4, and `LOFT` between
     two-or-more equal-edge-count planar profiles.
  2. **Sweep** — a profile along a line / arc / bulge-polyline path with a rotation-minimizing frame
     and optional twist; agreement asserted against extrude and revolve where the path is analytic.
### ADR-050 — POLYSOLID: offset-and-mitre in the kernel   (2026-09-03, accepted)
- Context: REQ-317 asks for a wall swept along a picked path. ADR-045 settled how a *primitive* is
  built — a formula, a frame, a closed shell — and ADR-046 settled the feature operations that cut
  and combine finished solids. A polysolid is neither: it is the first builder whose output topology
  depends on the length and shape of its **input path** rather than on a fixed template or on two
  operands. What has to be decided before any of it is written is where the corner geometry lives.
- Decision:
  (a) **The sweep lives in the kernel as `brep::MakePolysolid`, alongside the seven `MakeX`
  builders, and takes a path rather than an entity.** Its input is a frame, a list of straight and
  arc segments in that frame's plane, a closed flag, a width, a height and a justification — plain
  geometry, no document, no `CadPolyline`, no entity index. The `O`bject option is therefore a
  **command-layer conversion** that reads an entity and produces that path, which is what lets a
  clicked path and a converted Line reach exactly one builder. It is the same argument ADR-045 (b)
  made for the UCS supplying orientation: the kernel gets geometry, and the command layer translates.
  (b) **Corners are MITRED, by offsetting the path to each side and intersecting adjacent offsets.**
  The tempting alternative — one box per straight run, one cylinder patch per arc — is far easier
  and is wrong three ways at once: the runs **overlap** at every bend, so the volume double-counts
  every corner; the drawing holds N objects where the user drew one, so a single MOVE or ERASE
  cannot address the wall; and the overlap is invisible in the shaded view, which makes it exactly
  the silent wrong answer REQ-201 exists to prevent. Line/line intersects two offset lines, line/arc
  a line and a circle, arc/arc two circles — three cases, all closed-form, none iterative. A
  **smooth** join is taken directly rather than solved for, because its two offsets are tangent there
  and the intersection is a double root; every arc the command draws is tangent to the run before it,
  so that is the common path and not the exception.
  (c) **A corner that cannot be mitred is REFUSED by name, never approximated.** Three shapes have
  no wall: a bend so sharp that the inner offset runs back past its own segment, a segment shorter
  than the mitre its neighbours demand, and an arc whose inner offset radius reaches zero — the wall
  turning inside out around the curve. Each gets its own `Problem` and creates nothing. A **path that
  crosses its own run** is refused too, and the asymmetry with ADR-045 (f)'s self-intersecting torus
  is deliberate: a torus that passes through itself is a shape people draw on purpose, so it is built
  and only its mass properties are withheld, where a wall crossing its own run is an authoring
  mistake. That check is exact for straight-segment paths, where a rail is a polygon, and is
  deliberately **not applied** when the path contains an arc: testing a curve by its chords would
  refuse walls that are perfectly fine, and a false refusal is strictly worse than no check. The
  general case is the same Phase 4 self-intersection test ADR-045 already defers.
  (d) **A curved run produces a CYLINDER patch, not a torus, and no new surface kind is added.**
  Extruding a planar arc perpendicular to its own plane sweeps a cylinder; a torus would arise only
  if a round *profile* were swept along a curve, which is not what a polysolid does. Recorded because
  the opposite was assumed out loud while scoping this, and it is the difference between reusing a
  surface the kernel already integrates in closed form and deriving a new one. It is also why
  REQ-317 is not blocked behind REQ-315's freeform-surface question.
  (e) **`Surface::inward` is REUSED, not reinvented.** A curved wall's inner face has its material on
  the far side from its own axis — the same situation as the wall of a bore, which REQ-314 B2a
  already added that flag for (D-2026-09-02-c). This work was first written with a `sense` field of
  its own and that duplicate was removed on discovering the existing one: two flags meaning the same
  thing is the disagreement ADR-045's original "no reversed flag" rule was trying to prevent, and it
  would have been that rule's failure mode rather than its absence.
  (f) **The tessellator is NOT touched.** A wall's cap is non-convex the moment its path bends, and
  annular when the path closes on a circle — and REQ-314 had already taught the plane branch both:
  a convex ring is fanned from its centroid, a non-convex one is ear-clipped, and a two-loop face is
  stripped by angle about the hole. This is the first caller to reach those from a **swept** solid
  rather than a sliced or booleaned one, so the pairing is pinned by a test instead of by new code.
  (g) **The recipe carries the PATH — the first recipe whose length is not fixed.** ADR-045 (f) keeps
  the topology as the stored truth and the recipe as description, and that split is what makes this
  safe: a variable-length recipe field cannot change any answer, because validity, mass properties
  and tessellation read the topology and never the recipe. Written additively to `.gs`, so a file
  with no polysolids is byte-identical.
  (h) **`PLINE`'s arc rule is reused: an arc segment is TANGENT to the segment before it and ends at
  the picked point.** That determines the arc uniquely from one pick, which is what makes it a
  gesture rather than a form to fill in. An arc asked for as the **first** segment has no incoming
  direction and is refused by name rather than defaulting to some direction the user did not choose.
  A converted POLYLINE brings its arc segments with it: REQ-316 gave the polyline store per-vertex
  bulges, and `tan(theta/4)` converts to `PathSeg::sweep` as `4*atan(bulge)` — sign and all — so the
  two stores share the DXF convention rather than each having its own.
- Consequences: `brep` gains one builder, one `PrimitiveKind`, two recipe fields and the named
  refusals of (c); no new surface kind, no new curve kind, and no change to the tessellator, the
  integrals or the validity checks. `.gs` gains an optional recipe key and three settings keys
  (`polysolidWidth`, `polysolidHeight`, `polysolidJustify`, remembered between invocations the way
  AutoCAD's PSOLWIDTH and PSOLHEIGHT are); no `kGsFormatVersion` bump. **Still not addressed:**
  sweeping an arbitrary profile along an arbitrary 3D path (REQ-315, blocked on the freeform-surface
  question in ADR-046), and editing a placed polysolid's path — #120 Phase 5, alongside transforming
  any solid at all.
  Note that this project's `CadPolyline` store is **straight-only** — it carries no bulges — so the
  `O`bject option's curved paths come from `Arc` and `Circle` entities, not from polylines.

### ADR-049 — Sub-object picking: one shared pick, an expiring reference, and the projection as part of the answer   (2026-09-03, accepted)

- **Context.** REQ-318 needs the system to name the face, edge or vertex under the cursor.

  **The starting point was not what issue #148 said it was, and the correction is the reason this
  ADR has the shape it does.** The issue's "existing foundation" section, and the first draft of this
  decision, both rested on PR #180's finding that `src/util/ray3d.hpp` had ray/plane and ray/segment
  but no ray/triangle — concluding that solid faces were unpickable. PR #180 was written
  2026-08-31, *before* REQ-313 landed on 2026-09-01, and it was quoted without being re-checked
  against `beta`. In fact object snapping has picked solid faces, edges and vertices since REQ-313:
  `CadSnap.cpp` already had a Möller–Trumbore test over the display cache's triangle buffer
  (`RayHitSolidFace`), the `triFace` lookup, the `ClosestPointOnSurface` projection, a ray/edge
  approach (`ClosestRayPointToEdge`) and a padded bounds reject (`RayNearBounds`).

  So the real problem was never "can we hit a triangle". It was that all of that was file-private,
  and a second caller could only re-implement it. Three things then had to be decided.

  **(a) One pick, shared — the snap path is refactored onto it rather than left alongside.** A
  second implementation is a second set of numerics, and the two drafted here already disagreed: the
  snap copy used an absolute determinant epsilon (`1e-12`) and exact barycentric bounds, while the
  shared test is scale-relative with a small outward barycentric slack. On the hairline crack
  between two faces of the tessellation — which is deliberately *unwelded*, because "a solid's edges
  are creases" — the snap copy falls through and reports nothing where the shared one reports a hit.
  A user would have seen the snap marker and the sub-object highlight name different things under
  one cursor, and #156's "UCS from the picked face" would have aligned to a face object snap said
  was not there. The scale-relative epsilon is also the one that survives survey coordinates: an
  absolute threshold rejects a legitimate 0.25 ft triangle at easting 2e6.

  `ray3d::RayTriangleIntersect` is therefore the only ray/triangle test, `solidpick::RayNearBounds`
  the only broad phase, and `CadSnap` calls both. The alternative — leave both and document the
  split — was rejected: CLAUDE.md §7 names duplicate architecture directly, and the divergence above
  is what it looks like in practice.

  **(b) A sub-object reference is an index PLUS the identity of the solid it came from, and it
  EXPIRES rather than re-binding.** This is the part that is genuinely new, since snapping returns a
  *point* and never has to remember what it hit. The only names the kernel offers are indices into
  `Solid::faces` / `::edges` / `::vertices`. Measured: two identical `Make*` calls produce
  byte-identical topology, and an index keeps its meaning across an edit that *preserves* the
  topology — a box's face indices survive a height change, a length change and a frame translation,
  compared face-by-face on surface kind, outward normal and the `inward` flag. It loses its meaning
  across anything that changes the counts: a cone frustum collapsing to an apex goes 4 faces → 3, and
  every boolean rebuilds wholesale.

  The reference therefore pairs the index with a `weak_ptr<const brep::Solid>`, following the
  precedent `CadSolidTessellation` already sets for its cache key — "a raw key could be matched by a
  NEW solid allocated at the freed address", and the cache would then draw the wrong shape while
  looking plausible. A topology-changing edit lets the reference expire. A persistent per-sub-object
  id minted at construction was considered and **deferred, not dismissed**: it is what a mature
  kernel does and the only thing that survives a boolean, but it changes every builder and every
  operation in `brep.cpp` for a benefit Phase 5 does not need. If a later phase needs a selection to
  survive a boolean, that is the decision to revisit; this one does not preclude it.

  **(c) The projection is part of the answer — and it fixes one error, not both.** The triangle
  locates the sub-object; `ClosestPointOnSurface` (faces) and `ClosestPointOnEdge` (edges, clamped to
  the edge's own extent, so an arc answer is on the circle) place the point. At the shipping chord
  tolerance a raw triangle hit sits **0.00986 ft** off a cylinder's true surface — *inside* REQ-101's
  ±0.01 ft, which is exactly the trap, while spending 98.6% of the budget before any other error
  joins in.

  The sharper half of this decision, and the one an earlier draft got wrong: **the projection
  removes distance-from-surface error and does nothing whatever for position-along-surface error.**
  A test that asserts only the picked radius on a cylinder therefore cannot fail — the projection
  rescales *any* nearby point to exactly `r` — so the acceptance asserts the picked **azimuth** as
  well. The same distinction sets the module's precondition: the display buffers are `float`, which
  is adequate only because storage coordinates are document-local and so stay at model magnitude
  (REQ-101's whole reason for being local). Fed triangles at absolute state-plane magnitude they
  quantize to 0.125 ft, and at oblique incidence that displaces the answer *along* the surface by
  `d·tan(angle from normal)` where the projection cannot see it. Both cases are pinned in the tests
  with identical ray geometry, one passing and one failing.

  **(d) Precedence is vertex → edge → face, bounded by occlusion measured against the nearest
  TRIANGLE.** Every vertex lies on an edge and every edge on a face, so a pure nearest-hit rule makes
  a vertex unpickable — the same argument object snapping already makes for preferring an endpoint
  to a nearest-point. Each kind has its own screen-derived tolerance; zero means "do not offer this
  kind". A candidate more than its own tolerance behind the front surface is refused, or a click on a
  near face selects the back silhouette. The baseline is the nearest *triangle* and not the nearest
  usable *face*: a triangle whose face id is out of range still proves a front surface is there, and
  using the next valid face's depth would put the baseline on the far side of the solid.

  **(e) The pick normalizes the ray it is given.** `RayTriangleIntersect`'s parameter scales as
  `1/|dir|` while `ray3d::RayPointDistance`'s scales as `|dir|`, so on a non-unit ray a face depth
  and a vertex depth are a factor of `|dir|²` apart and the occlusion test in (d) is meaningless
  rather than merely imprecise. A caller that builds its ray by unprojecting a near and a far point —
  `dir = far - near`, the natural construction — would get no vertex or edge pick at all. Normalizing
  on entry was chosen over documenting a precondition because the failure is silent.

  **(f) The pick is a pure module in the Domain layer, in the solid's own coordinates.**
  `src/util/solidpick.{hpp,cpp}` depends on `brep` and `ray3d` and nothing else — no GL, no ImGui, no
  `AppCommandState` — so precedence, occlusion and every refusal are decided in the test target
  without a window (the ADR-002 pressure that already governs `brep`, `meshgeom`, `traverse` and
  `hatch`). Note this moves the shared pick *out* of `src/viewport`: snapping is one consumer of the
  query, not its owner. Coordinates are the solid's own storage coordinates and the caller converts
  the ray; taking a world ray plus an origin offset would put the local/world seam inside a geometry
  module, the class of error REQ-101's document-origin rebase exists to prevent.

- **Consequences.**
  - `CadSnap`'s solid pick changes behaviour slightly and deliberately: it now reports a hit on a
    face/face boundary where it previously fell through, and it accepts small triangles at survey
    magnitude that its absolute epsilon rejected. Both are fixes, and both are why the change is
    recorded rather than done quietly.
  - Issue **#156** becomes a thin consumer of the face answer, which is what PR #180 chose when it
    deferred #156 onto #148.
  - The pick sees one solid at a time, so occlusion between *different* solids is the caller's to
    resolve by depth-ordering the per-solid answers; `Pick::rayT` is a distance for that purpose.
  - A selection that must survive a boolean is not expressible. That is decision (b)'s accepted cost.
  - **A process consequence worth recording.** This decision was first drafted on a stale premise
    taken from a PR older than the code it described, and the error survived until review. The
    cheap defence is the one that was skipped: a claim about what the codebase does not have is a
    `grep` against `beta`, not a quotation.

- **Out of scope and not designed for:** the selection *mode* and its store, the highlight treatment
  (including whether a sub-object highlight may be occluded — the selection overlay is deliberately
  never depth-tested, which is right for 2D linework and wrong for a solid's far-side face, and that
  wants a screenshot rather than an argument); grips; gizmos; fillet and chamfer; picking a
  sub-object of a mesh or a TIN surface, neither of which has face identity (ADR-026 (g), REQ-070).

### ADR-051 — ACIS 3D-solid import: SAT-only, analytic primitives that fit the existing rectangular face model, refuse otherwise   (2026-09-05, accepted)

- **Context.** Issue #299: vendor block-library `.dwg`/`.dxf` files (e.g. Plant 3D piping symbols) commonly
  hold their only geometry as an ACIS `3DSOLID` entity — LibreDWG decrypts the container but exposes only
  the raw ACIS record stream (`Dwg_Entity__3DSOLID::acis_data`), never tessellating it. Three questions
  needed a recorded answer before any code, per CLAUDE.md's SPEC GAP process:

  **(a) SAT vs. SAB.** `_3DSOLID_FIELDS::version` (DXF 70) distinguishes ACIS v1 text (SAT) from v2+
  binary (SAB) framing of the same record model. **Decision: SAT only for the first increment.** SAT's
  tokens are whitespace-delimited text, trivial to hand-parse; SAB uses typed binary tokens and would
  double the parser surface for the same topology-interpretation logic. Deferred to **#301**, which reuses
  whatever record-interpretation code this ADR's implementation produces.

  **(b) Surface/curve scope.** ACIS analytic surfaces (`plane-surface`, `cone-surface` — which also
  encodes a cylinder as the zero-half-angle case, `sphere-surface`, `torus-surface`) map directly onto
  GoSurvey's existing `brep::SurfaceKind` (`Plane`/`Cylinder`/`Cone`/`Sphere`/`Torus`, ADR-045). ACIS
  free-form surfaces (`spline-surface`) and derived surfaces (blends, sweeps, lofts not already reduced to
  an analytic type by the file writer) have no such direct mapping and would route through
  `SurfaceKind::Nurbs` (ADR-048), which needs its own knot/control-point/trim design. **Decision: analytic
  primitive surfaces only for the first increment.** Deferred to **#300**.

  **(b-1) Within "analytic primitives", plane/cylinder/cone ship first, and only their FULL-revolve
  form; sphere/torus, and a PARTIAL cylinder/cone revolve, are each a fast-follow of this same
  feature.** A full revolve (two full-circle rim edges, no seam) has a fixed `u` span — `[0, 2*pi)` —
  independent of the loop's actual geometry, so it needs no derivation beyond recognizing the shape.
  A partial revolve (a seam line, an arc, a seam line, an arc) is an equally recognizable loop shape,
  but its `u` span is NOT fixed — it has to come from the seam edges' actual angular position in the
  face's own frame, a materially different derivation this increment does not implement. **Review of
  this very change caught a first draft that recognized the partial shape but then defaulted its span
  to the full revolve's `[0, 2*pi)` regardless — a real defect (silently over-reporting area/volume on
  a partial cylindrical wall) that would have shipped invisibly, since `Validate` checks topology, not
  a face's span against its loop's actual geometric extent.** Landing the correct derivation needs its
  own fixture that actually exercises a non-full span, which this ADR's revision does not yet have;
  the honest fix is to ship only the shape this increment can prove correct (a hand-authored full
  cylinder fixture, ADR-051's own sanctioned approach) and refuse the partial shape by name, rather
  than land an unverified formula. A sphere's loop can pinch to a point at a pole (a degenerate
  "vertex" the loop's edges meet at rather than a fourth corner) and a torus has its own seam/pole
  combinations; both need a genuinely different loop-shape recognizer than the cylinder/cone one, not
  a parameter change to it. Rather than risk a hastily-generalized recognizer being subtly wrong in
  exactly the cases that are hardest to unit test by hand, **this increment ships plane and the
  full-revolve form of cylinder/cone only**; sphere, torus, and the partial-revolve span derivation are
  each refused by
  name ("recognized but not yet mapped") and tracked as the next increment of #299 itself, not a new
  issue — the parser, topology walk and refusal plumbing this ADR builds are unchanged by adding them.

  **(c) The face-boundary mismatch, and why it forced a second decision (issue #302).** A general ACIS
  face's boundary is an arbitrary closed loop of edges in parameter space. `brep::Face` (ADR-045) instead
  carries a **rectangular** parametric span (`uStart/uEnd/vStart/vEnd`) — every existing primitive and
  Boolean-result builder produces faces whose boundary is exactly one such rectangle, and `Validate`, mass
  properties and tessellation all assume it. Making the kernel accept a general trimmed loop is a real
  architectural extension (it touches every one of those consumers, for every face kind, not just the ones
  ACIS exercises) — far beyond an importer's scope, and requiring a REQ/ADR of its own. **Decision:
  #299 accepts only ACIS solids whose every face's boundary reduces to that rectangle** (checked by
  walking each face's loop, converting vertices to the surface's own parameters, and confirming the loop
  is exactly the surface's iso-parameter rectangle at those bounds, with at most a hole loop the kernel's
  existing "extra loop" convention already supports). Anything else — an irregularly trimmed face, a loop
  that doesn't close onto an axis-aligned parameter rectangle — is refused by name. General trimmed-face
  support is tracked separately as **#302** and is not a prerequisite for #299.

  **(d) Unsupported content: refuse, never approximate.** Matches the export precedent already recorded
  (D-2026-09-01-b, which refuses to tessellate a GoSurvey solid into DWG rather than write a lossy
  `3DSOLID`) and REQ-201 (no silent data loss). Every refusal names the entity handle and the specific
  record/face that could not be represented — an empty block definition with no message is exactly the
  failure issue #299 was filed to fix, and a silently-approximated one would be worse: a shape that reads
  as authoritative but disagrees with the source file.

- **Decision.**
  1. A new pure, dependency-free module, `src/util/AcisSatParser.{hpp,cpp}` (Domain layer, no LibreDWG/GL/
     ImGui dependency — same ADR-002 pressure as `brep` itself), tokenizes a SAT text stream into records
     keyed by their `$n` position, and resolves the body → lump → shell → face → loop → coedge → edge →
     vertex → point graph into a `brep::Solid` by direct translation (ACIS already gives explicit topology,
     so this is a graph translation, not a primitive-recognition problem) — checking (c)'s rectangle
     constraint per face and returning a per-face/per-entity `Problem`-style reason on the first mismatch.
  2. `src/io/LibreDwgCad.cpp`'s `ImportObject` gains a `DWG_TYPE__3DSOLID` case (alongside the existing
     flat dispatch chain) that refuses immediately on `version` indicating SAB, then hands `acis_data` to
     the new parser; a refusal reaches the same `NoteSkip` mechanism the dispatch's fallthrough already
     uses, so the entity is reported, not silently dropped.
  3. `CadBlockContent` (`src/util/cadblock.hpp`) gains a `solids` member alongside its existing `meshes`,
     and `CadBlocks.cpp`'s capture/load-back/`DrawingHasCaptureableGeometry` functions treat it the same
     way they already treat meshes — closing the round-trip gap issue #299 named for *any* solid-bearing
     block (including a native GoSurvey solid WBLOCK'd), not only an ACIS-derived one.

- **Consequences.**
  - A real-world file using free-form/blend surfaces, SAB encoding, or a non-rectangular trim (e.g. a
    filleted edge written back as an analytic-looking but irregularly-bounded face) imports nothing from
    that `3DSOLID` and reports why, rather than the silent empty result issue #299 was filed against.
  - #300, #301 and #302 each build on this ADR's module/plumbing rather than duplicating it.
  - `kGsFormatVersion` is unaffected: an imported solid becomes an ordinary `brep::Solid` with
    `PrimitiveKind::None` (ADR-045's "no recipe" case, already handled — a Boolean result has the same
    shape), so nothing about `.gs` serialization changes.

- **Out of scope and not designed for:** SAB (#301); spline/blend/swept surfaces (#300); general trimmed
  face boundaries (#302); DWG *export* of solids (unchanged, D-2026-09-01-b); ACIS `wire` bodies (curves
  with no faces) and `sheet` bodies (open shells) — this ADR covers solid (`is_solid` lump) bodies only,
  matching REQ-313's "the solid kernel" framing; a future issue would need to name wire/sheet import if
  wanted.

### ADR-052 — General trimmed-boundary faces: an additive parameter-space loop, not a rectangle replacement   (2026-09-05, accepted)

- **Context.** Issue #302 (split from #299/ADR-051 (c)): `brep::Face`'s boundary is today always the
  surface's own iso-parameter **rectangle** — `uStart/uEnd/vStart/vEnd` — and every consumer (`Validate`,
  mass-property quadrature, tessellation, sub-object picking) assumes it. ADR-051 (c) refused any ACIS
  face whose loop is not that rectangle rather than guess at a general representation under an
  importer's time pressure. Issue #305 asks for that representation decision on its own, design only, no
  kernel code. `brep::Face` already carries `loops` — an ordered ring of 3D `Edge`/`EdgeUse` records
  (`Line`/`Arc`/`Ellipse`/`Intersection`, ADR-045) — but today those edges are always constrained to trace
  exactly the iso-parameter rectangle; `uStart/uEnd/vStart/vEnd` is carried explicitly alongside them
  rather than derived from the loop, because for a rectangle the two must always agree and re-deriving
  the span from the loop every time it's needed would be strictly more work for the same answer. The open
  question is how to represent a loop that does **not** reduce to that rectangle.

  Three sub-decisions, matching the issue's checklist:

  **(a) Representation: additive field, not a replacement.** `Face` gains a new field —
  `std::vector<std::vector<curveintersect::Vec2>> paramLoops` — index-aligned with `loops` (one 2D
  polygon per 3D loop: `paramLoops[0]` for the outer boundary, further entries for holes, matching the
  existing "extra loop is a hole" convention). **Empty `paramLoops` (the default) means the face is
  unchanged: rectangle form, `uStart/uEnd/vStart/vEnd` is the authoritative boundary, exactly today's
  behavior, byte-identical validation.** A non-empty `paramLoops` marks the face as **general form**;
  `uStart/uEnd/vStart/vEnd` on a general-form face is retained anyway, but repurposed as a cheap
  axis-aligned bounding box of `paramLoops[0]` — an optional fast-reject before the real point-in-loop
  test in #307/#308/#309's implementations, not a second source of truth (a general-form face's bbox is
  computed from its polygon, never authored independently, so the two can't disagree). This was chosen
  over replacing the rectangle fields outright because every existing primitive and Boolean-result
  builder (REQ-313/314/315) already emits valid rectangle-form faces; forcing all of them to also
  populate a parameter polygon for a rectangle they already describe exactly would be churn with no
  behavioral change, for dozens of call sites, to satisfy a consumer (ACIS import) that doesn't need it.

  **(b) Rectangle stays the fast path.** No existing primitive or Boolean builder changes as part of this
  ADR or its own implementation issue (#306): they keep emitting rectangle-form faces, and `Validate`,
  mass properties, tessellation and picking keep using today's closed-form/rectangle-quadrature paths for
  them unchanged. General-form handling in #307–#309 is an **additive branch** keyed on `paramLoops`
  being non-empty, not a replacement of the existing code paths.

  **(c) What bounds a general loop, and why not reuse `Edge` curve kinds directly.** `paramLoops`'
  polygon is a **straight-line polyline in (u,v) space** — literally a sequence of `Vec2` points, closed
  by wrapping back to the first. This is deliberately the narrowest option the issue offers ("an
  arbitrary polyline of param-space points"), sized to what #310 (the first real consumer, ACIS import)
  actually needs: ACIS coedges resolve to line, circle/arc, or ellipse curves (never the procedural
  `Intersection` kind, which has no closed-form parameter-space projection and is not something an
  imported file can produce), and REQ-314's precedent for those already gives a way to sample a curved
  edge finely into (u,v) points at import time — the polyline is a **classification aid**, not the
  boundary's geometric record. **The true 3D boundary curve stays the existing `loops`/`Edge` data**
  (used for the shell's manifold "every edge twice" check, for rendering the actual edge, and as the
  parameterization the surface evaluator maps through) — `paramLoops` exists only so #307's quadrature,
  #308's tessellation clipping and #309's picking each have a fast, uniform "is this (u,v) inside the
  face" test without every one of them re-deriving a point-in-loop test from `Edge` curve kinds
  individually. A curved (`Arc`/`Ellipse`) boundary edge therefore contributes several polyline vertices
  to `paramLoops` rather than one; the sampling density needed to keep classification error under
  REQ-314's existing relative-1e-6 tolerance is an implementation detail for #306 (which builds the first
  populated `paramLoops`) to pick and test against a known shape, not fixed by this ADR. Because
  `paramLoops` never carries the authoritative geometry, an under-sampled polyline is a latent bug in a
  later increment's numbers, never a silent loss of the imported shape itself — the exact curve is still
  in `loops`.

  **(d) Scope handed to follow-ups, in order.** This ADR decides representation only; no kernel code
  changes here (issue #305's own "not in scope"). The accepted follow-up order is **#306** (add
  `paramLoops` to `Face`, extend `Validate` for closed/simple/correctly-wound/nested-holes general loops,
  decide `.gs` round-trip for a hand-built fixture) → **#307** (mass-property quadrature restricted to a
  general loop's interior) → **#308** (tessellation clipped to a general loop) → **#309** (point-in-loop
  sub-object picking) → **#310** (ACIS import stops refusing non-rectangular faces, wiring its coedge
  data into `paramLoops`/`loops` per (c)). Each is independently reviewable and each can ship without the
  ones after it still working (a `paramLoops`-bearing `Face` that #307 doesn't yet integrate over
  correctly is a bug in #307, not a reason to hold back #306).

  **(e) Interaction with `SurfaceKind::Nurbs` (ADR-048).** No special case needed. A `Nurbs` face already
  repurposes `uStart/uEnd/vStart/vEnd` as the patch's parameter rectangle rather than an angular span
  (ADR-048); `paramLoops` sits at the same level — a boundary within that surface's own (u,v) domain,
  analytic or freeform alike. A trimmed freeform patch (the fully-general case a real kernel supports,
  named in #302's own checklist) is therefore already representable as a `Nurbs` surface with a
  non-empty `paramLoops`, with no further design decision required here; whether any near-term issue
  actually builds one is separate from whether the representation supports it.

- **Decision.**
  1. `brep::Face` gains `std::vector<std::vector<curveintersect::Vec2>> paramLoops;` (default empty),
     index-aligned with `loops`, per (a).
  2. Empty `paramLoops` is the rectangle form and is byte-identical to today's behavior in every consumer;
     non-empty is the general form and is handled by an additive branch in each consumer, never a
     replacement of the rectangle-form path.
  3. `paramLoops` holds only straight-line (u,v) polygons (outer + hole), used exclusively for
     inside/outside classification; `loops`' existing `Edge` records remain the sole authoritative record
     of the 3D boundary curve.
  4. Implementation is sequenced #306 → #307 → #308 → #309 → #310, each its own issue/PR.

- **Consequences.**
  - No behavior change for any existing solid: every current primitive/Boolean builder leaves
    `paramLoops` empty, so `Validate`/mass-properties/tessellation/picking see only the rectangle form
    they already handle.
  - #306–#310 each have an unambiguous, narrow scope with a clear "done" test, per (d).
  - A face kind or trim shape ACIS (or any future importer) produces that cannot be reduced to a closed
    polyline in (u,v) — e.g. a loop that is not a simple polygon once sampled — is still refused by name
    at import time (REQ-201); `paramLoops` gives the importer a target to hit, not a guarantee every ACIS
    face can hit it.

- **Out of scope and not designed for:** any kernel code (issue #305 is design-only; #306 makes the first
  code change); a general (u,v) *curve* record for `paramLoops` beyond straight polylines (deferred
  indefinitely — no consumer needs it, per (c)); `Intersection`-kind boundary edges in a general loop
  (no imported or generated content produces one yet); serialization details for a general-form face
  (`.gs` field layout, version bump if any) — left to #306 to decide and record.
