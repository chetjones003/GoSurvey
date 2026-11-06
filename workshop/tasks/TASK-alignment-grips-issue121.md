# TASK-alignment-grips-issue121 — Alignment grips like Civil 3D's, reverse carrying its dependents (REQ-487)

- Type:    feature (remaining REQ-487 statements)
- Status:  done (in review with PR #1152)
- Opened:  2026-11-04
- Owner:   chetjones003 ("Do req 487", 2026-11-04), Claude (implementation)
- GitHub:  #121, PR #1152

## 1. Requirement authority
REQ-487 statements 1–4 and acceptance 1–4; REQ-486 (the solver every grip uses); REQ-501 (the readout); REQ-488
(linked offsets). Recorded as D-2026-11-04-j.

## 2. Gap
Phase 2 shipped grips at the start, tangent ends and curve midpoints only. Missing: PI, PC/PT, centre and tangent
midpoint grips (statement 1, acceptance 3), the station / offset / bearing readout during a drag, and reverse updating
its dependents (statement 2: labels, design speeds and offsets). Station-equation editing (statement 3) and one undo
step per edit (statement 4) already held: `ALIGNMENTSTATION EQ ADD|REMOVE|LIST`, and the grip snapshot plus
`RefreshAlignmentOffsets` on every move.

## 3. Evidence (CLAUDE.md §5)
- Civil 3D 2026 (scratch Drawing8, closed unsaved; `build/artifacts/panel-487/`): the four-PI road at R 200 selected
  shows squares at the ends and tangent midpoints, circles at each PC, PT and curve midpoint, a triangle at each PI and
  dashed tangent extensions (`c3d-grips.png`). Hovering a grip shows the station/offset tooltip. Dragging PI 1 west
  kept both radii at 200 and re-fitted the curves: tangents 368.91 / 386.87 / 444.62 (`c3d-drag-pi-live.png`).
- Autodesk help "Exercise 2: Grip Editing an Alignment" and Civil 3D training material: PI grip keeps the radius and
  tangency; PC/PT grips change the radius keeping tangency; the midpoint is a pass-through point; the tangent midpoint
  square moves the tangent keeping its orientation.
- Repository: `Resolve()` re-fits a Free curve from the back tangent's start and bearing and the ahead tangent's end and
  bearing, so a PI move only has to re-aim the two tangents at their far PIs.

## 4. Implementation
- Domain: `GripKind` and `Grip::kind/entity`; `GripPoints` / `DragGrip` moved to `src/alignment/AlignmentGrips.cpp`
  (ids `1 + 10 * entity + kind`); `Alignment::PiAfterTangent`, `MovePi`, `MoveTangent` in `AlignmentEdit.cpp`
  (solver, locks and station locks as every edit). Pass-through radius from the quadratic for a circle tangent to both
  tangents through the point (larger root). `Alignment::Reverse` flips relative station labels and design speeds.
- Commands: `REVERSEALIGNMENT` negates linked offsets' distances; `AlignmentCursorReadout` reports on the grabbed
  alignment during a grip drag.
- Qt: grip shapes (square / circle / triangle), the hot grip matched by id, dashed PC–PI–PT lines; the Properties
  heading reads "Alignment" for alignments (panel finding).

## 5. Tests and verification
- `tests/AlignmentGripTests.cpp` (6 cases): grip placement (15 on the road), PI drag against closed forms (and plain
  corners, and a locked bearing), PC / PT / pass-through / centre radii, tangent-midpoint moves with curves and with
  corners, reverse with labels, speeds and twice-identity plus offsets keeping their side, the drag readout. Updated
  grip tests in `AlignmentEditTests.cpp` and `AlignmentSpiralCommandTests.cpp` to find grips by kind. `[alignment]` 181
  cases / 4385 assertions pass.
- Qt: `issue121AlignmentGripsShowAndDrag` (grips, Properties heading, PI drag, readout, captures).
- Build: `build.bat` Release, no warnings. Full suite 194/194 (`build/artifacts/ctest-487.log`).
- Visual panel `build/artifacts/panel-487/`: round 1 **PASS**; `RESULT.md`.

## 6. Product documentation review (REQ-453)
- `README.md`, `PRIVACY.md`: reviewed; no update needed.
- `resources/wiki/`: `Alignments.md` (grips, reverse, limits), `Commands/reversealignment.md`.
- Public wiki `Alignments.md` and gosurveycad.com `docs/alignments.html`: same changes.

## 7. Follow-ups
- Panel: a crosshair or rubber line from the grabbed point during a drag; an Alignment contextual ribbon tab and a
  command-line prompt during grip edits; selection emphasis closer to Civil 3D's glow.
- Civil 3D's separate radius triangle next to a curve midpoint is not offered (the midpoint, PC/PT and centre grips
  cover the radius).
- Reverse cannot keep a stretch that had no design speed empty (the list has no "none"); it takes the neighbouring
  speed and says so.
