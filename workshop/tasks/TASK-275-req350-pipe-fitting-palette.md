# TASK-275 — Pipe fitting tool palette: size-matched library parts while routing

- Type:    feature
- Status:  self-verify (implementation complete; one by-eye check outstanding)
- Opened:  2026-09-24
- Owner:   Claude (Workshop), on the user's request

## 1. Authority

- Goal:         "Piping System" epic (GitHub issue #486), Track A5 (library browser grouping) /
                Track B7 (manual fitting placement on a run)
- Requirements: **REQ-350** (accepted 2026-09-24, D-2026-09-24-a). Reads REQ-345 (pipe runs,
                `PIPERUN`, `PIPEFIT`), REQ-107 (block INSERT + connection ports), REQ-313 / ADR-045
                (solid tessellation), **ADR-062** (the thumbnail render path), REQ-100 (frame
                budget), REQ-300 (no new dependency), REQ-201 (named refusals).
- Constraints:  `spec/project.md` §7 (MSVC/Windows, reference machine for any REQ-100 claim),
                dependency policy (nothing new), `spec/architecture.md` §11 invariants —
                specifically **§11.6** (no `gl*` outside Renderer/Platform), **§11.3** (no new
                global mutable state), **§11.4** (no abstraction without two present-day uses),
                **§2** (downward-only dependencies).
- Acceptance:   REQ-350's nine acceptance conditions, restated verbatim:
  - Starting `PIPERUN` opens the palette by itself, as a floating window that can be moved and
    resized, without blocking the command line.
  - With a `2in` run active, the **Flanges** tab lists `2IN_BLIND_FLANGE` and `2in_WELD_NECK_FLANGE`
    and does **not** list `CJ_4in_WELD_NECK_FLANGE`; routing a `4in` run instead lists the `4in` part
    and not the `2in` ones, re-filtered live without the palette being reopened.
  - Every listed row shows a shaded preview of that part with its connection ports marked, and the
    part's name beside it.
  - Picking a row and then clicking **on** the routed run inserts the part and splits the run exactly
    as `PIPEFIT` does, as **one** undo step.
  - Picking a row and then clicking **off** any run places the part as a block INSERT at that point.
  - The palette is still open after the run is committed or cancelled; its close box closes it; the
    reopen command shows it again with its position kept.
  - A category with no size-matching part shows a sentence naming the filtered size, not a blank pane.
  - A `Nozzle`-tagged part round-trips through a sidecar, a block definition and `.gs` without losing
    its type, and a drawing written before `Nozzle` existed still loads with every part type intact.
  - REQ-100 holds: the palette open with every category populated does not push the frame past the
    16 ms p95 budget on the reference machine, because each part's preview is rendered **once** and
    cached, not re-rendered per frame.
- Owning subsystem: **UI** (the palette window) + **Commands** (arming and placement) +
  **Renderer** (the thumbnail, and *only* the thumbnail). Each piece sits in the subsystem that owns
  its concern; nothing crosses.

## 2. Scope

**In scope**
- `CadPipePartType::Nozzle` — the new enum value, its tag/parse, and every switch/label site.
- Metadata sidecars for the three bundled fittings in `resources/blocks/fittings/`.
- A pure filtering/grouping layer over `CadBlockLibraryEntry`: category → part types, exact size
  match, class precedence, and the reason string for an empty category.
- The palette window itself, with right-hand vertical category tabs.
- `ViewportRenderer` thumbnail entry point + per-part texture cache (ADR-062), serviced from
  `main.cpp` after `RenderScene` the way `ServicePendingThumbnail` already is.
- Arming a part from a row, and the two placement paths (splice on a run / INSERT off a run).
- `PIPERUN` opening the palette; a command to reopen it.

**Out of scope (explicitly)**
- Any *other* size than the run's exact size — no reducer/branch size rules (REQ-350 (c)).
- Changing the INSERT dialog's own A5 library pane or its top-down wireframe preview. It keeps
  working exactly as it does; this palette does not replace it.
- An orbitable/zoomable preview, or a preview of anything that is not a B-rep solid (a 2D-only
  block lists with its name and no thumbnail).
- Auto-fitting behaviour at bends/branches (B5/B6), the `CadPipingSystem` container (B3), and run
  edit operations (B8). Untouched.
- A ribbon button for the palette. The reopen command is enough for this increment; the ribbon's
  "Pipe Network" group is its own layout work (REQ-345 B2 recorded the same exclusion for `PIPERUN`).

**Smallest change**: the palette is a *view* over machinery that already exists. Placement reuses
`Kind::InsertBlock` (already routed, already ghosted, already ESC-handled) with the scale/rotation
prompts switched off, plus one branch at its commit for the splice. The splice reuses the existing
`TrySplicePipeFit` body, refactored to take a **block name** instead of re-deriving one. No new
command Kind, no new placement rule, no second preview path.

## 3. Architectural boundary check

Does this need a NEW abstraction / layer / dependency / ownership change / global state /
public-API or data-format change / algorithm the spec didn't specify?

- [x] **Yes — and it was escalated and RESOLVED before planning.** Three items, all recorded:
  1. **Data-format change** — a new value in the serialized `CadPipePartType` enum → recorded as
     **D-2026-09-24-a (1)**. Additive: an older sidecar/`.gs`/block definition parses unchanged, and
     `ParseCadPipePartType` already returns `None` for anything it does not know.
  2. **New render path** — offscreen shaded thumbnails with GL resources and a cache → recorded as
     **ADR-062** (accepted, D-2026-09-24-a (4)).
  3. **New on-disk resource obligation** — metadata sidecars beside the bundled fittings → recorded
     as **D-2026-09-24-a (2)**.
- No new dependency (REQ-300): the thumbnail uses the existing GL path; filtering is in-tree.
- No new global mutable state (§11.3): the thumbnail cache is a member of `ViewportRenderer`, which
  already owns the FBO and shaders; palette state is fields on `AppCommandState` beside
  `blockAuthoringPaletteOpen`.
- No new abstraction (§11.4): nothing gains an interface, template or virtual. The new units are one
  concrete pure function set, one concrete renderer method, one concrete draw function.
- §11.6 held: **every** `gl*` call stays in `ViewportRenderer`. The palette receives an opaque
  texture id, exactly as `DrawDrawingViewport` already receives `ColorTexture()` (`main.cpp:1079`).
- §2 held: UI → Renderer and UI → Commands are downward edges. Nothing in `src/render/` learns about
  the palette; it is handed geometry and returns a texture.
- §11.9 held: the thumbnail cache is keyed by **definition name**, not by an index into `blockDefs`.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | What does clicking a palette row do — splice, free INSERT, both, or nothing yet? | 2026-09-24 | **Both, decided by where the next click lands** (on a run → splice; off → INSERT). REQ-350 (f). |
| Q2 | How does the palette learn a part's size/category, given no bundled part has a sidecar? | 2026-09-24 | **Author sidecars**; no filename inference anywhere. REQ-350 (h), D-2026-09-24-a (2). |
| Q3 | What are the tabs, given `CadPipePartType` has no `Nozzle`? | 2026-09-24 | **Add `Nozzle`**, five tabs. REQ-350 (b), D-2026-09-24-a (1). |
| Q4 | Preview fidelity — isometric wireframe, shaded 3D, or the existing top-down wireframe? | 2026-09-24 | **Shaded 3D thumbnail**, offscreen + cached. REQ-350 (e), ADR-062. |
| Q5 | Lifecycle — auto-close with the command, or stay open? | 2026-09-24 | **Stays open**; close box + reopen command. REQ-350 (a). |
| Q6 | Pressure class of the three bundled parts, for their sidecars? | 2026-09-24 | **Class-agnostic (omit the tag)** — the real class is not knowable from the files, and an invented `CS150` would wrongly hide them from a `CS300` run. REQ-350 (h). |
| Q7 | Does a `2in` run offer a `1.5in` reducer? | 2026-09-24 | **No — exact size only** this increment. REQ-350 (c). |

## 5. Assumptions

```
ASSUMPTION-1: A library part's solid geometry does not change during a session, so a thumbnail
              cached by definition name never goes stale except through BEDIT.
- Because:     ADR-062 (c) states this, but the BEDIT case is the one live exception.
- Risk if wrong: a part edited in BEDIT would show its pre-edit thumbnail until restart.
- Validate by: invalidating the cache entry by name when a block definition is saved
               (`BSAVE` / `BCLOSE` path), and asserting that in the palette test.
```

```
ASSUMPTION-2: A run is "under the click" when the pick is within the pipe's own outer radius plus
              the ordinary pick tolerance of the run's centreline.
- Because:     REQ-350 (f) says "on a pipe run" without defining the tolerance, and no existing
               pipe-run pick helper exists to inherit one from (grepped: only
               `NearestPointOnPipeRun`, which is unbounded).
- Risk if wrong: a click near but not on a run either splices unexpectedly or falls through to a
               free INSERT. Both are visible and recoverable by undo, neither is silent.
- Validate by: the on-run / off-run pair of tests, plus the nearest-run tie-break when two runs
               overlap the pick (nearest centreline distance wins).
```

```
ASSUMPTION-3: A part with no B-rep solid (a 2D-only block) lists with its name and no thumbnail.
- Because:     REQ-350 (e) describes the preview as "the part's own B-rep solid" and says nothing
               about a part that has none; ADR-062's consequences require graceful degradation for
               the headless case anyway, which is the same code path.
- Risk if wrong: a 2D fitting looks unfinished in the list.
- Validate by: the palette test's "no solid" row case; the row is still selectable and placeable.
```

## 6. Plan

**Approach.** Six small steps, each independently buildable and testable, in dependency order. The
pure logic lands first with its tests, so the UI is assembled on top of something already proven.

**Step 1 — `Nozzle` part type (domain).**
- `src/util/cadblock.hpp`: add `Nozzle` to `CadPipePartType`; `"nozzle"` in `CadPipePartTypeTag` and
  `ParseCadPipePartType`.
- Grep every switch/label over `CadPipePartType` and extend each (`CadUi_InsertBlock.cpp` filter
  combo, `BLOCKFITTING` prompt text, `PIPEFIT`'s usage line, any Properties label). A `switch`
  without a `default` that misses the new value is a compile warning we want, not one to suppress.
- No `.gs` version bump (additive tag, ADR-020 (d) precedent, as REQ-345's own fields did).

**Step 2 — Sidecars for the bundled fittings (resources).**
- `resources/blocks/fittings/2IN_BLIND_FLANGE.json`, `2in_WELD_NECK_FLANGE.json`,
  `CJ_4in_WELD_NECK_FLANGE.json` — each `{"partType": "flange", "nominalSize": "<2in|4in>"}`, no
  `pressureClass` key (Q6). Exactly the shape `CadBlocksCollectLibraryEntries` already reads
  (`CadBlocks.cpp:795-804`) and `LIBEXPORT` already writes (`CadBlocks.cpp:3615-3620`).
- Confirm CMake copies `resources/blocks/fittings/*.json` beside the exe (CON-07 / REQ-200); the
  `.sat`/`.dwg` files already are, but a glob limited to those extensions would silently drop the
  sidecars — check, don't assume.

**Step 3 — Pure filtering + grouping (commands layer, no GL, no ImGui).**
- New `CadPipePaletteCategory` enum (`Fittings`, `Flanges`, `Valves`, `Nozzles`, `Other`) plus
  `CadPipePaletteCategoryOf(CadPipePartType)` and a row-collection function in
  `CadBlocks.hpp`/`.cpp` beside `CadBlocksCollectLibraryEntries`:
  `CadPipePaletteCollectRows(entries, nominalSize, pressureClass, category, out)`.
- Size compare through `CadParsePipeNominalSizeInches` on both sides (so `2in` == `2 in` == `2.0in`),
  never string equality. Class precedence lifted from `CadPipeCatalogFind`'s recorded rule — exact
  class preferred, class-agnostic accepted only when no exact match exists — implemented by reading
  that function's logic, not by restating a second rule.
- `CadPipePaletteEmptyReason(category, nominalSize)` returns the sentence for an empty tab.

**Step 4 — Thumbnail render path (renderer only) — ADR-062.**
- `ViewportRenderer`: `bool EnsurePartThumbnail(std::string_view key, const CadSolidTessellation&, const std::vector<CadBlockConnection>&, int px)` and
  `unsigned int PartThumbnailTexture(std::string_view key) const`; a bounded `std::vector` cache of
  `{key, fbo, tex}` owned by the renderer and released in `DestroyFramebuffer`'s company.
- Renders with the existing `shadedProgram_` and `kShadedAmbient`, a fixed SW-isometric camera fitted
  to the tessellation's bounds (ADR-062 (e)), then the ports as small coloured quads in the role
  colours the BEDIT gizmo uses. No new shader.
- `main.cpp`: `ServicePendingPartThumbnails(cmd, activeRenderer)` called beside
  `ServicePendingThumbnail` (line 1576) — **after** `RenderScene`, which is the one point in the
  frame where binding another FBO cannot disturb the drawing's own image. The palette *requests* by
  name during UI draw; the service call renders at most a small number per frame so a big library
  cannot stall one frame. A row whose texture is not ready yet draws the name and a placeholder.

**Step 5 — The palette window (UI).**
- New `src/ui/CadUi_PipeFittingPalette.cpp` + a declaration in `CadUi.hpp`, modelled directly on
  `CadUi_BlockAuthoring.cpp`: `ImGui::Begin("PIPE FITTINGS", &open, ...)`, content on the left,
  `PaletteTabButton`-style vertical tabs on the right. Reuse `BeditVerticalText` /
  `PaletteTabButton` — they are `static` in `CadUi_BlockAuthoring.cpp` today, so promote the pair to
  a shared header only if the second use is real, which it is (two present-day call sites, §11.4).
- Row = `ImGui::Image(texture)` + name + size/class line, as an `ImGui::Selectable`.
- `DrawPipeFittingPalette(cmd, log, activeRenderer)` called at `main.cpp:1173`, beside
  `DrawBlockAuthoringPalettes`.

**Step 6 — Arming and placement (commands).**
- New `AppCommandState` fields beside `blockAuthoringPaletteOpen`: `pipeFittingPaletteOpen`,
  `pipeFittingPaletteTab`, and `insertBlockPipeSpliceArmed`.
- `StartPipeRunCommand` sets `pipeFittingPaletteOpen = true`. A new `PIPEPALETTE` command toggles it.
- Row click → `st.insertBlockName = row.name`; import the entry if needed
  (`CadBlocksImportLibraryEntry`, which refuses by name on failure — REQ-201); set
  `insertBlockSpecifyScale/Rot/AlignFace = false` so `InsertAdvanceAfterPoint` places on the single
  click (`CadBlocks.cpp:1262`); `insertBlockPhase = WaitInsertPoint`; `active = Kind::InsertBlock`;
  `insertBlockPipeSpliceArmed = true`. The ghost, the snapping, the ESC and the click routing all come
  from the existing INSERT command unchanged.
- `PipeRunUnderPick(st, pick, &runIndex)` — new helper beside `NearestPointOnPipeRun`: nearest run
  whose centreline distance is within its own outer radius plus pick tolerance (ASSUMPTION-2).
- Refactor `TrySplicePipeFit` (`CadCommands.cpp:34990`) into `TrySplicePipeFitNamed(st, runIdx,
  blockName, pick, log)` + the existing part-type wrapper that calls `CadPipeCatalogFind` then
  delegates. **This is the one change to existing behaviour, and it is a pure extraction** — the
  type-taking path keeps byte-identical semantics, including its ambiguity refusal. It is needed
  because the palette picked a *specific* part, and `CadPipeCatalogFind` refuses when two parts match
  (which is exactly the `2in` flange case: two candidates).
- In `SubmitInsertBlockPick`'s `WaitInsertPoint` branch: if `insertBlockPipeSpliceArmed` and
  `PipeRunUnderPick` hits, call `TrySplicePipeFitNamed` and finish; otherwise fall through to
  `InsertAdvanceAfterPoint` unchanged. One `PushUndoSnapshot` either way.

**Test approach**
- **Happy path** (`tests/CadPipePaletteTests.cpp`, new, pure — no GL, no ImGui):
  category→part-type grouping including `Nozzle`; a `2in` run lists both `2in` flanges and not the
  `4in`; `2in` / `2 in` / `2.0in` are one size; a class-agnostic part matches a `CS150` run; an
  exactly-classed part is preferred when both exist.
- **Failure / edge modes**: an unparsable or non-table size lists nothing rather than everything; a
  non-fitting library entry never appears in any category; an empty category's reason names the
  filtered size; a part with no solid still lists (ASSUMPTION-3); `Nozzle` round-trips through
  sidecar → `CadBlockDefinition` → `.gs` → back, and a `.gs` written with no `partType` still loads
  as `None`.
- **Placement** (`tests/CadPipeRunCommandTests.cpp`, extending the existing pipe-run command suite
  which already has `gosurvey_domain`): arming from a row then picking **on** a run splices and
  splits into two pieces in one undo step; arming then picking **off** every run places a
  `CadBlockRef` and leaves `cadPipeRuns` untouched; `ESC` while armed clears the arm and places
  nothing; the extracted `TrySplicePipeFitNamed` and the old part-type path produce the same result
  for an unambiguous part (regression pin on the extraction).
- **Not automated, handed to the user by eye**: that the thumbnail *looks* like the part, and that
  the tabs read correctly — `project.md`'s anti-requirements rule out framebuffer golden images, and
  GUI hover/appearance is not automatable here. A Debug build + `PIPERUN 2in` is the check.

**Steps**
- [ ] 1. `Nozzle` part type + every switch/label site
- [ ] 2. Three sidecars + confirm CMake copies `*.json`
- [ ] 3. Pure category/size/class filtering + `CadPipePaletteTests` (green before any UI)
- [ ] 4. `ViewportRenderer` thumbnail + cache + `main.cpp` service call (ADR-062)
- [ ] 5. The palette window, tabs, rows
- [ ] 6. Arming, `PipeRunUnderPick`, the `TrySplicePipeFitNamed` extraction, placement tests
- [ ] 7. Self-verify: `build-project`, `architecture-review`, `code-review`, `testing`,
      `performance-review` (REQ-100 claim measured with `BENCH`, not asserted)

## 7. Workflow-specific notes

- Branch: `feat/pipe-fitting-palette` off `beta`. PR into `beta`, never `master`.
- Spec changes landed **before** implementation, as CLAUDE.md requires: REQ-350 accepted,
  D-2026-09-24-a recorded, ADR-062 accepted, traceability row added.
- Memory note that applies directly: *"a command missing from `ViewportClickRouteFor` swallows every
  model-space click."* Placement reuses `Kind::InsertBlock`, which is **already** routed — this is a
  concrete reason to prefer reuse over a new Kind here, not only a smaller diff.
- Memory note that applies to Step 5: `RibbonNyiButton`/palette closures need care with capture and
  labels, and ribbon/palette changes want a **Debug** build to test.

## 8. Verification verdict (Step 2)

**APPROVE.** Findings that shaped the plan rather than blocking it:

1. **The ambiguity trap.** `CadPipeCatalogFind` refuses when more than one part matches
   `(type, size, class)`. The bundled library already has **two** `2in` flanges, so a palette that
   placed by *part type* would refuse on its own primary example. This is why Step 6 extracts a
   name-taking splice. Caught by reading the function's own contract (`CadBlocks.hpp:102-108`)
   against the library contents.
2. **`ImGui::Image` samples after UI runs.** A single shared thumbnail FBO would make every row show
   the last part rendered. ADR-062 (c)'s per-part texture is load-bearing, not a nicety.
3. **Thumbnails must be serviced after `RenderScene`.** Binding an FBO mid-UI would disturb the
   frame; `ServicePendingThumbnail` (`main.cpp:1576`) already establishes where this belongs.
4. **The sidecars may not be installed.** If CMake's resource copy globs by extension, the new
   `.json` files would work from the source tree and vanish in an installed build — a defect that
   only shows up after packaging. Step 2 checks it explicitly.
5. **REQ-100 must be measured, not claimed.** REQ-350's last acceptance condition is a performance
   claim; `implementation-rules.md` §5 and `project.md` §7 both forbid asserting one without the
   reference machine. Step 7 measures with `BENCH`.

No SPEC GAP remains: every architecturally significant choice is recorded (§3), and the two
behaviour rules a reasonable person could have wanted the other way (exact size, stay-open) are
stated in REQ-350 rather than left to implementation.

---

## 9. Completion report (Step 4)

- **Status:** implement → self-verify **complete**; awaiting the user's by-eye check of the palette
  and thumbnails, which is the one acceptance condition no automated test in this repo may cover
  (`project.md` anti-requirements rule out framebuffer goldens; GUI hover is not automatable here).

### 9.1 Requirements satisfied

REQ-350 (a)–(h). Per acceptance condition:

| Acceptance condition | Status | Evidence |
|---|---|---|
| PIPERUN opens the palette by itself, movable, non-blocking | met | `StartPipeRunCommand` → `CadPipePaletteSetOpen`; test *"PIPERUN opens the fittings palette…"*. Movable/non-blocking is an ordinary `ImGui::Begin` window — by eye |
| 2in run lists both 2in flanges, not the 4in | met | test *"A 2in run lists the 2in flanges and not the 4in one"*, using the bundled library's real names |
| Live re-filter when the routed size changes | met | same test's 4in half + `pipeFittingPaletteShownSize` invalidation |
| Row = shaded preview + ports + name | built | `ViewportRenderer::EnsurePartThumbnail` + the palette row; **by eye** |
| Click a part, click ON the run → splice, one undo | met | test *"A palette part armed and clicked ON a run splices into it"* (2 runs, 1 block ref, one `PushUndoSnapshot`) |
| Click a part, click OFF a run → plain INSERT | met | test *"…clicked OFF every run is placed as a plain INSERT"* |
| Palette survives the run; X closes; command reopens | met | the same PIPERUN test asserts it stays open; `PIPEPALETTE` toggles; X is `ImGui::Begin`'s own `p_open` |
| Empty category states the size | met | test *"An empty tab states the size it filtered on"* |
| Nozzle round-trips, old drawings unaffected | met | test *"The new nozzle part type round-trips through its tag"*; the value is APPENDED, and `.gs`/sidecars persist the tag string, not the number |
| REQ-100 holds — rendered once, not per frame | met by construction, **not yet measured** | `EnsurePartThumbnail` early-returns on a cache hit; see §9.6 |

### 9.2 What changed, and why

| File | Change | Why |
|---|---|---|
| `src/util/cadblock.hpp` | `CadPipePartType::Nozzle` + tag/parse; `CadBlockLibraryEntry` moved here from `CadBlocks.hpp` | D-2026-09-24-a (1). The struct moved down because `AppCommandState` now caches a listing of them and `CadBlocks.hpp` sits above it — see §9.5 finding 2 |
| `resources/blocks/fittings/*.json` (3 new) + `README.md` | metadata sidecars, class-agnostic; README documents the sidecar format | REQ-350 (h). Without them the bundled parts have no type or size and list under no tab |
| `src/commands/CadBlocks.hpp/.cpp` | category enum + `CadPipePaletteCollectRows` / `…EmptyReason` / `…CategoryOf`; `CadPipePaletteSetOpen`, `CadPipePaletteArmPart`; `PIPEPALETTE`; splice branch in `SubmitInsertBlockPick`; arming cleared in `FinishInsertCommand`; BSAVE marks a thumbnail stale | REQ-350 (b)(c)(d)(f)(g)(a). Filtering lives beside the library scan it filters, where it is testable without a window |
| `src/commands/CadCommands.hpp/.cpp` | `TrySplicePipeFit` split into `TrySplicePipeFitNamed` + part-type wrapper; `CadPipeRunUnderPick`; `ExpandTessellation` exposed; PIPERUN opens the palette; arming cleared on cancel; palette state fields | REQ-350 (f). The split is the finding in §8 item 1 |
| `src/render/ViewportRenderer.hpp/.cpp` | `PartThumbnailInput`, `EnsurePartThumbnail`, `PartThumbnailTexture`, `InvalidatePartThumbnail`, per-part FBO/texture cache, released in `Shutdown` | ADR-062. Every `gl*` for this feature is here and nowhere else |
| `src/ui/CadUiPaletteTabs.hpp` (new) | `CadUiVerticalText` / `CadUiPaletteTabButton`, promoted from `CadUi_BlockAuthoring.cpp`'s file statics | Two present-day call sites (§11.4), and it is what makes the two palettes read as one family |
| `src/ui/CadUi_PipeFittingPalette.cpp` (new) | the palette window + `ServicePipeFittingThumbnails` | REQ-350 (a)(b)(e)(g) |
| `src/app/main.cpp` | draw the palette beside the BEDIT one; service thumbnails beside `ServicePendingThumbnail` | ADR-062 — after `RenderScene` is the only safe point to bind another framebuffer |

### 9.3 Tests

- **`tests/CadPipePaletteTests.cpp` (new, 9 cases)** — category grouping incl. `Nozzle`; the bundled
  library at 2in and 4in; numeric size equality (`2in` / `2 in` / `2.0in`) and the refusal of a nearby
  size; unusable run sizes yielding nothing rather than everything; non-fitting and untagged entries
  never listing; all four class-precedence branches including the per-part-type scope; empty-tab prose.
- **`tests/CadPipeRunCommandTests.cpp` (+6 cases)** — arm-then-click on a run splices; off a run
  inserts; cancel disarms *and* a following ordinary INSERT does not splice; `CadPipeRunUnderPick`
  on/off/past-the-end/nearest-wins; arming an absent or nameless part is refused; PIPERUN opens the
  palette and finishing the run leaves it open.
- **`tests/CadBlockImportTests.cpp` (+1 case)** — BSAVE marks the edited definition's thumbnail stale,
  and does not queue the same name twice.
- **Results (release, MSVC, this machine):** `GoSurveySnapTests` **420/420**, `GoSurveyTests`
  **1218/1219**, `ctest` **1792/1800**.
- **The 8 ctest failures are pre-existing and were verified as such**, not assumed: the working tree
  was committed, `beta` was checked out and rebuilt, and the same 7 headless transcripts failed
  identically there (`issue233-command-name-at-point-prompt`, `issue402-offset-ucs`,
  `regression-58-offset-entity-id`, `req068-surface-selection`, `req087-feature-line-modify`,
  `req313-solid-isolines`, `req313-solid-primitives`). The 8th, `Tessellation agrees with the analytic
  figures and winds outward`, is TASK-272 §10.6's recorded cone-apex crack — "256 of 768 edges cracked
  on a plain `MakeCone` … still the suite's one failing test", and the count observed here is exactly
  256 of 768.

### 9.4 Assumptions (resolved)

- **ASSUMPTION-1** (a cached thumbnail only goes stale through BEDIT) — **closed as designed**: BSAVE
  appends the definition's name to `pipeFittingThumbStale`, and the UI service pass calls
  `InvalidatePartThumbnail`. The command layer never touches the renderer, because Commands sits below
  it. Pinned by the `CadBlockImportTests` case.
- **ASSUMPTION-2** (what counts as "on a run") — **stated and pinned**: within the pipe's own outer
  radius × 1.5 of its centreline, nearest centreline winning. The factor is named
  (`kCadPipeRunPickRadiusSlack`) with its reasoning, and all four behaviours are asserted.
- **ASSUMPTION-3** (a part with no B-rep lists without a picture) — **partly obsolete, better than
  assumed**: 2D linework is fed to the thumbnail as edges, so a 2D-only block gets a wireframe
  picture rather than none. A part with neither solids nor lines lists by name with a placeholder box.
- **ASSUMPTION-4 (new, raised while implementing)** — *the palette may import a library part into the
  drawing's block table as a side effect of arming it.* **Because:** a thumbnail and a placement both
  need geometry, which needs the file imported. **Risk if wrong:** a block definition appears in the
  drawing (and in BEDIT's picker, and in a save) that the user did not explicitly INSERT. **Validated
  by:** this is pre-existing behaviour, not new — `LoadBundledBlockLibrary` already imports
  `resources/blocks/*.dxf|.dwg` and `fittings/*.sat` into every drawing at startup, and picking a
  library row in the INSERT dialog already imports that row. The palette imports **only the part the
  user actually clicks**, not every listed part.

### 9.5 Findings from self-verification (both fixed)

1. **A per-frame directory scan.** The palette first called `CadBlocksCollectLibraryEntries` every
   frame — three `directory_iterator` walks plus a JSON parse per part. The INSERT dialog does the
   same, but it is a modal open for seconds; this palette stays open for a whole routing session, so
   it is file I/O on a per-frame path, which invariant **§11.7** does not allow without a profile.
   Fixed with `pipeFittingLibraryCache` + `…CacheValid`, invalidated when the palette's size changes,
   when a part is imported, and by an explicit **Refresh** button — the button being the honest answer
   for a file copied into the library folder from outside the application, rather than a hidden timer.
2. **Layer inversion caught by the compiler.** Caching those rows on `AppCommandState` did not
   compile, because `CadBlockLibraryEntry` was declared in `CadBlocks.hpp`, which sits *above*
   `AppCommandState`. Rather than reaching upward, the plain-data struct moved **down** to
   `util/cadblock.hpp` beside `CadBlockDefinition`; the functions that scan for it stayed put.
3. *(Process, not code)* A stray carriage return introduced by an editing script left
   `cadblock.hpp` with mixed line endings and one broken comment line, which git rendered as a
   1,818-line rewrite. Normalised to CRLF and repaired; the file's real diff is 20 added lines.

### 9.6 Architectural decisions made by Workshop

**None.** The three that this feature needed were recorded before implementation began — the `Nozzle`
enum value and the sidecar obligation (D-2026-09-24-a (1) and (2)) and the thumbnail render path
(ADR-062). Finding 2 above moved a struct between headers to obey §2; that is the layering rule being
applied, not a new decision.

### 9.7 Dependencies

**None added** (REQ-300). The thumbnail reuses the existing shaded/line/vertex-colour programs and the
existing `brep` tessellation; filtering is in-tree; the sidecars use the `nlohmann/json` already
vendored and already used by this exact code path.

### 9.8 Technical debt / deliberately not done

- **REQ-100 is met by construction, not by measurement.** The cache makes each part's preview a
  one-off, and the per-frame cost of an open palette is now a filter over a cached vector. A `BENCH`
  figure on the reference machine would still be the honest evidence, and REQ-350's acceptance asks
  for it; it is not claimed here.
- **Thumbnails are cached per `ViewportRenderer`**, i.e. per drawing tab, so switching tabs re-renders
  them. Correct, bounded, and not worth a shared cache until someone notices.
- **No ribbon button** for the palette (PIPERUN and `PIPEPALETTE` reach it) — the same exclusion
  REQ-345 B2 recorded for `PIPERUN` itself, and for the same reason.
- **Exact size matching only** — reducers and branch sizes are REQ-350 (c)'s named deferral.
- The **INSERT dialog's own library pane** still rescans per frame and still draws the top-down
  wireframe preview. Untouched on purpose (out of scope), but it is now the odd one out, and finding 1
  applies to it in a milder form.

### 9.9 Build status

`./dev/build release` — clean, 0 errors. New warnings introduced: none (the `C4834` warnings in
`CadBlockImportTests.cpp` are pre-existing and in lines this task did not touch).

### 9.10 Documentation

- `spec/requirements.md` — REQ-350 + traceability row.
- `spec/project.md` — D-2026-09-24-a.
- `spec/architecture.md` — ADR-062.
- `resources/blocks/fittings/README.md` — rewritten: the library takes `.sat`/`.dwg`/`.dxf`, and the
  sidecar format is documented with the "omit the class when you do not know it" rule.
- This task log.

### 9.11 Left for the user

The one thing no test here can judge: **open a drawing, run `PIPERUN`, answer `2in`, and look at the
palette.** Specifically worth judging by eye — whether the isometric view reads as the part, whether
64 px is the right thumbnail size, whether the port dots are visible against the body, and whether the
five tabs are the right grouping once real parts are in them.
