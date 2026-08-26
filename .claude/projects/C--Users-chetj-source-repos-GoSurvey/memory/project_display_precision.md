---
name: project_display_precision
description: Display precision is centralized in NumFormat.hpp; UNITS dialog is the planned follow-up feature
metadata:
  type: project
---

User-facing coordinate/length readouts now share a single configurable display
precision via `src/util/NumFormat.hpp` (`FormatLinear`, `DisplayFloatFmt`,
`DisplayPrecisionClamp` — pure, UI-free, unit-tested in `tests/NumFormatTests.cpp`).

Two settings on `AppCommandState`:
- `displayLinearPrecision` (default 4) — all non-survey readouts (status bar, ID,
  INVERSE, dimensions, line/circle/annotation properties, snap tooltip, PDF insert,
  ALIGN dialog). Editable in Settings → Display → "Coordinate precision".
- `surveyPointDisplayPrecision` (default 4) — survey labels + survey/VIEWPOINTS
  tables. Editable in Settings → User Preferences → Survey points.

Both persist in `gosurvey-user.json` via `UserPrefs.cpp`.

**UNITS feature (REQ-020/021/022, ADR-004 — accepted 2026-06-11):** `UNITS`/`UN`
command opens a Drawing Units dialog (`DrawUnitsDialog` in CadUiSettings.cpp).
- Phase 1 (done): Length group owns `displayLinearPrecision` (removed the interim
  Display-tab control); Sample Output; OK persists, Cancel/[X]/Esc revert.
- Phase 2 (done): pure `src/util/AngleFormat.hpp` (`FormatBearing`,
  `FormatSweptAngle`, `AngleDisplaySettings`; tested in `tests/AngleFormatTests.cpp`
  incl. default-parity guard). Angle settings on AppCommandState:
  `angleDisplayType` (0 DD,1 DMS,2 Surveyor), `angleDisplayPrecision`,
  `angleDisplayClockwise`, `angleDisplayBaseDeg`; helper `CadAngleDisplaySettings(st)`.
  Threaded through INVERSE, angular dimensions, line/annotation rotation readouts,
  ALIGN dialog rotation, Sample Output. Display-only — angle ENTRY keeps CW-from-north.
- Phase 3 (done): drawing unit = AutoCAD INSUNITS relabel (REQ-022 amended
  2026-06-12). `drawingInsUnits` (code 0=Unitless/2=Feet/6=Meters) on
  AppCommandState; document property persisted in .gs (GsIo, no version bump,
  default 2) and written to/read from the DXF `$INSUNITS` header. RELABEL ONLY —
  never scales geometry; DXF import reads $INSUNITS without scaling so coordinates
  stay 1:1 (REQ-002). Combo in the UNITS dialog; changing it bumps cadGpuRevision
  (marks drawing dirty). Chosen over ft↔m conversion because a scalar rescale
  breaks SPCS state-plane georeferencing.
Deferred from REQ-021: angle INPUT convention (ANGBASE/ANGDIR-style), the ALIGN
saved report, and the Traverse editor bearings keep their existing formatting.

**Deliberately NOT precision-controlled:** SurveyCsv export and the Helmert/ALIGN
saved report (`CadCommands.cpp` ~10400) — those are data/report artifacts, not live
readouts (REQ-002 round-trip fidelity / fixed column widths).

**Known debt:** coordinate edit fields (InputFloat/InputDouble) reformat to the
display precision, so committing an edit can round the stored value to that
precision. User accepted this; revisit if a separate display-vs-edit precision is
wanted (the UNITS feature is the natural place). Existing dimensions bake their
text and only re-pick up precision on regen/edit, not when the setting changes.
