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
| **Platform** | OS, windowing, file handles, GL context | Anything domain-specific |

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

- **Threading:** `<single-threaded UI + one worker pool for IO/compute>`
- **Ownership across threads:** `<data is moved to a worker, results moved back; no shared mutable state without a documented lock>`
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
  commands/     parsing, validation, execution
  renderer/     GL backend, buffers, shaders
  domain/       entities, invariants, compute
  io/           format readers/writers
  platform/     window, files, GL context
build/          all build artifacts (never in source tree)
spec/           this specification layer
```

## 11. Architectural invariants (the audit list)

A change is rejected if it breaks any of these:

1. No upward dependency across layers.
2. No subsystem doing another subsystem's job.
3. No new global mutable state.
4. No new abstraction without ≥2 present-day concrete uses.
5. Every resource has exactly one visible owner.
6. No `gl*` (or other backend) calls outside the Renderer/Platform boundary.
7. No allocation/logging/virtual dispatch added to a measured hot path without a
   profile justifying it.

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
