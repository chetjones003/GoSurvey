# TASK-1010 — Sheet spline DWG and R2018 multileader repair

- Issue: #1010
- Status: implementation and verification
- Base: beta at 576a8072

## Authority and scope

REQ-113 (accepted, D-2026-10-20-a and D-2026-10-29-a) requires sheet splines in native DWG paper space and an AutoCAD-readable sheet. REQ-443 defines spline geometry. This increment adds the sheet spline store and DWG import/export. The accepted decision D-2026-10-29-a puts dimensions, points, and images in later increments, one PR each.

The R2018 AutoCAD check exposed a separate multileader writer defect that prevented the existing sheet leader from opening. The repair is included so the REQ-113 AutoCAD oracle can cover the sheet with all its existing entities.

## Plan and boundaries

- Domain: add `PaperLayout::paperSplines` and attributes, and move splines from paper import scratch state to the layout.
- IO: share the model spline writer and trailer codec with the paper store. Extend native paper import to SPLINE.
- Vendored LibreDWG: correct R2018 MULTILEADER field order and populate the leader node's last point and dogleg vector.
- Keep model-space spline behavior and R2000 LEADER fallback covered by existing tests.
- No new architectural layer or UI surface.

## Diagnosis and evidence

The spline was absent from the paper store and `ImportPaperEntity` dispatch, so DWG export and import could not retain it on a layout. A native DWG round-trip test exercises the path without the GoSurvey trailer.

For R2018, AutoCAD 2025 opened the control DWG but rejected GoSurvey's isolated multileader with `ErrorStatus=53`. A controlled mutation of AutoCAD's leader showed that changing only its node to `has_lastleaderlinepoint=0` and `has_dogleg=0` reproduced the rejection. AutoCAD also wrote an arrowhead count and the four trailing style fields in R2018, while the vendored spec skipped them; a field-encoded copy of AutoCAD's own leader began opening after those fields were corrected. The corrected GoSurvey isolated R2018 leader and full R2018 sheet now open in AutoCAD.

R2010 still fails for the empty baseline fixture `L0_R2010.dwg`; it is a separate writer defect and not introduced by the sheet content.

## Verification plan

- Native sheet spline save/reopen test checks layout, control vertices, and length.
- The existing multileader API test checks last point and dogleg fields and its DWG round trip.
- Run Release build, targeted tests, complete CTest suite, Qt test, and output layout check.
- Use AutoCAD Core Console on R2000 and R2018 isolated and full sheet fixtures; verify SPLINE and MULTILEADER on the paper layout.
- Review README, PRIVACY, in-app wiki, and public manual wiki before opening the PR.

## Pre-PR product documentation review (REQ-453)

- `README.md`: reviewed. Its general DWG exchange description remains accurate; no update needed.
- `PRIVACY.md`: reviewed. Native DWG entity serialization changes no data collection or transfer; no update needed.
- `resources/wiki/`: reviewed, especially `Paper-Space-and-Layouts.md` and `Import-and-Export.md`. Added the native sheet SPLINE and versioned leader representation to the paper-space guide. The broader import/export page has older converter guidance unrelated to this increment.
- Public manual wiki linked from `gosurveycad.com` (GoSurvey GitHub Wiki, HEAD `dde790d`): reviewed `Paper-Space-and-Layouts.md` and `Import-and-Export.md`. The paper-space page already says paper SPLINE and LEADER round-trip through DWG, so this fix brings the product into line with it; no update needed for this increment.
