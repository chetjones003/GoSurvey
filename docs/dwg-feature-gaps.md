# DWG feature gaps

What GoSurvey's DWG **save** and **open** still leave out for **AutoCAD, Civil 3D and other programs** (the ordinary DWG stream — not the ADR-044 trailer GoSurvey reads on reopen).

Tracking issue: **#601** (closed 2026-10-05). Evidence is from `beta` at **`81211cde`** (2026-10-05).

## The short version

GoSurvey saves DWG as **R2000 by default** and can export **R2004 through R2018** (D-2026-10-01-f, #600 closed). It opens R13–R2018. When GoSurvey reopens its own DWG it also reads the private trailer, so a GoSurvey user often sees a full drawing. **Everyone else only sees the DWG file itself.**

As of 2026-10-05, the original **Group A** inventory and **Group B** features through annotative scaling, fields, and dynamic blocks (#602–#622, #617, #618) are **shipped on `beta`**. The export dialog builds its loss list from the actual drawing (`ComputeDwgExportLosses` in `LibreDwgCad.cpp`, modal in `CadUi_Modals.cpp`, issue #614).

**Open tracker issues:** none — **#601** and **#624** closed 2026-10-05 after REQ-385 (PR **#713**). Optional follow-ups (**LIGHTLIST**, sun study, GoSurvey lighting UI) stay listed under **Remaining gaps** below; file a new issue if one is scheduled.

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
| #622 | Annotative scaling (GoSurvey-native + EED) | Closed 2026-10-05 — PRs **#657–#687** |
| #688 | Annotation context (REQ-384) | Shipped 2026-10-05 — PRs **#704–#710** (increments 1–5) |

Some export paths still **degrade** rather than drop (for example 3D polylines with bulge, multileader extra branches on R2000/R2004). Those appear in the dynamic loss list when they apply.

## Remaining gaps (detail)

### #688 — AutoCAD annotation context (split from #622)

GoSurvey-native annotative scaling shipped in **#622** (closed): CANNOSCALE UI, `.gs` SCALE list, viewport/model annotative draw, multi-SCALE DWG export/import, GOSURVEY / AcadAnnotative EED, per-scale visibility (`annoVisScales`), UI and `[issue622]` tests (PRs **#657–#687**).

**#688** (REQ-384, D-2026-10-05-f) — **shipped on `beta`:** R2010+ hand-built per-scale **annotation context** export for **MTEXT**, **TEXT**, **INSERT**, **DIMENSION**, **MULTILEADER**, and simplified **HATCH** scale context; import scan + default-scale **MTEXT**/**TEXT** merge; R2018 re-read round-trip tests; `#614` names pre-R2010 annotative hosts and hatch view/geometry context gaps. Full AutoCAD parity (every `*_OBJECTCONTEXTDATA` variant, extension-dictionary round-trip through LibreDWG, per-scale import merge for all hosts) is not claimed — track regressions via `[issue688][req384]` and export loss, not as an open #601 checklist item unless new gaps are filed.

### #624 — Materials, lights, visual styles

**REQ-371 (shipped):** model + paper viewport `VISUALSTYLE` through `.gs` and R2007+ DWG
(`LibreDwgVisualStyle.cpp`).

**REQ-372 (shipped):** AutoCAD **`MATERIAL`** diffuse RGB on 3D mesh/solid hosts — import shaded
display, R2007+ DWG export/import, `.gs` persistence, mesh round-trip and `#614` honesty for map-only
and **3DSOLID** entity-material encode limits (`LibreDwgMaterial.cpp`, PRs **#697–#702**).

**REQ-385 (shipped):** import capture + R2010+ export for **LIGHT** entities and **SUN**
(`LibreDwgLights.cpp`); `.gs` fields `dwgImportedLights` / `dwgImportedSun`; `#614` loss below R2010.

**Still deferred under #624:** **LIGHTLIST**, **SUNSTUDY**, photometric/web lights, GoSurvey-native lighting.

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

The 2026-09-30 order in #601 ( #602 → #614 → Group A → #600 → Group B features ) is **complete** for tracked MVP slices; **#624** LIGHTLIST/sun-study deferrals remain optional follow-ups. New DWG fidelity work should target newly filed gaps — **REQ-371**, **REQ-372**, and **REQ-385** shipped on `beta`.
