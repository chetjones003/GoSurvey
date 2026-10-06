# TASK-292 — Geolocation contextual ribbon tab, geographic marker, Position Marker

- Type:    feat
- Status:  review
- Opened:  2026-09-29
- Owner:   Workshop
- GitHub:  #582 (increment 3 of 6)

## Requirement authority

REQ-359 (accepted 2026-09-29, D-2026-09-29-b; item 3 amended by D-2026-09-29-e), REQ-358 (zone,
`DrawingPointToGrid` / `DrawingPointToLatLong`), REQ-357 (Drawing Settings window), REQ-143
(contextual tab pattern), REQ-084 (*not implemented yet*), REQ-101 / local-storage invariant,
REQ-201, ADR-044 (trailer).

## Decision taken during planning

**D-2026-09-29-e** (user, 2026-09-29): REQ-359 item 3 said Mark Position creates "ordinary GoSurvey
geometry", but GoSurvey has no plain node object. The user chose a **Position Marker**: its own
object (cross in a circle + its own MTEXT label), MTEXT editor opens after placement, one object for
select / MOVE / COPY / ERASE / UNDO, node (Survey point) / Center snap at the centre, saved per drawing, ROTATE / SCALE /
MIRROR / STRETCH / grips refused by name, **and also exported into the DWG / DXF body** as CIRCLE +
2 LINEs + MTEXT so other programs see it.

## Plan / architectural-boundary check

- **Commands (data)**: `CadPositionMarker` (CadEntities.hpp: local double x/y, z, lat/long at
  placement, its own `CadAnnotation` label) in `cadPositionMarkers` + `cadPositionMarkerAttrs`
  beside `cadTables`, in `AppCommandState`, `DrawingDocument` (per tab) and
  `DrawingGeometrySnapshot` (undo + BEDIT stash); cleared on import / BEDIT enter.
  `EntityKind::PositionMarker` and `SelectedEntity::Type::PositionMarker` **appended** (ids and
  type values of existing drawings stay stable).
- **Commands (zone)**: `DrawingSettings` gains the geographic marker (`markerX/Y` WORLD design
  point, `markerNorthDeg` CCW from +X; default world origin + grid north = 90°), so it rides the
  existing undo / tab / trailer paths of REQ-357/358. "Drawing origin" is read as WCS (0,0), the
  point GEODATA's design point is expressed in (REQ-362).
- **Commands (new, `CadCommands_Geo.cpp`)**: `RemoveGeoLocation` (one undo step), `GridToDrawingPoint`
  (inverse of REQ-358 item 4), `PlacePositionMarkerAtLatLong` / `...AtLocal`, interactive commands
  `GEOMARKLATLONG` (latitude, then longitude, decimal degrees), `GEOMARKPOINT` (one pick),
  `GEOREORIENTMARKER` (design point, then north direction). Point-pick kinds are added to
  `ViewportClickRouteFor`, the dynamic-input list, the prompt label, the footer hint, typed-point
  entry and cancel (the six places a point command must appear).
- **Integration of the new object**: box select, click pick / hover, COPY, MOVE (translate marker +
  label), ERASE, refuse-by-name helper for ROTATE / SCALE / MIRROR / STRETCH / ARRAY, Survey point + Center snap, MTEXT editor
  target (`mtextRichEditorMarkerIndex`; the post-placement edit shares the placement's undo step).
- **IO**: trailer JSON `positionMarkers` + `positionMarkerAttrs`; `drawingSettings.marker*`.
  DWG (LibreDWG `FillFromState`) and DXF (`DxfIo` export) write CIRCLE + 2 LINEs + MTEXT per marker.
  GoSurvey DWG reopen reads the trailer only, so no duplicates.
- **UI**: contextual **Geolocation** tab (appended after the other contextual tabs, never steals
  focus; falls back to Home if it disappears while active): Location (Edit Location split: icon half
  = Drawing Settings on Units and Zone, label half = menu with Edit Geographic Marker; Reorient
  Marker; Remove Location with a confirmation modal), Tools (Mark Position menu: Lat-Long / Point),
  Online Map (Map ▸ Map Off combo + Capture Area, disabled, *not implemented yet*). Viewport overlay:
  the geographic marker glyph (screen-size) and every Position Marker (plotted size) + label.

## Tests

- `tests/GeolocationTests.cpp` (`[req359]`, GoSurveySnapTests, staged dictionary): geolocated ⇔ tab
  visibility predicate; Remove Location clears zone + marker, one UNDO restores both; refuses when
  not geolocated; NGS AG9976 lat/long → marker at grid within 0.001 ft (US-ft zone, feet drawing,
  non-zero world origin) and label pre-filled; GEOMARKPOINT pick → lat/long round trip; reorient
  stores design point + north; marker MOVE / COPY / ERASE / UNDO as one object; ROTATE refuses it by
  name; trailer JSON round trip; two tabs keep their own markers.
- `tests/LibreDwgCadTests.cpp`: marker survives DWG save → reopen once; the DWG body holds the
  CIRCLE / LINEs / MTEXT (read without the trailer).
- DXF export holds the pieces.
- Dev Shell GUI check of the tab (hand visual checks to the user).

## Status log

- 2026-09-29: planned; spec amended (D-2026-09-29-e).
- 2026-09-29: implemented; "OSNAP Node" reworded in the spec (GoSurvey's node snap is the Survey
  point snap; the marker centre also answers Center).

## Completion report

- Build: `./dev/build` clean; `dev/build-devshell.bat` clean.
- Tests: `[req359]` 9 cases / 135 assertions pass (8 in `tests/GeolocationTests.cpp` + the DWG/DXF
  case in `tests/LibreDwgCadTests.cpp`). `ctest` 1969/1976 — the 7 failures are the headless ones
  already failing on `beta` and recorded in TASK-291 (offset / isolines / surface selection /
  feature-line modify / command-name prompt), unrelated. Dev Shell `req359-geolocation-tab` passes
  in the real window (tab appears without focus, Map / Capture Area disabled, Edit Location opens
  Drawing Settings, Lat-Long marker + editor, Remove Location asks then hides the tab, UNDO).
- Not verified by me: how the marker glyph, label and geographic-marker arrow LOOK — the viewport
  capture holds only the GL layer and the desktop capture returned a stale frame; handed to the user.
- Assumptions: "drawing origin" = WCS (0,0); typed Lat-Long accepts decimal degrees or D M S with
  N/S/E/W (sign or letter, not both); ARRAY also refuses a marker by name (it is neither move, copy
  nor erase); the label is placed up-right of the marker, 22 text heights wide.
- Debt: no MOVE/COPY ghost preview for a marker; no double-click re-edit of a marker label (the
  editor opens only at placement); marker hit test is plan-view (as tables); DXF re-import in
  GoSurvey brings the exported pieces back as loose geometry (accepted in D-2026-09-29-e).
  Pre-existing, noticed: the DXF exporter writes ordinary MTEXT at LOCAL, not world, coordinates.
- Docs: spec (REQ-359 item 3 + acceptance, D-2026-09-29-e); this task.
