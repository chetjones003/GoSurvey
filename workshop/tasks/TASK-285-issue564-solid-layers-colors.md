# TASK-285 — layers and colours for solids and pipe runs

- Type:    feat
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  #564 section 6 (fifth increment; §3 = #566, §1 = #568, §2 = #569, §4 = #570); follow-up #575

## Requirement authority

REQ-352 (new, D-2026-09-28-g). REQ-201 (no silent skip), REQ-313 / ADR-045 (solids), REQ-345
(pipe runs). REQ-102 (layer Lock, proposed) is referenced, not changed.

## Decision (D-2026-09-28-g)

Asked in plain English, decided as recommended: fix the real gap (Properties edits + undo) and give
the ribbon Layers combo AutoCAD's rule; `CHPROP` / `MATCHPROP` / `LAYMCUR` and a ribbon colour
dropdown — none of which exists for any entity type — go to GitHub issue #575.

## Starting state (measured)

| §6 item | Before |
|---|---|
| New solid stamps current layer, ByLayer | works (`MakeNewEntityAttrs` at every creation site) |
| ByLayer resolves to the layer colour | works (`ResolveEntityRgbaForViewport`) |
| Layer Off / Freeze hide solids and pipe runs | works (`SolidVisible`, `PipeRunWorldSolidVisible`) |
| Layer Lock | enforced for NO entity type (REQ-102 proposed) |
| Save → reopen | works (`GsIo` / ADR-044 trailer carry both attribute arrays) |
| Properties shows a solid's layer / colour | **no** — `CollectGeneralAttrs` skipped solids, pipe runs and block refs |
| Properties edits a solid's layer / colour | **no** — `ApplyLayerToSelection` / `ApplyColorToSelection` skipped them silently |
| Layer / colour edit undo | **no, for any type** — no snapshot was pushed |
| Pipe run colour edit reaches the drawn pipe | **no** — `RebuildPipeRunWorldSolids` keyed on geometry only, so the pipe solids kept stale attributes |
| Fitting (block ref) colour edit reaches its solids | **no** — same, `BlockRefWorldSolidsSig` |
| Ribbon Layers combo on a selection | sets the current layer only, for every type |
| CHPROP / MATCHPROP / LAYMCUR / ribbon colour combo | do not exist → #575 |
| DWG / DXF export of a solid | no ACIS writer; skipped with a stated count — nothing to carry a layer |

## Plan / architectural-boundary check

- **Commands (Domain)**: one templated accessor `AttrsOfSelected` (const + mutable, the
  `AttrsForKind` shape) covering every attribute-carrying type, solids and pipe runs included.
  `CadEntityAttrsForSelected` keeps its narrower isolation contract by excluding the two
  explicitly (isolation never listed them; widening it is not this section).
  `CadApplyLayerToSelection` / `CadApplyColorToSelection` write one field through it, pushing ONE
  undo snapshot only when something changes. `CadSelectionLayer` / `CadRibbonPickLayer` hold the
  ribbon rule so it is testable headlessly.
- Derived display: `RebuildPipeRunWorldSolids` re-copies run attributes on its early-out;
  `BlockRefWorldSolidsSig` mixes the insert's layer and colour (ByBlock solids resolve through them).
- **UI**: `ApplyLayerToSelection` / `ApplyColorToSelection` delegate to the command layer (≈100
  lines of per-type branches removed); `CollectGeneralAttrs` reads solids, pipe runs and block refs
  through `CadEditableAttrsForSelected`; the Properties header names "3D Solid" / "n solids" and
  counts them in the mixed line; the ribbon combo calls `CadRibbonPickLayer`.
- No new dependency, no new entity kind, no persistence change.

## Tests

`tests/SolidLayerColorTests.cpp` (GoSurveySnapTests, `[req352]`), asserting on the ASSEMBLED
display colour (`solidDisplayGeometry`), not only on the attribute:

- solid colour edit → displayed red; undo / redo;
- ByLayer solid on `PIPE-STEEL` takes the layer colour and follows a layer colour change;
- Off and Freeze hide a solid and a pipe run; thaw shows them;
- pipe run colour edit reaches already-built pipe solids (**fails with the rebuild fix removed** —
  checked);
- solid + pipe run + line → one layer, one undo step; a new layer name joins the table;
- a new layer name typed for a solid ALONE joins the layer table and can be turned off;
- an edit that changes nothing pushes no undo;
- ribbon: moves a selection and logs it, keeps the current layer; `(varies)`; no selection → sets
  the current layer;
- save → reload keeps a solid's and a pipe run's layer and colour.

## Results

- `[req352]`: 9 cases, 69 assertions, all pass.
- Full suite: 1860/1867. The 7 failures (issue233, issue402-offset-ucs, regression-58, req068,
  req087, req313-solid-isolines, req313-solid-primitives) fail identically on unmodified `beta`.

## Found in the final review

- `CollectLayersUsedInDrawing` (what `SyncDrawingLayerTableWithGeometry` adds rows from) scanned
  only the 2D stores, so a new layer name given to a solid alone never became a layer row — the
  solid sat on a layer the Layer Manager could not show or turn off. It now scans every store the
  edit can write. The first mixed-selection test hid this (its line put the name in the table).

## Not in scope / technical debt

- #575: `CHPROP`, `MATCHPROP`, `LAYMCUR`, a ribbon colour dropdown.
- Layer Lock (REQ-102) is still enforced for no entity type.
- Properties' Linetype / Lineweight / Transparency edits still skip solids and pipe runs (the issue
  puts linetype / lineweight on a shaded body out of scope).
- Because the layer / colour edit now covers every attribute-carrying type, a filled region, mesh,
  surface, feature line or point cloud in a mixed selection now moves with it too, where before it
  was silently skipped; the Properties header still does not name those types.
- A GUI hand-check of the Properties panel and ribbon combo is left to the user (hover / combo
  interaction is not automatable — see the viewport-click memory notes).
