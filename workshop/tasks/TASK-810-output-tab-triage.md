# TASK-810 — Output tab controls and export formats triage

- GitHub: #810, follow-up to #762
- Status: partially triaged — PDF Preview (REQ-486) and View Details (REQ-487) are implemented and verified; the remaining controls need independent scope or deferral
- Branch: `chore/triage-paper-space-issues` (combined paper-space batch)

## Issue scope

#810 groups Preview, View Details, Plotter Manager, several Civil 3D export/publish formats,
and an Export to DWF/PDF button with Display and Current choices. The issue says Plot, Batch Plot,
Page Setup Manager, DXF/DWG export, and point export are already wired. It has no acceptance
criteria beyond that inventory and is labeled `needs-triage`.

## Evidence pass

### Repository

- `REQ-029` and `REQ-030` authorize single and batch vector PDF plotting through PDFium.
- `REQ-445` specifies the existing Page Setup behavior; `REQ-485` (issue #1009) adds selected
  CTB/STB tables to those PDF paths. REQ-485 explicitly excludes DWF, raster output, and direct
  system printing. These accepted requirements and their implementation already cover the PDF
  plotting work relevant to #810.
- `ADR-007` selects PDFium vector PDF and defers direct-to-OS-printer output.
- The Output ribbon catalog has Preview, View Details, Plotter Manager, DWF/PDF Export, and
  several export/publish buttons disabled with “not implemented yet” tooltips. Existing PDF and
  batch-plot commands are separate, working entry points.
- #762's parent comment says most civil-design exports depend on #802's data model and third-party
  or proprietary writers. #1000 and #1001 are separate sheet-generation follow-ups and are blocked
  by #999 and #121 respectively.
- No accepted requirement defines Preview interaction, View Details content, Plotter Manager
  responsibilities, DWF output, or PDF Export's Display/Current scope. No TASK-810 record existed.

### Autodesk product documentation

- Autodesk AutoCAD 2025 Help, “About Plot Style Tables”:
  https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Core/files/GUID-2FD01085-DDD8-49D2-A910-E63EA45A7FED.htm
  confirms CTB/STB tables affect plotted appearance and are distinct from output devices. The
  accepted REQ-485 decision has already used this distinction to keep output-device work separate.

### Installed Autodesk products

- AutoCAD 2025 and `accoreconsole.exe` are installed; Civil 3D itself is not installed (only the
  Civil 3D 2025 Object Enabler is present).
- A prior read-only `-PLOT` console probe reached the detailed plot-configuration prompt but was
  cancelled before selecting an output device. It did not establish the missing UI or format
  behavior. No Civil 3D UI comparison can be run on this machine.

## Conclusion and next decision

Evidence confirms that #810 combines already-specified PDF plotting with distinct output-device
and Civil 3D format work. The existing specification cannot determine what Preview, View Details,
Plotter Manager, or the DWF/PDF Display/Current controls should do. Implementing the whole issue
would silently expand REQ-485 beyond its chosen scope. The next safe step is to select one
independently testable slice and record its behavior in SPEC before implementation; the remaining
controls can stay deferred or be split into later issues.

## Follow-up after PDF Preview

- REQ-486 now defines the PDF Preview behavior, and its implementation is recorded in
  `workshop/tasks/TASK-810-pdf-plot-preview.md`.
- REQ-487 now defines View Details as a compact read-only summary of the active layout's effective
  Page Setup. The feature request is confirmed, the implementation plan is approved, and the
  implementation/focused verification are complete; see
  `workshop/tasks/TASK-810-view-details.md`.
- The remaining Output controls still have no accepted behavior. The repository does not support
  PC3 output-device configurations or DWF, and no documented rule defines what GoSurvey's View
  Details or Export Display/Current actions should show or export.
- Autodesk AutoCAD 2025 help confirms that Plot Style Manager operates on CTB/STB files in the Plot
  Styles folder. This does not define GoSurvey's Plotter Manager behavior.
- Civil 3D is not installed here; product interaction cannot settle the remaining UI semantics.
- After REQ-487, the user selected Current-layout PDF Export. REQ-488 defines that slice and the
  implementation is complete; see `TASK-810-export-current-layout-pdf.md`.
- Remaining controls: defer Plotter Manager and DWF/device output while GoSurvey remains PDF-only,
  unless a separate user decision establishes a supported output-device/file-format scope. Any
  additional #810 control needs its own behavior and accepted requirement before implementation.

## Current handoff — 2026-11-04

The user directed continued work on #810. Repository review confirms the remaining candidate slices
are Plotter Manager and Export to DWF/PDF with Display/Current choices. Existing Plot and Batch Plot
already export PDF; Preview now opens the active layout in the PDF viewer. No accepted requirement
defines the remaining controls' behavior, and the prior evidence pass found no installed Civil 3D
capability or supported DWF/PC3 backend to settle that scope. Do not implement these controls until
one independently testable slice is selected and its feature request is accepted.

- D-2026-11-04-d / REQ-488 now defines Current-layout PDF Export through the existing plot pipeline.
  The user explicitly excluded DWF, Display-area export, Plotter Manager/device configuration, and
  changes to the Display/Current controls. The slice is implemented and focused verification passed;
  see `TASK-810-export-current-layout-pdf.md`.
