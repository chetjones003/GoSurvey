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
   compile error rather than a silent misread (ADR-025 (a)).
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
