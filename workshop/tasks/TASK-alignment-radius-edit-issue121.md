# TASK-alignment-radius-edit-issue121 — What a curve radius edit keeps (REQ-486, D-2026-11-04-f)

- Type:    fix (spec contradiction resolved)
- Status:  done (in review with PR #1152)
- Opened:  2026-11-04
- Owner:   chetjones003 (decision: "Option 1", 2026-11-04), Claude (implementation)
- GitHub:  #121, PR #1152

## 1. Problem
REQ-486 acceptance 2 said a radius edit (500 → 400) "updates later stations"; acceptance 3's GoSurvey rule said "a radius
edit keeps the length", which leaves later stations unchanged. The code followed acceptance 3 for every curve.

## 2. Evidence (CLAUDE.md §5)
- Repository: REQ-486.1 already says "a parameter is editable only if it participated in the entity's definition".
  `Alignment::EditEntity` kept the arc length on every radius edit; Free curves re-fit from their tangents.
- Civil 3D 2026 (this machine, scratch drawing closed unsaved, `build/artifacts/civil3d/radius-edit.ps1`): the panel road
  (90' tangent, R80 Δ75° curve between tangents, 60' tangent). Setting the curve radius to 60 through the COM API kept
  Δ = 75° and the PI; the curve went 104.72' → 78.54', the tangents grew to 105.35' and 75.35', and the end station moved
  7+54.72 → 7+59.23.
- Autodesk help: "About Editing Alignment Constraints" — "After an element is created, you can only edit the parameters
  that affect the specified constraints"; "Alignment Layout Tools" — Floating Curve (From element end, radius, length)
  and Free Curve Fillet (between two elements, radius).

## 3. Decision D-2026-11-04-f (user chose option 1)
A curve keeps what it was defined by: radius-and-Δ curves keep Δ (length and later stations change); radius-and-length
curves keep length; Free curves keep their tangents' intersection. Curves from objects and older files are radius-and-Δ.
Recorded in `spec/project.md` and REQ-486 statement 6 / acceptance 3.

## 4. Implementation
- `alignment::Entity::definedByLength` (arc only). `AppendCurve` (CURVELEN) sets it; `AppendCurveByDelta` (CURVE),
  `InsertCurveAfter` (ALIGNMENTINSERT CURVE) and create-from-objects fillets clear it; `AppendRaw` (DWG/objects) leaves it
  false.
- `EditEntity(CurveRadius)`: a radius-and-Δ, non-Free arc scales its length with the radius; with its length locked the
  edit is refused, naming the row.
- `GsIo`: `"dl": true` on radius-and-length arcs; absent reads as radius-and-Δ.
- Help: `resources/wiki/Commands/alignmentedit.md`.

## 5. Tests
`tests/AlignmentEditTests.cpp` `[req486]`: radius-and-Δ curve keeps Δ and moves later stations (AC2); radius-and-length
curve keeps its length; length-locked radius-and-Δ edit refused with the row named; the flag survives save/reload and
older files read as radius-and-Δ. Existing Free-curve test still keeps the PI. `[alignment]` 154 cases / 3931
assertions pass.

## 6. Evidence
- Build: `build.bat` Release exit 0, no warnings. Full suite: `build/artifacts/ctest-radius.log`.
- No visible UI change (no panel needed): the layout table and grips call the same edit.

## 7. Follow-ups
- REQ-486.1 also wants derived parameters shown read-only in the layout table by definition; the table still lets every
  curve value be edited (part of the dockable-editor work).
