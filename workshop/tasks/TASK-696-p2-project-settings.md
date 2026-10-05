# TASK — Projects P2: project settings (issue #696)

- Branch: `feat/projects-p2-settings`
- Authority: **REQ-375** (+ REQ-374 clause 1 for New Project / File > New); D-2026-10-05-d,
  **D-2026-10-05-e** (which settings are which — answered by the user, option 1); GitHub #696 (P2)
- Boundary check: pure parse/write/resolve in `src/commands/ProjectSettings.{hpp,cpp}` (domain, no UI);
  the trailer JSON for a `DrawingSettings` is now one pair of functions in `src/io/GsIo.cpp` shared by the
  drawing and the project defaults (two concrete uses); the window is the existing Drawing Settings window
  in a "project mode" (reuses its Units and Zone and Object Layers tabs); glue in `CadUi_Projects.cpp`.
  No new dependency (REQ-300).

## Delivered

1. `DrawingSettings::overridden` (bit per `ProjectDefaultKey`), saved as `drawingSettings.overridden`.
2. Project settings in the `.gsproj` `settings` object: `linearUnits`, `zone`, `defaults`, `plotScale`;
   unknown members kept.
3. `EnforceProjectSettings` (every frame): every project tab — shown or not — gets the project's zone and
   unit, and each inherited default it has not overridden. Each replaced zone/unit is logged.
4. Project Settings window: `PROJECTSETTINGS` command + File > Project Settings…; same window as Drawing
   Settings, Units and Zone + Object Layers tabs, OK/Apply writes the `.gsproj` atomically.
5. Drawing Settings in a project drawing: units and zone read-only ("Enforced by project"); each default
   shows Inherited / Overridden with **Reset**. UNITS dialog unit combo disabled when the project fixes it.
   Status-bar scale and `PLOTSCALE` mark/unmark the plot-scale override.
6. New Project dialog takes the linear units, then opens Project Settings for the coordinate system.
   File > New while a project drawing is active joins that project (REQ-374 clause 1).

## Decisions / assumptions (recorded)

- Which settings are enforced / inherited / the drawing's own: **D-2026-10-05-e**.
- ASSUMPTION (REQ-375 clause 3a): a project with no zone / no unit set enforces none, and one with no
  defaults supplies none — so projects made in P1 do not rewrite their drawings. Nothing in the issue says
  otherwise; it is the conservative reading.
- ASSUMPTION: the New Project dialog collects the unit, then Project Settings opens for the coordinate
  system (the zone picker is large and already lives in that window), rather than embedding a second
  copy of the picker in the dialog.
- ASSUMPTION: opening a drawing whose own zone/unit differs from the project's replaces them in memory
  and reports it; the file is not touched until saved. Add-Drawing-time blocking is P5 (REQ-378 cl. 5).

## Technical debt

- The project's `defaults` JSON carries a whole `DrawingSettings` (marker, transform, map…) although
  only six settings are read from it. Harmless; trimming needs a second serializer.
- Save for a project drawing does not yet default to `Drawings/` (not part of REQ-375).
- A replaced plot scale marks the drawing modified (it is a visible change); a replaced zone/unit does not.

## Tests

- `GoSurveySnapTests "[req375]"` (`tests/ProjectSettingsTests.cpp`, 8 cases): empty project enforces
  nothing; round trip + unknown member kept; zone/unit enforced (marker + transform reset with a replaced
  zone); inherited follows the project, overridden does not, Reset re-inherits; no defaults → nothing
  inherited; overrides survive the trailer JSON; EnforceProjectSettings on the active tab, a hidden tab
  and a standalone drawing; plot-scale override marking.
- GUI (window layout, tags, dialogs) is not unit-testable; checked by building the app.
