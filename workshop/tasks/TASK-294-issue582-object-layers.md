# TASK-294 — Drawing Settings Object Layers tab (per-object creation layers)

- Type:    feat
- Status:  review
- Opened:  2026-09-29
- Owner:   Workshop
- GitHub:  #582 (increment 5 of 6)

## Requirement authority

REQ-361 (accepted 2026-09-29, D-2026-09-29-b; Pipe fitting row narrowed by D-2026-09-29-f),
REQ-356 (current colour stamping), REQ-353 item 4 (a fitting on a run takes its run's layer),
REQ-357 (the window), REQ-084 (*not implemented yet*), ADR-044 (trailer).

## Decision taken during planning

**D-2026-09-29-f** (user, 2026-09-29): REQ-353 item 4 and REQ-361's Pipe fitting row both claimed
a fitting on a pipe run. The user chose the run's layer; the Pipe fitting row covers only a
pipe-catalogue part placed off any run.

## Creation paths (inventory)

| Row | Paths |
|---|---|
| Survey point | `TryPlaceSurveyPoint` (Create Points, Traverse; new + Overwrite), `SurveyCsvImportFile`, `LoadSurveyPointsFromJsonFile` (a point with no layer) |
| Survey point label | `EnsureSurveyPointLabelMtext` when it creates the label MTEXT |
| TIN surface | `CreateSurfaceFromPointGroups`, `CreateSurfaceFromVolumeParents`, `RunSurfaceCreateGrid/Corr/VolGrid`; Create Surface dialog's layer field |
| Feature line | `CommitFeatureLineDraft`, WATERDROP EXTRACT FL |
| Pipe run | `MakeNewPipeRunAttrs` (PIPERUN commit; its fittings inherit it, REQ-353) |
| Pipe fitting | `PlaceInsertImpl` with no run attributes and a catalogue part (`partType != None`) |
| Solid | primitives, EXTRUDE (both), LOFT, SWEEP, REVOLVE, PRESSPULL, prompted solids, POLYSOLID (+ convert) |
| Table | VOLTABLE |

Not creations (keep their attributes): copies/paste, splits, booleans, explode, DXF/DWG import,
legacy table migration, BENCH scenes, PIPERUN's provisional live run.

## Plan / architectural-boundary check

- **Commands (data)**: `ObjectLayerKind` (8 rows), `ObjectLayerRow {layer, modifier, value,
  locked}` and `DrawingSettings::objectLayers` (defaults per REQ-361 item 1). Rides the existing
  undo / per-tab / trailer paths.
- **Commands (one resolver)**: `ResolveObjectLayer(settings, kind, name)` (pure: `*` → name,
  dropped when there is no name; Prefix/Suffix) and `EnsureObjectLayer(st, kind, name)` (resolves,
  falls back to the row's base layer for an invalid name, adds the layer row if missing — inside
  the caller's undo step). `MakeNewObjectAttrs` = `MakeNewEntityAttrs` (current colour, REQ-356)
  + that layer. Every path above calls it.
- **Undo**: survey point placement becomes one undo step (the acceptance needs UNDO to remove the
  point and its new layer); every other path already pushes its snapshot before creating.
- **UI**: the Object Layers tab (was greyed): Object (ribbon icon + name) | Layer (combo of the
  drawing's layers, or typed) | Modifier | Value | Locked (padlock); the info line; the disabled
  display-components checkbox. Create Surface's layer defaults to the resolved layer (following the
  name until edited) and is read-only when the row is Locked.
- **IO**: trailer JSON `drawingSettings.objectLayers` (absent → defaults).

## Tests

- `tests/ObjectLayersTests.cpp` (`[req361]`): resolver (Prefix/Suffix/`*`/no name); new survey
  point on `V-NODE` + label on `V-NODE-TEXT`, surface `EG` with Suffix `-*` on `C-TOPO-EG`, pipe run
  on `C-PIPE`, feature line, solid, table — each layer created, one UNDO removes object and layer;
  CSV import; fitting on a run keeps the run's layer, a catalogue part off any run goes on
  `C-PIPE-FITT`; current colour still applied; trailer round trip; per-tab.
- `tests/LibreDwgCadTests.cpp`: table survives DWG save → reopen.
- Dev Shell: the tab, and a Locked Surface row making Create Surface's layer read-only.

## Status log

- 2026-09-29: planned; SPEC GAP on fitting layers resolved (D-2026-09-29-f).
- 2026-09-29: implemented. Found during inventory: the Create Points panel has a layer field saved
  with the drawing — the second creation dialog with a layer choice (REQ-361 item 4) besides Create
  Surface. Tests that encoded "a new object lands on the current layer" (REQ-352/353/356 suites, the
  REQ-071 transcript's `EXTRACT … C-TOPO`) updated to REQ-361's layers; REQ-353 item 3 revision noted.

## Completion report

- Build: `./dev/build` clean; `dev/build-devshell.bat` clean.
- Tests: `[req361]` 9 cases (8 in `tests/ObjectLayersTests.cpp` + DWG round trip) pass. `ctest`
  1986/1993 — the 7 failures are the headless ones already failing on `beta` (re-checked: each still
  fails on its old, unrelated step). Dev Shell `req361-object-layers` passes in the real window (tab,
  disabled display-components checkbox, padlock → OK → Create Surface's layer read-only);
  `req360-transformation-tab` and `req358-zone-group` still pass.
- Acceptance: new survey point on `V-NODE` (label `V-NODE-TEXT`), TIN surface `EG` with Suffix `-*`
  on `C-TOPO-EG`, pipe run on `C-PIPE` — each layer created, one UNDO removing object and layer ✔
  (also solid, table); CSV-imported points on the Survey point row's layer ✔; a Locked row makes the
  creation dialog's layer read-only (Create Surface in the GUI, Create Points in a unit test) ✔; the
  table survives DWG save → reopen and differs per drawing tab ✔.
- Not verified by me: how the tab LOOKS (icons, padlock glyph, column widths) — handed to the user.
- Assumptions:
  - The TIN surface row covers every surface in the surface store (TIN, grid, volume, corridor):
    GoSurvey keeps them in one store and Civil 3D has one Surface row.
  - Survey points, solids, tables and new pipe runs have no name, so `*` is dropped for them; a
    fitting's name is its block (part) name; a feature line's is its FEATURELINE name.
  - An object name that makes an invalid layer name (e.g. `A/B`) falls back to the row's base layer.
  - Create Points' saved layer now uses a new key (`layerOverride`, empty = the Object Layers
    layer); the old `layer` key held `0` by default and could not tell a choice from that default,
    and REQ-361 item 6 wants existing drawings on the new layers.
  - Placing a survey point is now one undo step each (the Traverse commit stays one step for all).
- Not creations (keep their attributes): copies, splits, booleans, explode, imports of existing
  objects (DXF/DWG, DXF point conflicts), legacy table migration, BENCH, PIPERUN's provisional run.
- Debt: none new.
- Docs: D-2026-09-29-f; REQ-361 Pipe fitting note + revision; REQ-353 revision; this task.
