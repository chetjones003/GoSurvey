# GoSurvey — source layout

This document describes how C++ sources are organised. All `#include "Header.hpp"` paths are short names only — CMake and the editor add each domain folder to the include path so headers are found without directory prefixes.

## Directory map

> **Note (2025-06):** the former `src/cad/` was split into `src/commands/`,
> `src/viewport/`, and `src/util/` (see the source-restructure refactor). Header
> paths below use the current folders.

| Folder | Role |
|--------|------|
| `src/app/` | Application entry (`main.cpp`) — GLFW/ImGui frame loop, startup workspace loading, global Enter/key routing, modal dispatch, high-level wiring. |
| `src/ui/` | ImGui panels, ribbon, menus, command line UI + dynamic input, drawing viewport input, multi-tab document bar, splash screen, rich MTEXT editor, modals (copy-survey, DXF point conflict), Options dialog, Traverse Editor UI, import/export-points panels. |
| `src/commands/` | CAD model and command orchestration: `AppCommandState` (`CadCommands.hpp`), all drawing/editing commands, command registry + fuzzy dispatch, footer hints, linetype metadata (`CadLinetype`), and the **document coordinate frame** (`CadCoordinateFrame` — local/world origin, rebase, zoom-extents). |
| `src/viewport/` | Viewport interaction: object snap (`CadSnap`), rubber-band preview (`CadRubberPreview`), transform/selection preview (`TransformPreview`), pick policy. |
| `src/util/` | Header-only helpers shared across domains (e.g. string utilities, `geom2d`, number/angle formatting). |
| `src/io/` | Workspace `.gs` I/O (JSON via nlohmann), DXF import/export, user preferences, survey CSV import/export, DXF ACI colour tables. |
| `src/survey/` | In-memory survey / COGO points model (`SurveyPoint`, create-points bookkeeping, label styles, label MTEXT linking, merge/conflict helpers). |
| `src/traverse/` | Traverse subsystem: raw-observation reduction (`TraverseCalc`), least-squares closure (`TraverseLeastSquares`), and Autodesk FBK raw-data import (`FbkImport`). |
| `src/render/` | OpenGL viewport renderer — geometry batches, overlays, PDF texture drawing, view-relative tessellation. |
| `src/platform/` | OS-specific helpers: Windows file dialogs, custom frame controls (title bar), application icon loading. |
| `src/pdf/` | PDF underlay subsystem: rasterisation pipeline (`PdfAttach.cpp`), async attach worker, snap geometry extraction, image-based snap-target detection, visibility mask, spatial endpoint grid. |

## Key data structures

### `AppCommandState` (`src/commands/CadCommands.hpp`)

Central mutable state for the entire application. One instance lives in `main.cpp` and is passed by reference to every UI and command function. Fields are grouped by subsystem:

- **Geometry containers** — `userLinesFlat`, `userCirclesCxCyR`, `userArcs`, `userEllipses`, `userPolylineVerts/Offsets`, `cadAnnotations`, `pdfAttachments`. Stored in **local** space (see Document coordinate frame below).
- **Per-entity attributes** — parallel `EntityAttributes` vectors and `drawingLayerTable`.
- **Active command** — `active` (Kind enum) plus per-command phase sub-enums and draft state.
- **Selection** — `selection` (vector of `SelectedEntity`) and `selectedSurveyPointIndices`.
- **Survey** — `surveyPoints`, `createPointsOpts`, label layout cache, and DXF-import merge state (`pendingDxfConflictPoints`, `dxfPointConflictModalOpen`, `dxfPointConflictOffset`).
- **Coordinate frame** — `worldDocumentOriginX/Y` (the local↔world offset; see below), plot scale (`modelUnitsPerPlottedInch`), and `drawingInsUnits`.
- **ALIGN** — see section below.
- **Viewport** — pan/zoom, snap settings, crosshair style, GPU revision counter (`cadGpuRevision`).
- **UI toggles** — `showCreatePointsWindow`, `showAlignResultsWindow`, `showLayerManagerWindow`, `showTraverseEditorWindow`, `showImportPointsWindow`, etc.
- **Multi-document** — the open drawings are kept as document snapshots; switching tabs saves/restores the full `AppCommandState` (geometry, layers, survey points, coordinate frame, viewport).
- **Reports** — `surveyReportTabs` (vector of title/body pairs), `surveyReportSelectLatestPending`.

### `PdfAttachment` (`src/pdf/PdfAttach.hpp`)

Holds everything needed to render and snap-to one attached PDF page:

- `glTexId` — GPU texture of the rasterised page.
- `snapLinesFlat` — Flat `(x0,y0,x1,y1)` segments (or degenerate point pairs for image-detected targets) in PDF page-point space.
- `snapEndptGrid` — CSR spatial grid built by `BuildSnapEndptGrid`; buckets endpoint pairs by page-space cell for O(cells-near-cursor) lookup.
- `snapVisMask` — 256×256 binary grid; each cell is 1 if the corresponding page tile contains visible foreground pixels. Used by snap to reject candidates in blank areas.
- `snapVisDark` — `true` when this is a dark-background PDF (CAD export style); drives both the snap mask threshold and the shader filter direction.
- `showBackground` — User toggle; `false` (default) renders the page with background made transparent.

## Snap pipeline (`src/viewport/CadSnap.cpp`, `src/pdf/PdfAttach.cpp`)

1. **Extraction** (`ExtractSnapFromPage`) — walks pdfium path objects; collinear-run dedup and minimum-length filter produce `snapLinesFlat`.
2. **Image-based override** (`BuildImageSnapPts`) — for dense PDFs (>2 000 path segments, e.g. GIS exports), replaces path-based snap: renders a 512×512 binary mask, analyses 8-connected topology to find endpoints (1 neighbour), corners (2 non-opposite), and junctions (3+), emits degenerate point pairs.
3. **Grid build** (`BuildSnapEndptGrid`) — packs endpoint pairs into the CSR `SnapGrid`.
4. **Query** (`CadSnap::FindBest`) — inverse-transforms cursor to PDF space, enumerates grid cells within tolerance, checks `snapVisMask`, transforms hits back to world space.

## Rendering (`src/render/ViewportRenderer.cpp`)

The PDF texture shader (`kTexFs`) supports two filter modes selected by `uDarkBg`:

- **Light-bg mode** — un-premultiplies the white tint (`bgMix = min(r,g,b)`), fades near-white pixels to transparent, boosts dark/grey content toward white so black lines remain readable on the dark CAD viewport.
- **Dark-bg mode** — un-premultiplies the black tint (`contentA = max(r,g,b)`), fades near-black pixels to transparent, restores full colour saturation to surviving pixels.

Background type is auto-detected by `DetectDarkBackground` using corner-patch sampling (4 corners + 4 edge midpoints, 12×12 px each) so PDFs with large coloured fills are classified correctly.

## ALIGN command — 2D Helmert transformation

### Model

ALIGN computes a **2D similarity (Helmert) transformation**: `X' = a·x − b·y + tx`, `Y' = b·x + a·y + ty`, where `a = s·cos θ`, `b = s·sin θ`. Four unknowns: scale `s`, rotation `θ`, translation `(tx, ty)`.

With ≥ 2 control pairs the system is over-determined. A 4×4 normal-equation system `(AᵀA)·p = Aᵀb` is assembled analytically exploiting the sparsity of `AᵀA` and solved via Gaussian elimination with partial pivoting (`SolveHelmert4x4`, `CadCommands.cpp`). With exactly 1 pair the result degenerates to translation-only (`a=1, b=0`).

### State machine (`AlignPhase` in `AppCommandState`)

```
PickSelection  ──Enter──►  PickSrc  ──click/type──►  PickDst  ──click/type──►  PickSrc  ─── ...
                                                                                    │
                                                                                  Enter
                                                                                    │
                                                                                  Solve (results window)
                                                                                    │
                                                                                 Apply / Close
```

- **PickSelection** — entered when ALIGN starts with no existing selection. Viewport clicks drive the standard selection-box machinery (`BeginSelectionBoxCorner` / `SubmitViewportPick`). Enter commits the selection snapshot (`alignSelectionSnapshot`, `alignSurveySnapshot`) and advances to `PickSrc`.
- **PickSrc** / **PickDst** — alternating coordinate picks via `SubmitViewportPickImpl`. Source picks use OSNAP-snapped world coordinates; destination picks accept typed `X,Y` or viewport clicks.
- After Enter in `PickSrc`/`PickDst` phase, `ExecuteAlignCommand` calls `RecalcAlignResult` (solve without apply) and opens `showAlignResultsWindow`.

### Results window (`DrawAlignResultsWindow`, `CadUi.cpp`)

- Displays live solution parameters (scale, rotation in D°M′S″, translation, point error RMS).
- Each pair row has a **`-`** remove button; removal calls `RecalcAlignResult` to update the solution immediately.
- **Apply Scale** checkbox: when checked the full Helmert (including scale) is applied; when unchecked `ApplyAlignCommand` normalises `(a,b)` to unit scale and re-derives translation from centroids (`tx′ = x̄_dst − (a′·x̄_src − b′·ȳ_src)`).
- **Apply** button calls `ApplyAlignCommand` which applies the transform, tags survey points, and pushes a report tab.
- **Close** discards without applying.

### Applying (`ApplyAlignCommand`, `CadCommands.cpp`)

1. Compute actual `(a, b, tx, ty)` — either from `alignLastResult` directly or scale-stripped.
2. Identify source and destination survey points by proximity (tolerance 0.01 drawing units) **before** the transform.
3. Call `ApplyHelmertToAllGeometry` — optionally **selective** when `alignHasSelection` is true (only entities in `alignSelectionSnapshot`/`alignSurveySnapshot` are moved).
4. **Restore destination survey points** to their exact `dstX/dstY` coordinates; append `" CON"` to their description.
5. **Tag source survey points** with `" ADJ"` in their description.
6. Update MTEXT labels for all tagged points via `EnsureSurveyPointLabelMtext`.
7. Push a plain-text report to `surveyReportTabs` and set `surveyReportSelectLatestPending = true`.

### Selective geometry transform (`ApplyHelmertToAllGeometry`)

When `selEnts` / `selSurvey` pointers are non-null, the function builds `unordered_set<int>` for each entity type and skips entities not in the set. Survey-linked MTEXT annotations (`surveyPointLabelFor >= 0`) are always skipped in the annotation loop and repositioned via `RepositionSurveyLabelMtextForPoint` after the survey-point coordinates are updated, preventing double-transformation.

## Command hint routing

### Dynamic cursor (`CommandInputHint`, `CadUi.cpp`)

`CommandInputHint(cmd)` returns the hint string shown in:
1. The command-line `InputText` placeholder text.
2. The **floating cursor palette** (`##ViewportCommandInput`) that appears near the mouse when a command is active and the cursor is inside the viewport.

The function handles all common commands explicitly, then falls back to each footer-hint function (`AlignCommandFooterHint`, `LineCommandFooterHint`, etc.) in sequence. Any command that defines a footer hint therefore automatically appears in the dynamic cursor without additional changes to `CommandInputHint`. This is the intended extension point for new commands.

### Footer hints (`*FooterHint` functions, `CadCommands.cpp`)

Footer hints return `""` when their command is not active, so the fallback loop in `CommandInputHint` is safe to call unconditionally. They are also rendered in the command-line panel footer (`DrawCommandLinePanel`) for persistent reference.

### Global Enter routing (`main.cpp`)

The main loop checks `ImGui::IsKeyPressed(ImGuiKey_Enter)` and `!ImGuiIO::WantTextInput`. When Enter is pressed outside a text field and any command is active, `ProcessCommandLineSubmit` is called with an empty buffer — this triggers phase transitions (e.g. PickSelection→PickSrc in ALIGN, LINE close, TRIM done) without requiring the user to click the command line first.

## Create points — placement mode

The **Create points** panel (`DrawCreatePointsPanel`) activates point placement whenever `showCreatePointsWindow` is true. No separate toggle is needed: opening the panel enables placement, closing it disables it. The viewport OSNAP system and click handler check `showCreatePointsWindow` directly instead of a separate `createPointsPlacementActive` flag (which was removed). The panel exposes only the settings that matter for daily use: next point ID, default layer/description/elevation, duplicate policy, and JSON file save/load.

## Document coordinate frame (`src/commands/CadCoordinateFrame.cpp`)

All geometry — including survey points — is stored in **local** space; world (state-plane)
coordinates are `world = local + worldDocumentOrigin`. Keeping a near-origin local frame
preserves `float` precision for large real-world coordinates (e.g. Louisiana state plane).

- `LocalFromWorld` / `WorldFromLocal` (and the `WorldXFromLocal`/`WorldYFromLocal` inline
  helpers) convert between the two; UI readouts, the VIEWPOINTS table, and `{north}`/`{east}`
  labels all display via these so the user always sees world coordinates.
- `ShiftAllStorageBy` translates every stored container (geometry + survey points) by a delta.
- `ApplyDocumentOriginRebase` changes the origin and shifts storage so world positions are
  unchanged; it also regenerates survey-point label text (origin affects `{north}`/`{east}`).
- `RebaseDrawingToLocalOrigin` re-centres the document on its centroid (used after DXF import);
  `MaybeRebaseLargeCoordinates` does so on load/CSV-import when stored coordinates are large.
- **Invariant:** any importer that ingests *world* coordinates (CSV, DXF survey points) must
  convert to local (subtract the origin, in `double`) before storing into the `float` fields,
  otherwise points land an origin's-distance away. `.gs` files store world coordinates with
  origin 0 and rebase on load.

## Survey-point DXF round-trip and merge (`src/io/DxfIo.cpp`, `src/survey/SurveyPoints.cpp`)

Survey points export as native `POINT` entities at world coordinates carrying `GOSURVEY` XDATA
(id / label style / description). On import (REQ-023):

- A `POINT` with GoSurvey XDATA is reconstructed; a foreign `POINT` becomes snap cross-lines.
- Import replaces CAD geometry but **preserves** existing session survey points. The kept points
  are shifted back to world space before the new origin is established (because `ClearCadGeometry`
  zeroes the origin), then rebased with the new geometry.
- The DXF's reconstructed points are collected in a side buffer (threaded through
  `ParseEntityRegion`) and merged: non-colliding ids are added before the rebase; colliding ids
  are deferred (in world coords) to `DrawDxfPointConflictModal`, which calls
  `ResolveConflictingWorldSurveyPoints` to overwrite or offset.

## Traverse subsystem (`src/traverse/`)

- `TraverseCalc` reduces raw per-leg observations (horizontal angle, distance, vertical angle;
  Face 1 / Face 2 sets) into leg vectors and accumulated coordinates.
- `TraverseLeastSquares` performs the closed-traverse least-squares adjustment and residual
  reporting — an in-tree solver, no external linear-algebra dependency (ADR-001).
- `FbkImport` parses Autodesk FBK raw survey files into the editor's observation model.
- UI lives in `src/ui/CadUi_TraverseEditor.cpp` (opened via `showTraverseEditorWindow`);
  results are pushed to the Reports tab. Regression tests: `tests/TraverseTests.cpp`.

## Build

- **CMake 3.20+** with the `ninja-release` preset (see `CMakePresets.json`).
- **Languages**: `CXX C RC` — the `RC` language compiles `resources/icons/app.rc` to embed the application icon in the Windows executable.
- `target_include_directories` adds `src` and all subdirectories so includes stay portable.

## Frame order (conceptual)

1. Poll input, `ImGui::NewFrame`.
2. Global key handling — Enter routed to `ProcessCommandLineSubmit` when any command is active and no text field has focus.
3. UI (`CadUi`, layout, modals) mutates `AppCommandState`.
4. Rubber lines (`CadRubberPreview`) and transform/selection preview batches (`TransformPreview`).
5. `ViewportRenderer` draws geometry, PDF texture overlays, snap glyphs.
6. Present.

## Adding a new command

1. Add a `Kind` enumerator to `AppCommandState::Kind`.
2. Add any phase sub-enum and draft fields to `AppCommandState`.
3. Add a `StartXxxCommand` function (clears active, sets phase, logs prompt).
4. Add dispatch in `DispatchByPrimary` (registry entry + `StartXxxCommand` call).
5. Add a `const char* XxxCommandFooterHint(const AppCommandState&)` function — return `""` when not active. It will automatically appear in the dynamic cursor via the `CommandInputHint` fallback.
6. Handle viewport picks in `SubmitViewportPickImpl` and typed input in `ProcessCommandLineSubmit`.
7. Handle Enter (empty submit) in the `if (line.empty())` block if the command needs it.
8. Add a cancel path in `CancelActiveCommand`.

## Adding a new module

Place the `.cpp`/`.hpp` pair in the most fitting folder, add the `.cpp` to `add_executable` in `CMakeLists.txt`. No include changes needed unless you add a new top-level folder (which requires a new `target_include_directories` line in CMake). Large UI entry points may be split across multiple `.cpp` files in the same folder sharing one `.hpp` API (e.g. `CadUi_SurveyReports.cpp`).
