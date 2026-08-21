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
- **Status:** **typed-storage half done, 2026-08-17** (TASK-067) — the document origin is established
  at entry so a typed coordinate narrows to `float` at local rather than world magnitude; measured
  ~1.5e-9 ft error where it was 0.025 ft. The reference-dataset half (an external dataset run through
  the pipeline end to end) is still outstanding — see `spec/requirements.md` REQ-101.

### M3 — Interactive performance
- **Goal:** Smooth editing and orbiting at target scene size
- **Delivers:** REQ-100
- **Done when:** the benchmark scene holds the frame budget on reference hardware
- **Status:** **all three profiles measured 2026-08-15** (TASK-052, TASK-053). On the RTX 5060:
  segments **1.38 ms**, shaded meshes **1.97 ms** at 2M triangles, surface **10.28 ms** — all against
  16 ms. Run with `BENCH` / `BENCH SURFACE` / `BENCH MESH`.
  **Milestone complete**, with the device question settled the same day it was found: until
  2026-08-15 every figure had been measured on the machine's *integrated* GPU rather than the
  RTX 5060 the spec names, because the application never asked for the discrete part. BUG-013
  (TASK-054) fixed that, and the budget is judged on the RTX 5060 with the integrated figures kept
  as a documented floor — on which the mesh profile fails at 21.40 ms.
  The 2026-08-12 figure (8.93 ms) was a clang build on the integrated GPU and is superseded twice.

> Add milestones until the in-scope list in `project.md` is covered. Stop there.

---

## Now / Next / Later

A lightweight board that complements the milestones. Keep each column honest.

### Now (in flight — keep short)
- **M-Surfaces steps 6 + 7 — REQ-068 (selection), REQ-070, REQ-072.** Resequenced by
  **D-2026-08-21-a**: the user asked for surface styles and a selectable surface together, so step 7
  (REQ-072 banding + arrows) is pulled forward beside step 6 and **REQ-071 (contour EXTRACT) is
  pushed back** to its own task. Three tasks, in this order — each blocked on the one before it:
  - **TASK-084 — REQ-068's selection half**, never implemented. It also gives surfaces the stable
    entity id (`EntityKind::Surface`) that everything below depends on.
  - **TASK-085 — REQ-070 surface styles**: the style table, `util/contourgen`, the display-geometry
    cache, the Surface Style dialog.
  - **TASK-086 — REQ-072 analysis**: elevation/slope banding, slope arrows, the legend.
  Step 5 (REQ-069) is done (see M-Surfaces status below). ADR-036 records the shape; it **amends
  ADR-028 (h)** on the band-shading path.

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
- **Status:** **steps 1–5 done.**
  - Step 1 — REQ-076 / ADR-027 (TASK-044), **2026-08-12**: every entity carries a stable,
    never-reused id, cross-object references are by id, and the `labelMtextAnnIndex` fix-up sprawl
    is deleted. Legacy `.gs` label migration verified against a real legacy file.
  - Step 2 — REQ-066 + REQ-067 (TASK-045), **2026-08-15**: points carry a raw description, and
    named point groups resolve by rule (id ranges / description / raw description / picks, combined
    as a union) with a live member count in the manager.
  - Step 3 — REQ-068 (TASK-046), **2026-08-15**: point groups triangulate into a surface entity that
    stores, saves, selects and renders, with the triangulator written in-tree. Held open until its
    REQ-100 surface bench case was actually run — measured **p95 9.32 ms against 16 ms** at 100,000
    points / 199,966 triangles under continuous orbit (TASK-052), so the budget is claimed on its
    own profile rather than on the segment profile's behalf.
  - Step 4 — REQ-074 (TASK-055), **2026-08-15**: `SURFELEV` / `SE` reports interpolated elevation at
    a pick and grade/slope/horizontal/vertical distance between two, for every surface covering the
    point, by name. Verified against `samples/surface-demo.gs` in the running application as well as
    in unit tests (`TinQueryTests`). The hide-boundary half of the "outside surface" condition is
    unreachable until REQ-069 lands (recorded as ASSUMPTION-1 in TASK-055, not claimed).
  - Step 5 — REQ-069 (TASK-072), **2026-08-18**: breaklines and boundary rings (outer/hide/show)
    are part of a surface's definition, referenced by stable entity id and pruned when the reference
    no longer resolves; constrained-edge insertion is flip-based (Anglada/Sloan) in `util/tinbuild`,
    with crossing-elevation and duplicate-conflict diagnostics reported rather than absorbed. The
    surface rebuilds dynamically off the UI thread — `cadGpuRevision`, already bumped at every
    drawing mutation, doubles as the dirty signal and the async worker's generation check, so no new
    mark-dirty call sites were needed. `AppCommandState::SurfaceRebuildAsync` is the first complete
    implementation of architecture §8's one-shot-worker contract (generation staleness +
    cooperative cancellation). New commands `DESIGNATEBREAKLINE`/`DBL` and `DESIGNATEBOUNDARY`/`DBD`.
    Verified end to end in the running application against `samples/surface-demo.gs` (breakline
    forcing, boundary void with restore, dangling-id pruning on delete, `.gs` round-trip across a
    fresh process relaunch); full suite green at 405/405 cases, 203,846 assertions. One real bug
    found and fixed within this task (`CadUi.cpp`'s viewport-click dispatch chain was missing the
    two new command kinds); one pre-existing gap found and recorded against REQ-074 instead of
    fixed (`SURFELEV`'s own viewport-click wiring is likely similarly missing from that chain — not
    this task's to own). Constraint-insertion performance was not measured against REQ-100's
    100k-point surface budget — recorded as a limit, not assumed fine, since the rebuild runs off
    the UI thread regardless.
- **Deliberately out of scope:** grading design objects, contour smoothing (linear contours only),
  proximity / wall / non-destructive breaklines, surface import from Civil 3D, and DEM /
  point-cloud sources. See ADR-028.
  - **Feature lines moved out of this list 2026-08-19** (decision D-2026-08-19-a). ADR-028
    alternative (5) deferred them as "a separate milestone once surfaces are trustworthy" rather
    than declining them on merits; that milestone is **M-Grading** below. The rest of the list
    stands, including the other breakline types.

### M-Grading — Surface definition UI, 3D linework, and feature lines (incremental)
- **Goal:** make a surface's definition editable where a designer expects to find it, and give them
  the 3D linework grading is actually designed with.
- **Delivers:** REQ-075 (already accepted, never built), REQ-085 (3D polyline), REQ-086 (point file
  as a surface source), REQ-087 (feature line entity), REQ-088 (feature line elevation editing).
- **Sequence** — chosen by the user 2026-08-19; each step is independently shippable:
  1. **REQ-075 — Surface Manager panel.** An explorer tree: `Surfaces ▸ <surface> ▸ Definition ▸
     Point Groups / Breaklines / Boundaries / Point Files`, right-click ▸ Add… / Remove / Refresh,
     with **Add Boundaries** (name, type) and **Add Breaklines** (description, type) dialogs, plus
     reorder, the stale/rebuilding indicator, and the counts REQ-075 already asks for. **Needs no
     spec change** — REQ-075 has required this since 2026-08-12 and the current panel is explicitly
     a stub. Boundary types are the three the engine has (outer / show / hide); breakline types are
     Standard only. The Point Files node appears here but cannot act until step 2.
  2. **REQ-085 + REQ-086 — 3D polyline and linked point files.** `3DPOLY` is an entry mode, not a
     storage change: the polyline store is already stride-3 XYZ and `CadCommitElevation` already
     returns a snapped point's own Z (REQ-058). Point files make the step-1 node live.
  3. **REQ-087 + REQ-088 — feature lines and the elevation editor.** The largest step by far: a new
     entity kind with its own store, and a station / elevation / grade table. Needs its own ADR
     before implementation — see the open question below.
- **Open before step 3 starts:** an ADR for the feature-line entity. ADR-028's consequences
  paragraph is explicit that a new store grows a case in selection, extents, layer state, the undo
  snapshot, `.gs`, DXF export, render, snap, pick, grips and properties; the project's own note on
  3D entity work records that a missed site there is **silent in plan view**, not a compile error.
  The ADR must enumerate those sites and say how elevation points (which carry an elevation but are
  not plan vertices) are stored without breaking the flat-array stride invariant (architecture §11.8).
- **Status:** proposed 2026-08-19. Step 1 may proceed immediately under REQ-075; steps 2–3 wait on
  REQ-085…088 being accepted.

### M-Distribution — Automated releases and in-app updates (incremental)
- **Accepted 2026-08-15** (REQ-077, REQ-078, REQ-202, ADR-029). Runs in parallel with M-Surfaces:
  it touches the build, the installer and one new module, and shares no code with surfaces.
- The steps are sequenced so the pipeline is proven before anything depends on it:
  - Step 1 — TASK-048, the packaging refactor: one generated `Version.hpp`, the executable renamed
    to `GoSurvey.exe`, `installer/` un-ignored and its two per-version scripts collapsed into one
    parameterized `GoSurvey.iss`, and the `AppMutex` that lets Inno close a running instance.
  - Step 2 — TASK-049, the CI pipeline (REQ-202): build + test on every push, artifact-only on
    feature branches, a rolling `channel-beta` prerelease from the `beta` branch, and a
    version-gated stable release from `master`.
  - Step 3 — TASK-050, the updater (REQ-077 + REQ-078). Deliberately last: it consumes the manifest
    that step 2 produces, so by the time it is written its input already exists and is observable.
- **Deliberately out of scope:** delta/patch updates, rollback to a previous version, per-user
  (non-elevated) installation, staged rollouts, and any platform but Windows x64. See ADR-029.
- **Known debt on entry:** the installer is unsigned, so SmartScreen will warn on download and the
  SHA-256 proves integrity but not authenticity. Tracked as debt, not as a solved problem.

### Next (accepted, sequenced, not started)
- **REQ-071 — contour extraction** (EXTRACT bakes displayed contours into unlinked polylines). Moved
  out of "Now" by D-2026-08-21-a so a verification FAIL on styles cannot block it. `util/contourgen`
  (TASK-085) emits the same flat-verts + offsets layout `userPolylineVerts` uses, so the follow-up is
  small by construction rather than by hope.
- **Surface plotting** — `src/io/PdfPlot.cpp` handles no surfaces at all, so REQ-068's "a surface on
  a non-plottable layer is not plotted" is satisfied vacuously. Invisible today; the first thing a
  user hits once styled contours exist. **No requirement covers it** — recorded as TASK-085 DEBT-1
  and needing a REQ decision, not a quiet fix.
- Re-run the REQ-100 surface bench case per the roadmap's own sequencing note — and note ADR-036 (e):
  it must prove the display cache **holds across frames**, not merely that one regeneration is fast.
- REQ-101's reference-dataset half (M2) — the typed-storage half is done; see M2 status above.

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
| GL driver variance across target GPUs | perf budget unmet on some HW | **Mitigated 2026-08-12**: reference hardware is defined (`project.md` §7) and REQ-100 is measurable on demand via `BENCH`. **Rewritten 2026-08-15** (TASK-053): the risk turned out to be real and already happening, just not where this row was looking. The variance was not across users' GPUs but *within the reference machine* — the application had been rendering on its integrated GPU the whole time (BUG-013). On the RTX 5060 the headroom is very large (1M segments at 2.30 ms, 2M shaded triangles at 1.97 ms); on the integrated GPU the mesh profile fails outright. So the honest statement is: **GoSurvey's performance depends on which GPU it lands on, and today it does not choose.** **BUG-013 fixed the same day** (TASK-054): the application now requests the discrete GPU and a setting hands it back for battery life, so the 6-9x factor is chosen rather than suffered. Residual risk, now quantified rather than vague: on a machine with no discrete GPU the mesh profile misses the budget at 21.40 ms, which is REQ-100's documented floor |
| The installer is unsigned (ADR-029, D5) | SmartScreen warns on every download, and an auto-offered update is exactly the context where users are least equipped to judge that warning | Accepted for now. The pipeline carries a no-op signing step so a cert drops in without restructuring. Since the 2023 CA/Browser hardware-key rules this needs a cloud signing service (Azure Trusted Signing / SSL.com eSigner / DigiCert KeyLocker), not a `.pfx` in a CI secret — pricing and eligibility to be confirmed when pursued |
| The release pipeline runs on hardware we do not control | A GitHub runner image change (Inno Setup version, MSVC toolset, Windows SDK) can break releases without a commit touching the repo | Pin what can be pinned; the version gate means a broken pipeline fails loudly on master without publishing a bad release. Residual risk stands: REQ-200 reproducibility is now asserted against a moving runner image |
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
| 2026-08-15 | Added **M-Distribution** (REQ-077/078/202, ADR-029), running in parallel with M-Surfaces | User asked for automated releases and an auto-updater. It parallelises safely because it touches the build, the installer and one new module, sharing no code with surfaces — and it pays down an existing gap rather than adding scope: the installer script was gitignored and machine-specific, so REQ-200's reproducibility promise did not reach the artifact users actually received |
| 2026-08-18 | M-Surfaces step 5 (REQ-069, TASK-072) closed PASS; "Now"/"Next" moved to step 6 (REQ-070 + REQ-071) | Breaklines, boundaries, and dynamic rebuild shipped and verified end to end in the running application; the roadmap's own forced sequencing puts styles/contours next |
| 2026-08-21 | **Steps 6 and 7 merged and resequenced; REQ-071 pushed out; REQ-068's selection half pulled in** (D-2026-08-21-a, ADR-036). "Now" is TASK-084 → TASK-085 → TASK-086 | The user asked for surface styles and a selectable surface in one request. Reading the code first moved two things: REQ-068 already required selection and it was never built (so it is authority already held, and it supplies the stable id the style cache needs), and REQ-072's Analysis tab is part of what "surface styles" means to the user, so shipping step 6 without it would ship a dialog with a stubbed tab. REQ-071 moved out to keep the unit reviewable |
| `<…>` | `<Moved X from Later to Now>` | `<user need materialized>` |
