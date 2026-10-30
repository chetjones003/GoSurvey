# TASK-1010 — Paper-space objects in DWG and R2018/R2010 leader repair

- Issue: #1010 (audit #751 I3)
- Status: PR opened with one open regression (see "Open issue"); not verified for R2010/R2018 leaders
- Base: beta (merged up to 0c92241a, which includes PR #1137 for #993)

## Authority and scope

REQ-113 (accepted; amended by D-2026-10-29-a to include DIMENSION) requires every paper-space object kind in
its set to be written to the AutoCAD-readable part of a DWG. REQ-456 (sheet POINT) and REQ-457 (sheet raster
IMAGE, path-only) were accepted under D-2026-10-29-a. REQ-443 defines spline geometry. REQ-367 defines
multileaders.

This is one PR, by the user's decision, covering every object kind REQ-113 names that is not yet written to
DWG, plus the leader repair the R2018 oracle needs.

## What this PR changes

- **Sheet dimensions:** a sheet dimension (a Dim-kind annotation in the sheet text store) is written as a
  native DIMENSION with its anonymous block. The model-space writer and the sheet writer share
  `WriteDimensionAnnotation`. Anonymous block names skip names already in use.
- **Sheet images (REQ-457):** `CadRasterImage` stores the file path, insertion, size, rotation and pixel size.
  `ProbeRasterImage` and `MakeSheetRasterImage` (src/io/RasterImage.cpp) decode with stb_image and refuse a
  truncated or unreadable file. The IMAGE and IMAGEDEF are written with `dwg_add_IMAGE`; the IMAGEDEF is
  listed in `ACAD_IMAGE_DICT`, and the IMAGE's link to it is a hard pointer. A file missing on import is
  unloaded and logged.
- **Drawing point mode (REQ-456):** the existing `st.pdMode` / `st.pdSize` (set by the PDMODE/PDSIZE commands)
  are read from and written to the DWG header.
- **Sheet splines and points:** beta already stores and imports these (`paperSplines`, `paperPoints`). This
  PR replaces beta's sheet spline writer with the shared spline writer used by model space, so control
  vertices, knots and weights survive a DWG round trip. Beta's sheet point writer is kept.
- **Multileaders (R2010+):** the vendored writer sets ByBlock linetype and lineweight, the AutoCAD class
  application names and proxy flags, an AutoCAD-style preview block, and the arrowhead count and R2010 fields
  that AutoCAD reads. PR #1137 corrected the same MULTILEADER layout from the other side; the merged layout is
  beta's (see `third_party/libredwg/VENDORED.md`, rows 16–18).
- Decisions: D-2026-10-29-a (sheet dimensions, points and images), D-2026-10-29-b (multileader writer accepted
  by AutoCAD; supersedes D-2026-10-01-h's "style handle is null" note).

## Evidence

- Native round trips (no GoSurvey trailer): sheet spline, sheet dimension, sheet point (with PDMODE/PDSIZE),
  sheet image (including missing-file unload and truncated-file refusal). Tests `[issue1010]`.
- AutoCAD 2025 Core Console, `/i` open then query: sheet SPLINE, DIMENSION, POINT and IMAGE counted on
  the `Plan` paper space; the image's IMAGEDEF resolves to its file path with the loaded flag set.
- PR #1137's oracle fixtures (R2000, R2010, R2018 at levels 0–4) open in AutoCAD with exit code 0.

## Verification

- Release build (`build.bat`) on the merged tree.
- Full domain suite (`GoSurveyTests.exe`) on the merged tree.
- AutoCAD checks above on the merged build, R2000 and R2018 (and R2010 with the PR #1137 layout).

## Open issue (must be resolved before merge)

AutoCAD 2025 rejects R2010 and R2018 DWG files that contain a multileader, both on a sheet and in model space
(`MODELLEADER_R2018`, and the level-4 fixtures `L4_R2010` and `L4_R2018`). The same level-4 fixtures open at R2000.
Before the merge with beta these leader fixtures opened; PR #1137's build opens its own level-4 R2010 and R2018
fixtures.

Ruled out by experiment on the merged tree (each one tested alone, in AutoCAD):

- the leader node: flags and dogleg vectors, the node carrying the final point, line point count, line weight
  and arrow size. Beta's node block was restored; none of the variants opens the file;
- the restored proxy graphics ("preview"), switched off and on;
- the class application names for MLEADERSTYLE and MULTILEADER, switched off and on.

Not yet ruled out: the MLEADERSTYLE defaults this PR sets (line type, line weight, text attachment), the
R2010+ layout of the record after beta's changes, and an effect of the sheet content on the same drawing.
The next step is to build PR #1137's commit separately and compare its MULTILEADER and MLEADERSTYLE records
with this branch's output field by field.

## Pre-PR product documentation review (REQ-453)

- `README.md`: reviewed. No paper-space, leader, spline, image or point content, and the DWG description is
  about the build and licence only. No update needed.
- `PRIVACY.md`: reviewed. No data collection or transfer change. No update needed.
- `resources/wiki/` (in-app help): reviewed `Paper-Space-and-Layouts.md`, `Known-Limitations.md`,
  `Import-and-Export.md` and `FAQ.md`.
  - `Paper-Space-and-Layouts.md`: DWG paragraph corrected to name SPLINE, POINT, DIMENSION and IMAGE, the
    R2010+ MULTILEADER rule, image-by-path behaviour, and PDMODE/PDSIZE.
  - `Known-Limitations.md`: the DWG export-losses entry no longer lists multileaders (only R2000/R2004 extra
    branches are lost).
  - `Import-and-Export.md` and `FAQ.md` describe the DWG converter. That wording is stale from before native
    DWG reading and is not affected by this PR; it is recorded for a separate fix.
- Public manual wiki (GoSurvey GitHub Wiki, HEAD `dde790d` before this PR): the same pages were reviewed. The
  public `Paper-Space-and-Layouts.md` and `Known-Limitations.md` receive the same corrections as the in-app
  copies. Its `Import-and-Export.md` and `FAQ.md` carry the same stale converter wording, recorded above.
