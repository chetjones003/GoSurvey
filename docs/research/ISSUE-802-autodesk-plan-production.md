# Issue #802 — Autodesk Civil 3D behaviour for Plan Production (evidence for REQ-517..519)

Date: 2026-11-07. Scope: Create View Frames (#999), Create Sheets (#1000), Create Sample Lines and
Create Section Sheets (#1001). Method: official Autodesk help and tutorials, plus state DOT procedures that
follow Civil 3D. The Civil 3D application is not installed on this machine, so nothing here was reproduced in
the product. Anything the sources do not state is recorded as an assumption in the REQs, not as Autodesk behaviour.

## What the sources support

- **View frames run along the alignment.** Frames are normally placed along the alignment for the most efficient
  use of paper. A match-line rounding option moves match lines and increases overlap. Frames are created from a
  station range, and the first frame can start before the alignment start.
  Sources: Autodesk 2026 tutorial "Exercise 2: Creating View Frames"
  (https://help.autodesk.com/cloudhelp/2026/ENG/Civil3D-Tutorials/files/GUID-E8788617-0B22-45D9-9930-46C2A7CBC720.htm);
  Autodesk user guide "To Create View Frames for Plan/Profile Sheets"
  (https://help.autodesk.com/cloudhelp/2024/ENG/Civil3D-UserGuide/files/GUID-879A407F-9F9F-49F3-842D-C6A1CA57AFD1.htm);
  Wisconsin DOT plan/profile sheet guide (https://c3dkb.dot.wi.gov/Content/c3d/pln-prod/pln-prod-pln-prof.htm).
- **Scale comes from the sheet template, not a frame-size field.** The 2026 wizard takes scale from the sheet template
  chosen; the tutorial uses a 40-scale ANSI D plan and profile template. The frame placement also depends on the profile
  view style and band set chosen in the wizard.
  Same sources as above.
- **Sheets come from a view frame group and a template.** The Create Sheets wizard asks for a view frame group and a
  range, and uses a template whose title blocks and viewports are already set up. Layouts can be one per drawing or many.
  Sources: Autodesk user guide "View Frame Group and Layouts Page (Create Sheets Wizard)"
  (https://help.autodesk.com/cloudhelp/2015/ENG/Civil3D-UserGuide/files/GUID-2A3CB69E-5557-4FAE-A405-A7D9E97CA690.htm,
  2015 documentation); Montana DOT plan sheet procedure (https://mdt.mt.gov/other/webdata/external/esdc/library/RW09_PlanSheets.pdf).
- **Sample lines come before section sheets.** Sample lines are placed perpendicular to the alignment by a station range
  at a set interval (Home → Profile & Section Views → Sample Lines, By Station Range). Create Section Sheets (Output tab) is
  built from the alignment, pattern lines and sections.
  Sources: Autodesk "Sections Command Reference"
  (https://help.autodesk.com/cloudhelp/2024/ENG/Civil3D-UserGuide/files/GUID-EDC77EEC-1DE4-4FD9-8BC0-87FE67CCDFEB.htm);
  Montana DOT Procedure XSinAdesk (https://www.mdt.mt.gov/other/webdata/external/esdc/library/Procedure-XSinAdesk.pdf);
  Wisconsin DOT "Add section view to existing group" (https://c3dkb.dot.wi.gov/Content/c3d/xsec/xsec-add-sctn-vu.htm).
- **Copied sample lines keep their labels.** Station names must be updated by hand (Wisconsin DOT, as above).

## What the sources do not support (left as open choices)

- No default frame size, overlap or sample-line interval value was found in the sources. The REQs leave these to the user.
- Sections per sheet and the section axis elevation are set by local template standards, not by Autodesk defaults.

## How the REQs use this

- REQ-517 frame length = sheet width x scale; overlap and placement as in the tutorial's along-alignment method. Scale and
  size are typed in the dialog rather than taken from a template, which departs from Civil 3D and is an open choice.
- REQ-518 one layout per frame, built from a template layout in the drawing; the Civil 3D sheet-set file is not reproduced.
- REQ-519 sample lines by station range at an interval; sections sampled from the TIN; section sheets as paper-space geometry.
