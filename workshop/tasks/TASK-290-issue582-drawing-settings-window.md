# TASK-290 — Drawing Settings window: entry points, units, scale and per-drawing settings

- Type:    feat
- Status:  review
- Opened:  2026-09-29
- Owner:   Workshop
- GitHub:  #582 (increment 1 of 6)

## Requirement authority

REQ-357 (accepted 2026-09-29, D-2026-09-29-b; Scale amended by D-2026-09-29-c). REQ-020 / REQ-022
(the drawing unit), REQ-107 (INSERT unit scale), REQ-308 (Start tab), REQ-084, REQ-175 / ADR-044
(trailer), REQ-101, REQ-201.

## Starting state (measured)

| Item | Before |
|---|---|
| `DRAWINGSETTINGS` | unknown command; ribbon Palettes "Drawing Settings" button is *not implemented yet* |
| Drawing unit | `drawingInsUnits`; UNITS offers Feet/Meters/Unitless; DXF/DWG import accept 0/2/6 only |
| Plot scale | `modelUnitsPerPlottedInch`, status-bar combo (16 imperial scales) + `PLOTSCALE` |
| Per tab | neither unit nor plot scale is in `DrawingDocument` — both leak across tabs |
| Undo | neither unit nor plot scale is in `DrawingGeometrySnapshot` |
| Feet↔meters | `CadBlockUnitsScale` hard-codes the international foot, in `float` |
| DWG header | export never sets `INSUNITS` / `LUNITS` / `AUNITS` |

## Plan / architectural-boundary check

- **Commands** (`CadCommands.{hpp,cpp}`): a `DrawingSettings` struct (angular units, imperial→metric
  conversion, scale-inserted-objects, set-drawing-variables) on `AppCommandState`, `DrawingDocument`
  and `DrawingGeometrySnapshot`; `drawingInsUnits` and `modelUnitsPerPlottedInch` join the latter two
  (per-tab + undo). One `ApplyDrawingSettings(st, units, mup, settings, log)` writes all of them as
  one undo step, only when something changed. One `SetDrawingPlotScale(st, mup)` (the label/cache
  refresh the status combo and `PLOTSCALE` each duplicated) used by all three writers. Every
  interactive writer of the unit / plot scale (UNITS OK, `INSUNITS`, `PLOTSCALE`, status combo)
  pushes an undo frame, because those values are now restored by undo — without that an unrelated
  UNDO would silently revert them. `DRAWINGSETTINGS` / `EDITDRAWINGSETTINGS` in the registry;
  refused on the Start tab.
- **Domain** (`util/cadblock.hpp`): `CadBlockUnitsScale` computes in `double` and takes the
  inches-per-meter factor; `CadBlockInsertUnitsScale` passes the drawing's and returns 1 when
  "Scale objects inserted…" is off. A shared plot-scale list (`util/`) by drawing unit, used by the
  status combo and the window.
- **IO**: `drawingSettings` object in the trailer JSON (`BuildRoot` / `ApplyDocumentFromJson`),
  defaults when absent. DXF writes `$AUNITS` / `$LUNITS` when set-variables is on; DXF and DWG
  import accept INSUNITS 1 and 4; DWG export sets `INSUNITS` always and `LUNITS` / `AUNITS` when on.
- **UI**: `DrawDrawingSettingsWindow` (product accent + `BeginStyledDialog`, three tabs, the two
  later ones greyed *not implemented yet*), edits a staged copy; OK / Cancel / Apply. File menu item
  (disabled on the Start tab), ribbon Palettes button enabled. UNITS offers the five units.
- No new dependency; no format version bump (additive JSON object).

## Tests

`tests/DrawingSettingsTests.cpp` (`[req357]`): command entry + Start-tab refusal; apply = one undo
step, no-op apply pushes nothing; units change moves no geometry; US vs international foot insert
factor and the off switch; custom scale validation; trailer round-trip + legacy default; per-tab
isolation; DXF `$AUNITS`; `PLOTSCALE` / `INSUNITS` undo.

## Completion report

(pending)
