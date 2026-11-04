# TASK-alignment-labels-visible-issue121 — Readable alignment labels; geometry-point, design-speed and segment labels (REQ-492)

- Type:    fix + feature
- Status:  done (in review with PR #1152)
- Opened:  2026-11-04
- Owner:   chetjones003 (decision: "Option 2", 2026-11-04), Claude (implementation)
- GitHub:  #121, PR #1152
- Branch:  feat/implement-issue-121

## 1. Authority
- **REQ-492** (accepted 2026-11-02): label set items include Major Stations, Minor Stations, Geometry Points, Station
  Equations and Design Speeds (492.1); segment labels for Line, Curve and Spiral with the stated content (492.3).
- **REQ-495.2**: station ticks and labels, geometry point markers are drawn from the style.
- **REQ-491**: ticks and markers carry a colour.

## 2. Problem (found during the REQ-502 visual panel recapture)
1. The viewport draws station text, equation text, station/offset text and the fallback tick, marker and arrow colour in
   a fixed `#20252a`. The model background is near-black in both themes and has no setting, so none of it can be read.
2. Geometry-point labels, design-speed labels and segment labels are not drawn at all. `ALIGNMENTLABEL ... SEGMENTS` only
   sets a flag used by tables.

## 3. Evidence pass (CLAUDE.md §5)
- Repository: REQ-492/495 text above; `QtAnnotations.cpp` alignment block; `AlignmentLabelSet` has no geometry-point or
  design-speed item; `CurveLabelText` (REQ-492 AC3) exists and is tested.
- Civil 3D 2026 (this machine, 2026-11-04, `build/artifacts/panel-502/1-civil3d-viewport-warning.png`), Imperial NCS
  template, label set "All Labels":
  - Geometry points labelled `BP: 5+00.00`, `PC: 5+90.00`, `Mid: 6+42.36`, `PT: 6+94.72`, `EP: 7+54.72`, each written along
    a perpendicular leader on one side of the alignment. Line midpoints, curve centres and curve PIs are not labelled.
  - Major stations (`6+00`, `7+00`) are written along the alignment beside the tick; minor stations show a tick only.
  - Design speed: `STA 5+00.00` / `DESIGN SPEED = 30 mi/h` along a perpendicular on the other side.
  - Label text has its own colour (label style / layer), distinct from the dark background.
  - Segment labels are not part of the label set; Civil 3D adds them with Add Labels > Multiple Segment.

## 4. Decisions (recorded in spec/project.md as D-2026-11-04-c)
1. Alignment label text, and ticks, markers and arrows whose style colour is ByLayer, draw in the alignment layer's
   colour (CAD ByLayer), not a fixed colour.
2. Minor stations draw a tick only; major stations get text (Civil 3D evidence above).
3. Geometry-point labels: `<code>: <station>` for BP, EP, PI (tangent–tangent), PC, PT, PCC, PRC, TS, ST, SC, CS, SS and
   Mid (curve midpoint). Line midpoint, curve centre and curve PI are not labelled.
4. Design-speed label: `STA <station> DESIGN SPEED = <speed> mi/h` (km/h when the drawing unit is metres or millimetres).
5. The label set gains Geometry Points and Design Speeds items, on in `Standard`, switched with
   `ALIGNMENTLABELSET SHOW|HIDE <set>, GEOMETRY|SPEEDS`. Older files without the fields load with them on.
6. Segment labels draw when the alignment's segments are labelled (`ALIGNMENTLABEL <name>, SEGMENTS`): line
   `<bearing> <length>'`, curve `CurveLabelText`, spiral `A = .. Ls = ..' R1 = .. R2 = ..` (∞ for a tangent end).

## 5. Implementation
- Domain `src/alignment/AlignmentLabels.{hpp,cpp}`: `GeometryPointAbbreviation`, `CollectGeometryPointLabels`,
  `CollectDesignSpeedLabels`, `LineLabelText`, `SpiralLabelText`.
- Commands: `AlignmentLabelSet` fields, `ALIGNMENTLABELSET SHOW|HIDE GEOMETRY|SPEEDS`, LIST output; `GsIo` save/load.
- Qt `QtAnnotations.cpp`: alignment label drawing split into helpers; text placed and rotated in screen space.
- Architecture check: text content is domain (testable, no Qt); placement stays in the Qt painter layer like the existing
  station text. No new dependency.

## 6. Tests
- `tests/AlignmentLabelTests.cpp` `[req492]`: geometry-point label texts and codes for tangent–curve–tangent; design-speed
  label text in feet and metres; line and spiral label text; label-set items persist and default on; SHOW/HIDE refusal of
  an unknown item.
- `tests/QtEditorTests.cpp`: REQ-502 capture case also saves labelled captures for the panel.

7. Labels are drawing text 0.1" high at the plot scale (REQ-492.5; Civil 3D labels are annotative). Leader and offset
   distances are in text heights, shared with `alignment::LabelledBounds`, so ZOOM EXTENTS frames the labels as
   Civil 3D does (found by the first visual panel, §7).

## 7. Evidence
- Build: `build.bat` Release, exit 0, no warnings.
- Tests: `[alignment]` 149 cases / 3854 assertions pass. Full `ctest --preset ninja-release`: 194/194 pass
  (`build/artifacts/ctest-labels.log`). Qt capture case `issue121Phase6DesignScreenshots` passes and asserts that
  hiding GEOMETRY and labelling segments each change the viewport image.
- Visual panel (`build/artifacts/panel-492/`): round 1 **FAIL** (corroborated blocker: end-point label clipped by
  ZOOM EXTENTS); fixed per item 7; round 2 **PASS** — all medians 1 or 2, no corroborated blocker. Civil 3D side built
  in scratch drawings inside the user's session (closed unsaved).
- Documentation review (REQ-453): `resources/wiki/Alignments.md` and `Commands/alignmentlabel.md` updated;
  `README.md`, `PRIVACY.md` no change needed; public GitHub wiki has no alignment pages (still pending for the whole
  epic, as recorded on PR #1152).

## 8. Follow-ups and technical debt
1. Overprinting where a major station is close to a geometry point (6+00 by PC, 7+00 by PT); no collision avoidance.
2. Label styles (per-label height, colour, font, prefix) are not modelled; one plotted height and the layer colour.
3. No station-only labels at the begin and end stations.
4. Zero-padded DMS in the drawing-wide angle format (`70°0'0.00"`).
5. `LabelledBounds` estimates text width at 0.7 text heights per character; a wide font could still clip slightly.
