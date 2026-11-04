# TASK-alignment-create-issue121 — Create alignments: dialog, layout toolbar, from objects (REQ-485)

- Type:    feature (remaining REQ-485 statements)
- Status:  done (in review with PR #1152)
- Opened:  2026-11-04
- Owner:   chetjones003 ("Do req 485", 2026-11-04), Claude (implementation)
- GitHub:  #121, PR #1152

## 1. Requirement authority
REQ-485 statements 1–3 and acceptance 1–6; REQ-486 (the layout toolbar opens the Alignment Layout table); REQ-502
(design criteria on the dialog); REQ-087 / REQ-498.3 (feature lines as sources); REQ-201 (named refusals).
Architecture: geometry stays in `src/alignment` (pure); commands parse and report; Qt only collects input and submits
commands, so every path is scriptable and undoable.

## 2. What already exists
`ALIGNMENT <name>, …` (typed metadata), `CREATEALIGNMENTFROMOBJECTS` on the current selection (lines, arcs, polylines
with bulges, feature lines; pick-end direction, REVERSE, FILLET, TOL) and `BuildFromSegments`. Tests cover acceptance
2, 4 and the gap refusal. Missing: the Create Alignment dialog, by-layout drawing and its toolbar, interactive picking
with the Reverse prompt, erasing the sources (and one-step undo), self-overlap / vertical / non-planar / spline
refusals, auto-join within the tolerance, and the ribbon entry (a "not implemented" placeholder).

## 3. Evidence (CLAUDE.md §5)
Civil 3D 2026 on this machine, scratch drawings Drawing6/Drawing7 closed unsaved; captures in `build/artifacts/panel-485/`.
- **Create Alignment - Layout** (`c3d-create-layout-general.png`, `-criteria.png`): Name, Type, Description, Starting
  station; General tab: Site, Alignment style, Alignment layer, Alignment label set; Design Criteria tab: Starting
  design speed, Use criteria-based design (criteria file), Use design check set. OK opens the toolbar.
- **Alignment Layout Tools - <name>** (`c3d-layout-toolbar.png`, menus `c3d-toolbar-menu-*.png`): Tangent-Tangent (No
  curves / With curves) and Curve and Spiral Settings…; Insert / Delete / Break-apart PI; fixed / floating / free line,
  curve, spiral and spiral combinations; Convert AutoCAD line and arc; Reverse sub-entity direction; Delete
  sub-entity; Edit best fit data; Pick sub-entity; Sub-entity editor; Alignment grid view; Undo; Redo. A status row
  shows the current command and "Spiral Type: Clothoid".
- **Curve and Spiral Settings** (`c3d-curve-spiral-settings.png`): Type Clothoid; Spiral in (length, A); Curve (default
  radius, "applies to curve and spirals"); Spiral out (length, A). Default radius 200.00', spirals off.
- **Tangent-Tangent (With curves)** typed PIs (0,0), (0,500), (400,800), (900,800) at R 200 gave tangent 400, tangent
  333.33, curve 185.46, tangent 433.33, curve 128.70 — T = R·tan(Δ/2) at each PI (`c3d-window-layout.png`).
- **Create Alignment from Objects**: "Select the first line/arc/polyline or [Xref]:", "Select lines/arcs or
  polylines to create alignment:", then direction arrows and "Press enter to accept alignment direction or
  [Reverse]:" (`c3d-fromobj-direction.png`). The direction runs from the end nearest the first pick. Then the dialog
  (`c3d-create-from-objects.png`): the layout dialog's fields plus Conversion options — Add curves between tangents
  (checked), Default radius 200.00', Erase existing entities (checked). Sources were erased.
- **Tight corners (contradicts the spec)**: Civil 3D never left a corner as a PI. When R·tan(Δ/2) exceeds 40 % of the
  shorter adjacent tangent it reduces the radius so T = 0.4 × that tangent (e.g. two 100' tangents at 90° → R 40;
  300'/300' → R 120; 250'/600' → R 100; 300'/300' at 30° keeps R 200). REQ-485.2 says such a corner "is reported and
  left as a PI". Not changed here; raised with the user as a recorded decision (§7).

## 4. Plan
Domain (`src/alignment`):
- `BuildFromPis(pis, CurveSettings, out, warnings)`: Fixed tangents between PIs; with curves, a Free circular curve
  at each interior PI (T = R·tan(Δ/2)), or spiral-curve-spiral when spirals are set (T from the clothoid's p and k);
  a corner that does not fit is reported and left as a PI (REQ-485.2 as written).
- `BuildFromSegments`: refuse self-overlapping segments; join gaps within the tolerance (the next entity starts where
  the last one ended).
Commands:
- `ALIGNMENT` / `CREATEALIGNMENT` with no arguments opens the dialog in the Qt editor; typed fields gain `TYPE=`,
  `LABELSET=`, `CRITERIA=`, `SPEED=`.
- `ALIGNMENTLAYOUT <name>[, CURVES|NOCURVES][, FROM=<x> <y>][, DIRECTION=<deg>]`: interactive PI picking (click or
  X,Y; `U` undoes a PI; Enter ends), rebuilt through `BuildFromPis` after every PI, one undo step per session,
  continuing an alignment that already has geometry from its end.
- `ALIGNMENTCURVESETTINGS`: the curve and spiral settings (radius, spiral in/out lengths).
- `CREATEALIGNMENTFROMOBJECTS` with no name: interactive picks, the Reverse prompt, then the dialog (headless: a
  default name). Options `ERASE`, `PICK=<x> <y>`, `TYPE=`, `STYLE=`, `LAYER=`, `LABELSET=`, `CRITERIA=`, `SPEED=`,
  `DESCRIPTION=`. Z ignored and reported; vertical / non-planar segments and splines or other objects refused by name.
  ERASE deletes the sources and adds the alignment in one undo step.
Qt: `EditorAlignmentCreate.{hpp,cpp}` — the Create Alignment dialog (layout and from-objects forms), Curve and Spiral
Settings, and the Alignment Layout Tools window (Tangent-Tangent split button, settings, Delete sub-entity, Grid view,
Undo, Redo, status row). Ribbon Home ▸ Create Design ▸ Alignment becomes a menu (Alignment Creation Tools, Create
Alignment from Objects).

## 5. Tests
Domain: PI layout with and without curves (Civil 3D's numbers above), spirals (C0/C1 at every junction), tight
corner left as a PI, overlap refusal, auto-join. Commands: acceptance 1 (custom metadata, 5+00, LIST), 3 (polyline 3
lines + 1 bulge → 3 tangents + 1 curve, erased), 5 (2-unit gap names gap and both objects, drawing unchanged), 6
(Reverse at the prompt, undo removes the alignment and restores the sources in one step), layout picks / U / Enter,
vertical and spline refusals. Qt: dialog creates and opens the toolbar; toolbar buttons drive the commands; captures.

## 6. Architectural-boundary check
No new dependency. New Kinds (`AlignmentLayout`, `AlignmentFromObjects`) follow the existing Kind/phase pattern and
are routed in `ViewportClickRouteFor`. The Qt dialogs are reached through request flags, like EDITALIGNMENT's dock.

## 7. Decision on tight fillet corners (D-2026-11-04-i)
Tight fillet corners: keep REQ-485.2 ("reported and left as a PI") or follow Civil 3D (shrink the radius so T is 40 %
of the shorter tangent). The user chose option 2 (follow Civil 3D, report the smaller radius) for Create from Objects
only; Tangent-Tangent layout was not checked in Civil 3D and keeps its rule. Implemented in `BuildFromSegments`
(`kMaxTangentShare`); a corner that turns back on itself stays a PI. The test "Create from objects shrinks a fillet
that is too big, as Civil 3D does" replays the seven Civil 3D cases; the Qt test now gets R 40 from the dialog's 200,
as Civil 3D did. Docs updated in the in-app help, the public wiki (753bbc6) and gosurveycad.com (37c1b8a). Also observed: Civil 3D's default
radius is 200' and its dialog checks "Add curves" and "Erase" by default; the spec's 100 ft / 30 m rule is kept, and
the dialog checks both boxes as Civil 3D does.

## 8. Implementation (as built)
- Domain: `alignment::CurveSettings` / `BuildFromPis` (`src/alignment/AlignmentLayout.cpp`); `BuildFromSegments`
  refuses overlaps (`OverlapLength`, `FindSelfOverlap`) and branches, names the smallest gap and both objects, and
  joins gaps within the tolerance.
- Commands (`CadCommands_Alignment.cpp`): `ALIGNMENT` / `CREATEALIGNMENT` KEY=value fields (`CreateOptions`) and the
  dialog request; `CREATEALIGNMENTFROMOBJECTS` rewritten on `GatherSourceSegments` (Z ignored and reported; vertical,
  tilted, 3D-polyline and non-linework refusals), `ERASE` in one undo step, and the interactive Kind with
  first-pick direction and Reverse; `ALIGNMENTLAYOUT` (Kind, PIs, `U`, one undo per session, continuation, starting
  direction); `ALIGNMENTCURVESETTINGS`; `CREATEALIGNMENTENTITIES` alias. Routed in `ViewportClickRouteFor`, dynamic
  input, the Enter/typed/ESC paths, the prompt footer and the rubber band.
- Qt (`EditorAlignmentCreate.*`): both Create Alignment dialogs, Curve and Spiral Settings, and the Alignment Layout
  Tools window; Home ▸ Create Design ▸ Alignment is now a menu (Alignment Creation Tools, Create Alignment from
  Objects).
- Spec: D-2026-11-04-h in `spec/project.md` and REQ-485 statement 4.

## 9. Tests and verification
- `tests/AlignmentCreateTests.cpp` (13 cases): layout with and without curves (Civil 3D's numbers), spiral-curve-
  spiral (hand-computed clothoid T, unequal spirals), tight corners, overlap / branch refusals, auto-join; acceptance
  1, 3, 5 and 6; vertical / 3D polyline / spline refusals and the Z note; interactive picks with Reverse; layout
  picks, `U`, continuation and undo; starting direction. Updated: the gap-message test in `AlignmentTests.cpp` and
  the selection expectation in `AlignmentCommandTests.cpp`. `[alignment]` 175 cases / 4262 assertions pass after
  D-2026-11-04-i; full suite again 194/194 (`build/artifacts/ctest-485b.log`).
- Qt: `issue121CreateAlignmentDialogsAndLayoutTools` passes (dialogs, toolbar, settings, from objects, ribbon).
- Build: `build.bat` Release, no warnings. Full suite `ctest --preset ninja-release` 194/194
  (`build/artifacts/ctest-485.log`).
- Visual panel `build/artifacts/panel-485/`: round 1 **PASS** (every median ≥ 1, no blockers); `RESULT.md`.

## 10. Product documentation review (REQ-453)
- `README.md`: reviewed; one-line alignment summary, no update needed.
- `PRIVACY.md`: reviewed; no data change, no update needed.
- `resources/wiki/`: `Alignments.md` (create rows), `Commands/alignment.md`, `Commands/createalignmentfromobjects.md`,
  new `Commands/alignmentlayout.md` and `Commands/alignmentcurvesettings.md`, `Command-Reference.md`.
- Public wiki: `Alignments.md` and `Command-Reference.md` (commit 2d6ec9d). gosurveycad.com: `docs/alignments.html`
  and `docs/command-reference.html` (category block and index), site commit 94916a0, live and checked.

## 11. Follow-ups
- Check what Civil 3D's Tangent-Tangent (With curves) does at a corner too tight for the radius before deciding
  whether the layout should also shrink it (D-2026-11-04-i covers Create from Objects only).
- Panel non-blocking items: open the toolbar clear of new geometry and show the current tool pressed; dark-theme
  checkbox contrast; Curve and Spiral Settings A value as a read-only field; Help and style edit buttons.
- Civil 3D's other sub-entity layout tools (fixed / floating / free lines, curves and spirals, PI insert / delete /
  break-apart, best fit, sub-entity editor) and the direction arrows at the Reverse prompt are not built.
