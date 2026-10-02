# DWG feature gaps

What GoSurvey's DWG **save** and **open** still leave out for **AutoCAD, Civil 3D and other programs** (the ordinary DWG stream — not the ADR-044 trailer GoSurvey reads on reopen).

Tracking issue: **#601**. Evidence is from `beta` at **`b7503ca9`** (2026-10-02).

## The short version

GoSurvey saves DWG as **R2000 by default** and can export **R2004 through R2018** (D-2026-10-01-f, #600 closed). It opens R13–R2018. When GoSurvey reopens its own DWG it also reads the private trailer, so a GoSurvey user often sees a full drawing. **Everyone else only sees the DWG file itself.**

As of 2026-10-02, the large **Group A** backlog and most **Group B** format work from the original inventory (#602–#616, #619–#621, #623) are **shipped on `beta`**. The export dialog builds its loss list from the actual drawing (`ComputeDwgExportLosses` in `LibreDwgCad.cpp`, modal in `CadUi_Modals.cpp`, issue #614).

**Still tracked under #601:**

| Issue | What is left |
|---|---|
| **#622** | Annotative scaling — most slices merged (#657–#683); AutoCAD parity gaps remain (see [#622 comments](https://github.com/chetjones003/GoSurvey/issues/622)). |
| **#617** | Fields — **REQ-368** (inline codes + evaluation + R2004+ native `FIELD`/`FIELDLIST`). |
| **#618** | Dynamic blocks — **REQ-369** (increment 1: `*U` INSERT fidelity; full round trip open). |
| **#624** | Visual styles, materials, lights — **new feature**, SPEC GAP. |

Close **#601** only when those four are resolved or explicitly moved off this tracker.

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

Some export paths still **degrade** rather than drop (for example 3D polylines with bulge, multileader extra branches on R2000/R2004). Those appear in the dynamic loss list when they apply.

## Remaining gaps (detail)

### #622 — Annotative scaling (in progress)

GoSurvey now has CANNOSCALE UI, `.gs` SCALE list sync, viewport-scaled annotative text/dims/hatches/blocks/multileaders, multi-SCALE DWG export/import, GOSURVEY / AcadAnnotative EED paths for several entity types, and GoSurvey-native **per-scale visibility** (`annoVisScales` / `.gs` `annotativeVisibleScaleNames`) with draw gates in model, layout viewports, and GL solid hatches (PRs **#657–#681**).

**#622 stays open** until remaining AutoCAD-parity items are done or split out — see the checklist in [issue #622](https://github.com/chetjones003/GoSurvey/issues/622) (full annotation context blobs, DIMENSION annotative in native DWG where LibreDWG lacks fields, etc.). REQ-110 remains **proposed**; delivery may amend or supersede it.

### #617 — Fields

REQ-368 delivers live TEXT/MTEXT fields (area, length, survey point coords, filename/date/layout) with
inline `%<…>%` wires, viewport evaluation, and R2004+ DWG export of hand-built `FIELD` / `FIELDLIST`
objects (`LibreDwgField.cpp`). Civil sheet-set / view fields remain out of scope.

### #618 — Dynamic blocks

Parameters, actions, visibility states, DWG round trip. **REQ-369** (accepted): `*U` fidelity, foreign
display, GoSurvey linear/stretch (and flip) export/import, grip re-evaluation, GoSurvey↔DWG round trip for
linear distance, and R2004+ `#614` loss lines for visibility/unsupported/extra-linear/conflicting INSERTs
and stretch actions without entity handles (`LibreDwgDynamicBlock.cpp`). Still degraded or manual: visibility
lookup parameters, per-block multi-linear chains, AutoCAD stretch without entity association handles, and
Save → AutoCAD → Save → GoSurvey for flip/visibility/lookup.

### #624 — Visual styles, materials, lights

3D presentation data beyond REQ-064's current scope. **SPEC GAP**.

## Why exports target R2018

DWG versions cluster on a few containers: R2000; R2004/R2010/R2013/R2018 share one family (R2007 is a dead end for writing). With #600 resolved, GoSurvey exports **R2018** for current AutoCAD compatibility and fills format-specific fields (true colour, transparency, multileaders, annotative data) when the user picks a new enough version in the export dialog.

## Where the evidence lives

- DWG writer and reader: `src/io/LibreDwgCad.cpp` (`FillFromState`, `ImportObject`, `ComputeDwgExportLosses`, `TableWriter`)
- Save warning and format picker: `src/ui/CadUi_Modals.cpp` (`DrawDwgLossyExportModal`)
- LibreDWG (in-tree, D-2026-09-30-d): `third_party/libredwg/`; class support in `src/classes.inc`
- DXF reference implementation for several entity classes: `src/io/DxfIo.cpp`

## Historical recommended order

The 2026-09-30 order in #601 ( #602 → #614 → Group A → #600 → Group B features ) is **complete except** the four rows in the table at the top of this document. New work should target **#622** first for DWG fidelity, then **#617**, **#618**, and **#624** after their REQs exist.
