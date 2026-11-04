# TASK-alignment-cursor-readout-issue121 — Station / offset / bearing in the dynamic cursor (REQ-501.2)

- Type:    feature (REQ-501 statement 2, acceptance 2)
- Status:  done (in review with PR #1152)
- Opened:  2026-11-04
- Owner:   chetjones003 ("Do req 501.2", 2026-11-04), Claude (implementation)
- GitHub:  #121, PR #1152

## 1. Requirement authority
REQ-501.2: "While moving or creating a point near an alignment, the dynamic cursor shows Station, Offset, Bearing …,
integrated with the existing dynamic input (not a second cursor UI)." Acceptance 2: the readout equals
`GetStationOffset` at the cursor. Depends on REQ-484 (the query) and REQ-076.

## 2. Gap
`AlignmentCursorReadout` was computed on every mouse move but shown only in the status bar; the dynamic input beside
the cursor had nothing.

## 3. Evidence
Civil 3D 2026 (scratch Drawing9, closed unsaved; `build/artifacts/panel-501/`): during LINE, near the four-PI road,
a tooltip beside the cursor reads "Main Road  SO  2+60.76'  21.75'" — name, station and offset (no bearing). The spec's
format adds the bearing and puts it in the dynamic input, so no SPEC GAP.

## 4. Implementation
- `AlignmentCursorReadout` (`CadCommands_Alignment.cpp`): one formatter, `<name>  STA  OFF  BRG`, for both the idle
  search (within a tenth of the view of the nearest alignment) and the grip-drag case (the grabbed alignment).
- Qt (`EditorInput.cpp`): a `dynamicAlignmentReadout` line in the dynamic input under the fields, with a tooltip-like
  backing; shown during point entry near an alignment, and on its own during a grip drag with no command.
  `DynamicInputAcceptsInput()` keeps typing and Enter away from the dynamic input when it shows only the readout.

## 5. Tests and verification
- Qt `issue121DynamicCursorReadsStationOffsetBearing`: real mouse-move events during LINE; the readout equals
  `GetStationOffset` at the cursor (station, offset, then bearing); hidden far from the road while the fields stay;
  readout-only during a grip drag, which takes no input; Esc clears it. Domain `[alignment]` 181 cases pass (the
  readout tests in `AlignmentCogoTests` and `AlignmentGripTests` still hold).
- Build: `build.bat` Release, no warnings. Full suite 194/194 (`build/artifacts/ctest-501.log`).
- Visual panel `build/artifacts/panel-501/`: round 1 PASS (backing and BRG label added from its findings), round 2
  **PASS**; `RESULT.md`.

## 6. Product documentation review (REQ-453)
- `README.md`, `PRIVACY.md`: reviewed; no update needed.
- `resources/wiki/`: `Drafting-Aids.md` (dynamic input near an alignment), `Alignments.md` (cursor readout, grip drag).
- Public wiki (079f289) and gosurveycad.com `docs/drafting-aids.html`, `docs/alignments.html` (c0f3d34): same changes.

## 7. Follow-ups (panel, non-blocking)
- A lighter, Civil 3D-like tooltip backing; a tighter dynamic input box with a backed prompt line; zero-padded bearings.
- Civil 3D also shows the tooltip with no command running; REQ-501.2 asks only for point entry and moving, so idle
  hover stays in the status bar.
