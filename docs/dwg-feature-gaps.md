# DWG feature gaps

What GoSurvey's DWG **save** and **open** leave out today, and what would fix each gap.
Tracking issue: **#601**. Evidence is from `beta` at `5c7bbcf1` (2026-09-30).

## The short version

GoSurvey saves DWG as **R2000 by default** and can export **R2004 through R2018** (D-2026-10-01-f); it opens every version from R13 to R2018. When GoSurvey reopens its own DWG it reads its private copy of the drawing (the ADR-044 trailer), so nothing looks lost to a GoSurvey user. **AutoCAD, Civil 3D and every other program only see the ordinary DWG part**, and a lot is missing from it.

The gaps fall into two groups:

- **Group A: fixable now, in the R2000 format.** DWG has had a place for all of these since 2000. We just don't write (or read) them yet. Several are bugs against accepted requirements.
- **Group B: need a newer DWG format.** R2000 has nowhere to store some of these. Export through **R2018** is shipped (#643, D-2026-10-01-f); remaining rows are feature work or import gaps.

Some group B items are also **new GoSurvey features** (fields, dynamic blocks, multileaders, annotative scaling, visual styles/materials/lights). They need a requirement written first (a SPEC GAP), then the DWG support.

## Group A: fixable in R2000

| Issue | Gap | Save | Open | Spec status |
|---|---|---|---|---|
| #602 | Tilted arc/circle fixes (#435/#436/#437) are on `master` but not `beta` | ✗ | ✗ | covered (REQ-312/325) |
| #603 | Polylines saved flat at Z=0; feature lines dropped | ✗ | — | covered (REQ-057/170); feature-line form needs a decision |
| #604 | Text: `°` and other special characters garbled; rotation, justification, style, height not written | ✗ | — | covered (REQ-170/044) |
| #605 | Survey points and labels not written at all | ✗ | — | representation needs a decision |
| #606 | Blocks, inserts, attributes | ✗ | exploded | covered (REQ-107 requires it) |
| #607 | Dimensions | ✗ silent | ✗ | covered (REQ-170) |
| #608 | Hatches (gradients need R2004) | ✗ | ✗ | covered (REQ-170 acceptance names HATCH) |
| #609 | Lineweight | ✗ | ✗ | covered (REQ-170) |
| #610 | Paper-space layouts and viewports | ✗ | ✗ | covered (REQ-037/155/170) |
| #611 | Meshes and TIN surfaces | ✗ | — | needs a decision (ADR-026 (c)) |
| #612 | 3D solids and pipe runs | ✗ | — | needs a decision (ADR-045 (i)) |
| #613 | Open skips SPLINE, LEADER, 3DFACE, polyface, 2D SOLID, elliptical arcs, POINT, IMAGE | — | ✗ | covered; new entity types need REQs |
| #614 | Save warning is a fixed, partly wrong list; several drops are silent | ✗ | — | covered (REQ-170/201) |

## Group B: need a newer DWG format (after #600)

| Issue | Gap | Minimum format | New GoSurvey feature? | LibreDWG support |
|---|---|---|---|---|
| #615 | Exact 24-bit colours (save rounds to AutoCAD's 255) | R2004 | no | fields exist; blocked only by #600 |
| #616 | Tables | R2004 | no (GoSurvey has tables) | TABLE debugging-level, no add API |
| #617 | Fields (live text) | R2004 | **yes**, SPEC GAP | FIELD stable |
| #618 | Dynamic blocks | R2004 | **yes**, roadmap Someday (REQ-107) | mixed; many parameters unstable |
| #619 | Multileaders and leaders | R2007 (target R2010+) | **yes** — REQ-367 shipped on `beta` (#619 closed) | MULTILEADER stable; hand-built export |
| #620 | Transparency | R2010 | no | fields exist; blocked by #600 |
| #621 | Point clouds saved as a link to the scan file | R2013 | no (GoSurvey has clouds) | unstable; AutoCAD attaches only `.rcp`/`.rcs` |
| #622 | Annotative scaling | R2007 (target R2010+) | **yes**, SPEC GAP — SCALE list import/export (multi-SCALE hand-build) + GOSURVEY CANNOSCALE EED + status-bar picker + model/layout display + `.gs` index sync + model/paper/nested INSERT + GOSURVEY XDATA annotative on INSERT/TEXT (#657–#673); ACAD scale dictionary + per-scale visibility + native TEXT/MTEXT context open | SCALE stable; context data unstable; no `dwg_add_SCALE` |
| #623 | Newer GEODATA layout (local grid vs projected) | R2010 | no | stable |
| #624 | Visual styles, materials, lights | R2007 (target R2010+) | **yes**, SPEC GAP | VISUALSTYLE stable |

## Why group B points at R2018, not R2004

DWG has changed format only a handful of times: R13, R14, R2000, R2004, R2007, R2010, R2013 and R2018 (AutoCAD 2018 through 2027 all save R2018).

- **R2004, R2010, R2013 and R2018 share one file container.** Once LibreDWG can write that container (the crash in #600), the rest of those versions are mostly a matter of which object fields to fill in.
- **R2007 uses a different container of its own**, which LibreDWG can barely write. Skipping it costs nothing: anything that needs R2007 (multileaders, annotative scaling, visual styles) also exists in R2010+.

So the practical path for group B is: fix the shared container → save **R2018** (what current AutoCAD uses natively) → add each feature. Choosing the target version is part of the decision #600 needs.

## Recommended order

1. **#602**: get the already-written tilted-curve fixes into `beta`. Small and mechanical.
2. **#614**: an honest save warning, so users know what is lost while the rest is fixed.
3. **Group A by how much surveyors notice:**
   1. #605 survey points
   2. #603 elevations
   3. #604 text
   4. #606 blocks
   5. #607 dimensions
   6. #608 hatches
   7. #609 lineweight
   8. #610 layouts
   9. #613 open-side types
   10. the decision items #611 and #612
4. **#600**: the newer-format container (decision + LibreDWG fix), then #615 colours, #620 transparency, #616 tables, #623 GEODATA, #621 point clouds.
5. **New features**, each after its REQ is written: #619 multileaders, #617 fields, #622 annotative, #618 dynamic blocks, #624 visual styles/materials/lights.

## Where the evidence lives

- DWG writer and reader: `src/io/LibreDwgCad.cpp` (`FillFromState`, `ImportObject`, `TableWriter`)
- Save warning: `src/ui/CadUi_Modals.cpp` (`DrawDwgLossyExportModal`)
- LibreDWG (our in-tree copy, D-2026-09-30-d): `third_party/libredwg/`; class support levels in `src/classes.inc`
- DXF writer, which already handles several of these (blocks, dimensions, hatches, points, true colour): `src/io/DxfIo.cpp`
