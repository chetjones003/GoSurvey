# Roadmap

> **Template.** The roadmap is where *future* work lives so it stays out of the
> codebase and out of the architecture until it is real. It sequences accepted
> requirements into shippable increments and records what is deliberately
> deferred. The roadmap describes *intent and order*; it is not a contract or a
> dated promise.

---

## How to use this document

- The roadmap pulls from `requirements.md`. A roadmap item should reference the
  `REQ-NNN`(s) it delivers. No requirement, no roadmap item — features are not
  invented here.
- Sequence by **value and dependency**, not by what is most fun to build.
- Keep "Now" small. A long in-flight list is a sign of unfinished work, not
  progress.
- When something moves from "Later" to "Now," that is a deliberate decision —
  record it in `project.md`'s decision log if it changes scope.

> **Anti-speculation rule.** Items in "Later" and "Someday" must not influence
> today's architecture. Build for the increment in front of you; let the design
> emerge from real requirements (see `project.md` §5).

---

## Milestones

State each milestone as an outcome a user would notice, with an explicit "done
when" and the requirements it closes.

### M1 — `<Walking skeleton>`
- **Goal:** `<End-to-end thinnest slice: import → display → export>`
- **Delivers:** REQ-001, REQ-200
- **Done when:** `<a real file imports, renders in the viewport, and exports back within tolerance>`
- **Status:** `<in progress / done>`

### M2 — `<Core domain correctness>`
- **Goal:** `<Computations match reference data>`
- **Delivers:** REQ-101
- **Done when:** `<the regression dataset passes at the stated tolerance>`
- **Status:** `<planned>`

### M3 — Interactive performance
- **Goal:** Smooth editing and orbiting at target scene size
- **Delivers:** REQ-100
- **Done when:** the benchmark scene holds the frame budget on reference hardware
- **Status:** **done 2026-08-12** — p95 8.93 ms against the 16 ms budget at 250,000 segments under
  continuous orbit, on the reference machine recorded in `project.md` §7. Run with `BENCH`.

> Add milestones until the in-scope list in `project.md` is covered. Stop there.

---

## Now / Next / Later

A lightweight board that complements the milestones. Keep each column honest.

### Now (in flight — keep short)
- **Paper Space — Increment 1** (REQ-025, REQ-026, REQ-031 partial): model/paper
  spaces, layout tabs, MODEL/PAPER status toggle, paper size + orientation + sheet
  outline, `.gs` persistence of layouts.

### M-PaperSpace — Paper space & plotting (incremental)
- **Goal:** compose the model onto sheets and plot them.
- **Delivers:** REQ-025–031 (ADR-006, ADR-007).
- **Increments:**
  1. ✅ Spaces + layout tabs + MODEL/PAPER toggle + paper size/orientation + outline + `.gs` (REQ-025, REQ-026, REQ-031 part).
  2. ✅ Viewports: create/move/resize, independent scale/center, model rendered inside (REQ-027, REQ-031).
  3a. Layout contextual ribbon + Rectangular viewport command w/ preview (REQ-032, REQ-033).
  3b. Viewports selectable; MOVE/COPY/DELETE operate on them (REQ-035).
  3c. Floating model space — double-click to edit model through a viewport (REQ-036).
  3d. ~~Polygonal viewport (REQ-034)~~ — **WITHDRAWN 2026-07-13** (unneeded complexity; was blocked on the GL clip pass). See decision log.
  3e. ✅ Per-viewport layer freeze (REQ-028) — freeze/thaw per viewport; on-screen + PDF plot honor it.
  4. ✅ Plot single + batch to vector PDF via PDFium (REQ-029, REQ-030) — per-layer "plottable"
     toggle + viewport-on-layer; geometry/borders on off/frozen/non-plottable layers excluded.
- **Deferred:** DXF persistence of layouts/viewports; direct-to-OS-printer; GL
  per-viewport transform/clip pass (perf + polygonal/MSPACE drawing); plot color/plot-styles and
  PDF-underlay content in plots — see ADR-006/007/008 debt.
- **Status:** Inc 1, 2, 3a, 3b, 3c, 3e, 4 done; 3d (polygonal, REQ-034) withdrawn. Milestone complete.

### M-Models — Imported 3D models
- **Goal:** open a real plant / structural model and see it shaded, like the reference screenshot.
- **Delivers:** REQ-063 (mesh entity), REQ-064 (visual styles), REQ-065 (glTF import) — ADR-026.
- **Sequence is forced by dependency**, and each step is independently shippable:
  1. **REQ-064 visual styles** first, on the geometry we already have. It is the only one with no
     new entity or parser behind it, it pays off immediately under orbit, and it puts the depth
     buffer and the triangle shader in place before anything needs them. Its parity gate
     (2D Wireframe pixel-identical) is also the cheapest to prove while there are no meshes.
  2. **REQ-063 mesh entity** — store, `.gs`, selection, extents, layer state, undo.
  3. **REQ-065 glTF import** — the parser lands last, when there is somewhere to put the result.
- **Done when:** a `.glb` exported from the source Plant 3D model opens in GoSurvey, shaded, with
  its parts distinguishable by colour, and REQ-100 still holds in Shaded.
- **Status:** **complete 2026-08-12** — REQ-064 (TASK-040), REQ-063 (TASK-041), REQ-065 (TASK-042) all delivered. A `.glb` opens shaded with its parts distinguishable by colour.
- **Deliberately out of scope:** snapping to meshes (ADR-026 (g)), textures, editing imported
  geometry, and decoding vendor custom objects (impossible — see ADR-026 Context).

### M-Surfaces — TIN surfaces for grading and drainage (incremental)
- **Goal:** turn survey shots into a terrain model you can contour, read grade off, and compute
  earthwork against.
- **Delivers:** REQ-066 (raw description), REQ-067 (point groups), REQ-068 (surface entity),
  REQ-069 (definition + dynamic rebuild), REQ-070 (styles), REQ-071 (contour extraction),
  REQ-072 (banding + slope arrows), REQ-073 (volumes), REQ-074 (spot/grade readout),
  REQ-075 (Surface Manager), REQ-076 (stable entity identity) — ADR-027, ADR-028.
- **Sequence is forced by dependency**, and each step is independently shippable:
  1. **REQ-076 stable entity identity** (ADR-027). A prerequisite, not a surface feature: without it
     a surface cannot safely reference a breakline. Ships alone and pays off immediately by deleting
     the `labelMtextAnnIndex` fix-up sprawl.
  2. **REQ-066 + REQ-067 — raw description and point groups.** No surface yet. Independently useful
     (a named, rule-based set of points), independently testable, and it needs nothing from step 1.
     Can run in parallel with it.
  3. **REQ-068 — the surface entity**, triangulated from point groups only. First visible surface.
     The REQ-100 surface bench case lands here.
  4. **REQ-074 — spot elevation and grade readout.** Small, and it is what you reach for constantly
     once a surface exists.
  5. **REQ-069 — breaklines, boundaries, dynamic rebuild.** Needs step 1. The hardest step:
     constrained triangulation plus the background-rebuild worker.
  6. **REQ-070 + REQ-071 — styles and contours**, then extraction. Re-run the REQ-100 surface case.
  7. **REQ-072 — elevation/slope banding and slope arrows.** Re-run REQ-100.
  8. **REQ-073 — cut/fill volumes.** Last, because it needs two trustworthy surfaces.
  9. **REQ-075 — Surface Manager.** Grows across steps 3–8; consolidated and finished here.
- **Done when:** a real topo's points build a contoured surface with breaklines honoured, slope
  arrows show where water goes, cut/fill against a proposed surface reports a hand-verifiable
  volume, and REQ-100 holds in the surface profile.
- **Status:** **steps 1–2 done.**
  - Step 1 — REQ-076 / ADR-027 (TASK-044), **2026-08-12**: every entity carries a stable,
    never-reused id, cross-object references are by id, and the `labelMtextAnnIndex` fix-up sprawl
    is deleted. Legacy `.gs` label migration verified against a real legacy file.
  - Step 2 — REQ-066 + REQ-067 (TASK-045), **2026-08-15**: points carry a raw description, and
    named point groups resolve by rule (id ranges / description / raw description / picks, combined
    as a union) with a live member count in the manager.
  - Step 3 — REQ-068, the surface entity itself, is next. It is the first step that puts a surface
    on screen, and it carries the REQ-100 surface bench case.
- **Deliberately out of scope:** grading design objects and feature lines, contour smoothing
  (linear contours only), proximity / wall / non-destructive breaklines, surface import from
  Civil 3D, and DEM / point-cloud sources. See ADR-028.

### Next (accepted, sequenced, not started)
- `<REQ-101 — coordinate tolerance regression>`
- `<REQ-100 — frame-budget benchmark>`

### Later (real but deferred)
- `<Second file format>` — deferred until a user actually needs it.
- `<Undo/redo system>` — design only once edit operations stabilize.

### Someday / maybe (explicitly speculative — do NOT design for these yet)
- `<Pluggable rendering backend (Vulkan)>` — single backend until a second is a
  genuine requirement; revisit then.
- `<Plugin/scripting API>`

---

## Dependencies and sequencing

> Make ordering forced by dependency explicit, so no one starts a downstream item
> prematurely.

```
M1 walking skeleton
   └─> M2 domain correctness        (needs the import/model path from M1)
          └─> M3 interactive perf   (only meaningful once correctness holds)
```

- `<Item B>` cannot start before `<Item A>` because `<reason>`.

---

## Risks and unknowns

> Name what could invalidate the plan. A roadmap that lists no risks is hiding
> them.

| Risk | Impact | Mitigation / spike |
|------|--------|--------------------|
| `<Reference data unavailable for tolerance test>` | blocks M2 verification | `<obtain dataset / build synthetic reference>` |
| GL driver variance across target GPUs | perf budget unmet on some HW | **Mitigated 2026-08-12**: reference hardware is defined (`project.md` §7) and REQ-100 is measurable on demand via `BENCH`. Residual risk stands — the budget is only known on that one machine, and it holds to 750k segments there, so a weaker GPU has 3–4× less headroom than the figure suggests |
| `<…>` | `<…>` | `<…>` |

---

## Explicitly out of roadmap

> Mirror `project.md` §4's out-of-scope list here so contributors see it while
> planning. These are not "Later" — they are decisions not to build.

- `<capability we are not building, and why>`

---

## Changelog of the plan

> The roadmap itself changes. Record significant re-sequencing so history is
> legible — what moved, when, and why.

| Date | Change | Reason |
|------|--------|--------|
| `<2026-06-10>` | `<Initial roadmap>` | `<—>` |
| `<…>` | `<Moved X from Later to Now>` | `<user need materialized>` |
