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

## 8. Follow-up increment (2026-11-04, user choices "2 b and d", "3a", "4a only if all phases are done")
Authority: REQ-502 (marker), REQ-492.5 (label styles); decisions D-2026-11-04-d and D-2026-11-04-e.
- **Marker (D-2026-11-04-d):** amber warning triangle with "!" centred on each failing entity, one label text height on
  paper, never under 10 px; replaces the 4-unit red flag.
- **Label styles (D-2026-11-04-e):** `AlignmentLabelStyle` (text style, plotted height, colour), built-in Standard,
  `ALIGNMENTLABELSTYLE LIST|NEW|DELETE|SET`, `ALIGNMENTLABELSET STYLE, <set>, <item>, <style>` for MAJOR, GEOMETRY,
  SPEEDS, EQUATIONS, SEGMENTS and OFFSETS; saved in the drawing JSON; missing references fall back to Standard with a
  log line; undo snapshots and per-tab documents carry the table. SHX text styles are stroked (`romans.shx` by
  default); characters the font lacks use a TrueType face of the same cap height.
- **Placement (D-2026-11-04-e):** screen boxes (`LabelBoxes`); leaders first, then segment labels, then station text,
  each at its first free side and row; labels avoid the warning marker; the off-line curve PI cross is no longer drawn.
  `alignment::LabelledBounds` now takes per-item heights and a nine-height margin for moved stacks.
- Tests: `[alignment]` 151 cases / 3898 assertions (label-style command refusals, delete-in-use, undo, save/reload,
  orphaned style, labelled bounds by style). Qt capture case adds a red 0.12" label-style capture.
- Evidence: Release build exit 0, no warnings; full `ctest --preset ninja-release` 194/194
  (`build/artifacts/ctest-labelstyles.log`); panel `build/artifacts/panel-492b/` round 1 FAIL (bearing over station text,
  3 of 3), round 2 PASS (all medians 1–2, one uncorroborated blocker).
- Documentation (REQ-453): `resources/wiki/Alignments.md`, `Command-Reference.md`, `Commands/alignmentlabel.md`, new
  `Commands/alignmentlabelstyle.md`; public GitHub wiki: new Alignments page, Command Reference rows, sidebar and Home
  (wiki commit `9f609b5`); gosurveycad.com: new `docs/alignments.html`, sidebar on every docs page, Command Reference
  rows, docs home and sitemap (site commit `fc54314`, live via the Pages production deploy). `README.md`, `PRIVACY.md`:
  no change. `tools/docsgen/generate.js` lists Alignments; its page template is older than the live site's (the site's
  pages were edited by hand after generation), so the site was updated by targeted edits, not a full regeneration.
- Phase audit (4A gate): REQ-480..503 audited against code and tests on 2026-11-04 — 2 DONE (497, 500), 22 PARTIAL. The
  issue's phases are **not** complete, so the beta conflicts were left unresolved and the PR stays out of review, as
  the user asked. Main blockers: Create Alignment dialog and layout toolbar (485), dockable layout editor with
  definition-dependent and Constraint columns (486), dynamic-cursor readout (501.2), foreign-DWG re-read and Civil 3D
  decode (496), PI/PC/PT/centre grips (487), reference point and metadata (480, 483), 100 000-entity timing tests (484,
  499, 503), display cache and layer visibility of the geometry (495), and the REQ-486 AC2/AC3 contradiction.

## 9. Follow-ups and technical debt
1. Dense spots still overlap a little (6+00 by the PC, 7+00 by the PT in the segment-label view); no free box exists at
   that scale.
2. Curve-label stacks crowd at larger heights; the SHX space advance is wide.
3. Label styles have no prefix/suffix, precision, orientation or per-code geometry styles (rest of REQ-492.5 and .1).
4. Ticks and markers keep the alignment style's colour when a label style recolours the text.
5. No station-only labels at the begin and end stations; DMS not zero-padded in the drawing angle format.
6. `LabelledBounds` estimates text width (1.0 text height per character) and a fixed margin.
7. `tools/docsgen/generate.js` template lags the live site (header, nav.js, Studies link, WebP images).
