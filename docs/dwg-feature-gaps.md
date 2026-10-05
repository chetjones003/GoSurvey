# DWG feature gaps

What GoSurvey's DWG **save** and **open** still leave out for **AutoCAD, Civil 3D and other programs** (the ordinary DWG stream — not the ADR-044 trailer GoSurvey reads on reopen).

Tracking issue: **#601**. Evidence is from `beta` at **`dfbcd13a`** (2026-10-05).

## The short version

GoSurvey saves DWG as **R2000 by default** and can export **R2004 through R2018** (D-2026-10-01-f, #600 closed). It opens R13–R2018. When GoSurvey reopens its own DWG it also reads the private trailer, so a GoSurvey user often sees a full drawing. **Everyone else only sees the DWG file itself.**

As of 2026-10-05, the original **Group A** inventory and **Group B** features through annotative scaling, fields, and dynamic blocks (#602–#622, #617, #618) are **shipped on `beta`**. The export dialog builds its loss list from the actual drawing (`ComputeDwgExportLosses` in `LibreDwgCad.cpp`, modal in `CadUi_Modals.cpp`, issue #614).

**Still tracked under #601:**

| Issue | What is left |
|---|---|
| **#688** | AutoCAD annotation **context** objects — split from closed [#622](https://github.com/chetjones003/GoSurvey/issues/622); LibreDWG / SPEC GAP (see [#688](https://github.com/chetjones003/GoSurvey/issues/688)). |
| **#624** | Materials and lights — **SPEC GAP**; **visual styles (increment 1)** shipped as REQ-371 (paper VIEWPORT `VISUALSTYLE` at R2007+). |

Close **#601** only when those two are resolved or explicitly moved off this tracker.

## Shipped on `beta` (child issues closed)

### Group A — R2000-capable fidelity

| Issue | Topic | Notes |
|---|---|---|
| #602 | Tilted ARC/CIRCLE/polyline extrusion (#435–#437) | On `beta` via #626 |
| #603 | 3D polylines / feature lines | REQ-057 / REQ-170 |
| #604 | TEXT/MTEXT encoding, rotation, style, height | |
| #605 | Survey points and labels in DWG | REQ-365 |
| #606 | Blocks, INSERT, attributes | REQ-107 |
| #607 | DIMENSION save/open | REQ-366 |
| #608 | HATCH save/open | Gradients need R2004+ export |
| #609 | Lineweight | |
| #610 | Paper-space layouts and viewports | REQ-037 / REQ-155 |
| #611 | Meshes / TIN surfaces | ADR-026 policy |
| #612 | 3D solids / pipe runs | ADR-045 policy |
| #613 | Former open skips (SPLINE, LEADER, …) | |
| #614 | Dynamic export warning + save log | REQ-170 / REQ-201 |

### Group B — newer DWG format (R2004–R2018 export path)

| Issue | Topic | Notes |
|---|---|---|
| #600 | R2004+ container / export versions | LibreDWG 0.14 vendor + encode fixes |
| #615 | True 24-bit colours | When export ≥ R2004 |
| #616 | TABLE objects | |
| #619 | Multileaders / leaders | REQ-367 |
| #620 | Transparency | R2010+ export |
| #621 | Point cloud external references | R2013+ |
| #623 | GEODATA layout (local vs projected) | REQ-362 |
| #617 | Live fields on TEXT/MTEXT + DWG `FIELD`/`FIELDLIST` | REQ-368 — PR **#677** |
| #618 | Dynamic blocks MVP (`*U` INSERT, linear/flip) | REQ-369 — PR **#678**; optional follow-ups in REQ-369 |
| #622 | Annotative scaling (GoSurvey-native + EED) | Closed 2026-10-05 — PRs **#657–#687**; context parity → **#688** |

Some export paths still **degrade** rather than drop (for example 3D polylines with bulge, multileader extra branches on R2000/R2004). Those appear in the dynamic loss list when they apply.

## Remaining gaps (detail)

### #688 — AutoCAD annotation context (split from #622)

GoSurvey-native annotative scaling shipped in **#622** (closed): CANNOSCALE UI, `.gs` SCALE list, viewport/model annotative draw, multi-SCALE DWG export/import, GOSURVEY / AcadAnnotative EED, per-scale visibility (`annoVisScales`), UI and `[issue622]` tests (PRs **#657–#687**).

**#688** tracks remaining AutoCAD parity: per-object **annotation context** blobs (`*_ANNOTATION_CONTEXT_DATA`, `CONTEXTDATAMANAGER`), native DIMENSION annotative fields where LibreDWG lacks them, and export-loss honesty when context cannot be preserved. **REQ-110** remains **proposed**; context work needs an accepted REQ or a recorded SPEC decision.

### #624 — Materials and lights (visual styles increment 1 shipped)

**REQ-371 (2026-10-05):** paper-space VIEWPORT entities carry GoSurvey `VisualStyle` through `.gs` and
R2007+ DWG (`LibreDwgVisualStyle.cpp`) — AutoCAD `VISUALSTYLE` dictionary handles on export, import maps
back to 2D Wireframe / Hidden / Shaded.

**Still open under #624:** `MATERIAL`, `LIGHT`, `SUN`. **SPEC GAP** until REQs exist for those slices.
Model and paper viewport visual styles ship under REQ-371 (R2007+ DWG + `.gs`).

## Shipped detail (#617, #618)

### #617 — Fields (closed, REQ-368)

Live TEXT/MTEXT fields (area, length, survey point coords, filename/date/layout) with inline `%<…>%`
wires, viewport evaluation, and R2004+ DWG export of hand-built `FIELD` / `FIELDLIST` objects
(`LibreDwgField.cpp`, PR **#677**). Civil sheet-set / view fields remain out of scope.

### #618 — Dynamic blocks (closed MVP, REQ-369)

`*U` INSERT fidelity, foreign display via baked `*U` geometry, GoSurvey linear/stretch (and flip)
export/import, grip re-evaluation, GoSurvey↔DWG linear parameter round trip, and R2004+ `#614` export
loss lines (`LibreDwgDynamicBlock.cpp`, PR **#678**). Optional follow-ups: visibility/lookup encoders,
stretch entity association handles, and manual Save → AutoCAD → Save → GoSurvey for flip/visibility/lookup.

## Why exports target R2018

DWG versions cluster on a few containers: R2000; R2004/R2010/R2013/R2018 share one family (R2007 is a dead end for writing). With #600 resolved, GoSurvey exports **R2018** for current AutoCAD compatibility and fills format-specific fields (true colour, transparency, multileaders, annotative data) when the user picks a new enough version in the export dialog.

## Where the evidence lives

- DWG writer and reader: `src/io/LibreDwgCad.cpp` (`FillFromState`, `ImportObject`, `ComputeDwgExportLosses`, `TableWriter`)
- Save warning and format picker: `src/ui/CadUi_Modals.cpp` (`DrawDwgLossyExportModal`)
- LibreDWG (in-tree, D-2026-09-30-d): `third_party/libredwg/`; class support in `src/classes.inc`
- DXF reference implementation for several entity classes: `src/io/DxfIo.cpp`

## Historical recommended order

The 2026-09-30 order in #601 ( #602 → #614 → Group A → #600 → Group B features ) is **complete except** **#688** and **#624** in the table at the top of this document. New DWG fidelity work should target those issues after their REQs exist (**#688** needs an accepted REQ or SPEC decision; **#624** is still a SPEC GAP).
