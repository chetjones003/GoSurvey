// REQ-359 (GitHub issue #582 increment 3): the Geolocation tab's command layer — when the tab
// exists, Remove Location as one undo step, the geographic marker, and Mark Position's Position
// Marker (D-2026-09-29-e): NGS AG9976 placed at its grid coordinate, the label editor pre-filled,
// and the marker behaving as ONE object for pick / MOVE / COPY / ERASE / UNDO, refused by ROTATE,
// saved in the trailer and kept per drawing tab.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "ViewportPickPolicy.hpp"
#include "geo/CoordinateSystems.hpp"

namespace {

void LoadShippedDictionary() {
  if (!geo::DictionariesLoaded())
    REQUIRE(geo::LoadDictionaries(GOSURVEY_CSMAP_DICTIONARY_DIR));
}

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

double Dms(double d, double m, double s) { return d + m / 60.0 + s / 3600.0; }

// NGS datasheet, PID AG9976 "UNIV OF TEXAS TOWER SEC" (the REQ-358 test's control point):
//   NAD 83(1993) POSITION- 30 17 10.51249(N) 097 44 21.71739(W)
//   SPC TX C -10,077,391.26  3,115,243.14   sFT
const double kLat = Dms(30, 17, 10.51249);
const double kLon = -Dms(97, 44, 21.71739);
constexpr double kNFt = 10077391.26;
constexpr double kEFt = 3115243.14;
constexpr double kOriginX = 3115000.0;
constexpr double kOriginY = 10077000.0;

/// A feet drawing (US survey foot) in Texas Central HARN, US feet, stored about a world origin.
AppCommandState TexasDrawing() {
  LoadShippedDictionary();
  AppCommandState st;
  st.drawingInsUnits = 2;
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  st.worldDocumentOriginX = kOriginX;
  st.worldDocumentOriginY = kOriginY;
  return st;
}

SelectedEntity MarkerSel(int index) {
  SelectedEntity e;
  e.type = SelectedEntity::Type::PositionMarker;
  e.index = index;
  return e;
}

}  // namespace

TEST_CASE("The Geolocation tab follows the zone; Remove Location is one undo step (REQ-359)", "[req359]") {
  std::vector<std::string> log;
  AppCommandState st;
  CHECK_FALSE(GeolocationRibbonTabVisible(st));
  CHECK_FALSE(RemoveGeoLocation(st, log));  // nothing to remove: refused, not a silent no-op

  DrawingSettings s = st.drawingSettings;
  s.zoneCode = "HARN/TX.TX-CF";
  REQUIRE(ApplyDrawingSettings(st, st.drawingInsUnits, st.modelUnitsPerPlottedInch, s, log));
  CHECK(GeolocationRibbonTabVisible(st));
  REQUIRE(SetGeographicMarker(st, 100.0, 200.0, 45.0, log));

  REQUIRE(RemoveGeoLocation(st, log));
  CHECK_FALSE(GeolocationRibbonTabVisible(st));
  CHECK(st.drawingSettings.zoneCode.empty());
  CHECK(st.drawingSettings.markerX == 0.0);
  CHECK(st.drawingSettings.markerY == 0.0);
  CHECK(st.drawingSettings.markerNorthDeg == 90.0);

  // ONE undo brings both the zone and the marker back.
  REQUIRE(DoUndo(st, log));
  CHECK(GeolocationRibbonTabVisible(st));
  CHECK(st.drawingSettings.zoneCode == "HARN/TX.TX-CF");
  CHECK(st.drawingSettings.markerX == 100.0);
  CHECK(st.drawingSettings.markerNorthDeg == 45.0);

  // Choosing No Datum, No Projection in Drawing Settings also resets the marker.
  s = st.drawingSettings;
  s.zoneCode.clear();
  REQUIRE(ApplyDrawingSettings(st, st.drawingInsUnits, st.modelUnitsPerPlottedInch, s, log));
  CHECK(st.drawingSettings.markerX == 0.0);
}

TEST_CASE("Each drawing tab shows the tab for its own zone (REQ-359)", "[req359]") {
  AppCommandState st;
  st.documents.resize(3);
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  SaveDocumentToSnapshot(st, 1);
  st.drawingSettings.zoneCode.clear();
  SaveDocumentToSnapshot(st, 2);
  RestoreDocumentFromSnapshot(st, 1);
  CHECK(GeolocationRibbonTabVisible(st));
  RestoreDocumentFromSnapshot(st, 2);
  CHECK_FALSE(GeolocationRibbonTabVisible(st));
}

TEST_CASE("GEOREORIENTMARKER sets the design point (world) and north (REQ-359)", "[req359]") {
  AppCommandState st = TexasDrawing();
  std::vector<std::string> log;
  Submit(st, "GEOREORIENTMARKER", log);
  REQUIRE(st.active == AppCommandState::Kind::GeoReorientMarker);
  CHECK(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
  Submit(st, "3115010,10077020", log);  // typed coordinates are WORLD (easting, northing)
  SubmitViewportPick(st, 20.0, 20.0, log);  // a click (LOCAL): north along +X
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(st.drawingSettings.markerX == Catch::Approx(kOriginX + 10.0).margin(1e-6));
  CHECK(st.drawingSettings.markerY == Catch::Approx(kOriginY + 20.0).margin(1e-6));
  CHECK(st.drawingSettings.markerNorthDeg == Catch::Approx(0.0).margin(1e-9));
  REQUIRE(DoUndo(st, log));
  CHECK(st.drawingSettings.markerNorthDeg == 90.0);  // back to grid north, one step

  // Not geolocated: refused by name.
  AppCommandState flat;
  Submit(flat, "GEOREORIENTMARKER", log);
  CHECK(flat.active == AppCommandState::Kind::None);
  Submit(flat, "GEOMARKPOINT", log);
  CHECK(flat.active == AppCommandState::Kind::None);
  CHECK(log.back().find("no location") != std::string::npos);
}

TEST_CASE("Latitude/longitude entry and the default label (REQ-359)", "[req359]") {
  double d = 0.0;
  REQUIRE(ParseGeoAngleDegrees("30.5", true, &d));
  CHECK(d == 30.5);
  REQUIRE(ParseGeoAngleDegrees("30 17 10.51249N", true, &d));
  CHECK(d == Catch::Approx(kLat).epsilon(1e-15));
  REQUIRE(ParseGeoAngleDegrees("97\xC2\xB0" "44'21.71739\"W", false, &d));
  CHECK(d == Catch::Approx(kLon).epsilon(1e-15));
  REQUIRE(ParseGeoAngleDegrees("-97 44 21.71739", false, &d));
  CHECK(d == Catch::Approx(kLon).epsilon(1e-15));
  CHECK_FALSE(ParseGeoAngleDegrees("91", true, &d));
  CHECK_FALSE(ParseGeoAngleDegrees("30 61 0", true, &d));
  CHECK_FALSE(ParseGeoAngleDegrees("30E", true, &d));  // a longitude letter on a latitude
  CHECK_FALSE(ParseGeoAngleDegrees("abc", true, &d));
  CHECK_FALSE(ParseGeoAngleDegrees("-97W", false, &d));  // sign and hemisphere together

  CHECK(FormatLatLongLabel(kLat, kLon) ==
        "LAT 30\xC2\xB0" "17'10.51249\"N\nLONG 97\xC2\xB0" "44'21.71739\"W");
  CHECK(FormatLatLongLabel(-0.5, 179.99999999999) == "LAT 0\xC2\xB0" "30'00.00000\"S\nLONG 180\xC2\xB0" "00'00.00000\"E");
}

TEST_CASE("Mark Position > Lat-Long places AG9976 at its grid coordinate (REQ-359)", "[req359]") {
  AppCommandState st = TexasDrawing();
  std::vector<std::string> log;
  Submit(st, "GEOMARKLATLONG", log);
  REQUIRE(st.active == AppCommandState::Kind::GeoMarkLatLong);
  CHECK(ViewportClickRouteFor(st) == ViewportClickRoute::Ignore);  // typed, not picked
  Submit(st, "30 17 10.51249N", log);
  Submit(st, "97 44 21.71739W", log);
  CHECK(st.active == AppCommandState::Kind::None);
  REQUIRE(st.cadPositionMarkers.size() == 1);
  REQUIRE(st.cadPositionMarkerAttrs.size() == 1);
  const CadPositionMarker& m = st.cadPositionMarkers[0];

  // At the point's grid coordinate within 0.001 ft (the projection of the typed lat/long) ...
  const geo::GeoResult grid = geo::LatLongToGrid("HARN/TX.TX-CF", kLon, kLat);
  REQUIRE(grid.ok);
  CHECK(std::abs(m.x + kOriginX - grid.x) <= 0.001);
  CHECK(std::abs(m.y + kOriginY - grid.y) <= 0.001);
  // ... which is the NGS datasheet's grid to the datasheet's own 0.01 ft.
  CHECK(std::abs(m.x + kOriginX - kEFt) <= 0.01);
  CHECK(std::abs(m.y + kOriginY - kNFt) <= 0.01);
  CHECK(m.latitudeDeg == kLat);
  CHECK(m.longitudeDeg == kLon);

  // The MTEXT editor is open on the marker's own label, pre-filled with the latitude/longitude.
  REQUIRE(st.mtextRichEditorOpen);
  CHECK(st.mtextRichEditorMarkerIndex == 0);
  CHECK(st.mtextRichEditorBuf == FormatLatLongLabel(kLat, kLon));
  CHECK(MtextRichEditorTargetAnnotation(st) == &st.cadPositionMarkers[0].label);

  // The user's text becomes the label, and ONE undo still removes the whole placement.
  st.mtextRichEditorBuf = "UT Tower";
  CommitMtextRichEditor(st, log);
  CHECK_FALSE(st.mtextRichEditorOpen);
  CHECK(st.cadPositionMarkers[0].label.text.find("UT Tower") != std::string::npos);
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadPositionMarkers.empty());

  // "lat,long" in one line also works.
  Submit(st, "GEOMARKLATLONG", log);
  Submit(st, "30.28625,-97.7394", log);
  CHECK(st.cadPositionMarkers.size() == 1);
}

TEST_CASE("Mark Position > Point labels the picked point with its latitude/longitude (REQ-359)", "[req359]") {
  AppCommandState st = TexasDrawing();
  std::vector<std::string> log;
  Submit(st, "GEOMARKPOINT", log);
  REQUIRE(st.active == AppCommandState::Kind::GeoMarkPoint);
  CHECK(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
  SubmitViewportPick(st, kEFt - kOriginX, kNFt - kOriginY, log);
  REQUIRE(st.cadPositionMarkers.size() == 1);
  const CadPositionMarker& m = st.cadPositionMarkers[0];
  const geo::GeoResult ll = DrawingPointToLatLong(st, m.x, m.y);
  REQUIRE(ll.ok);
  CHECK(m.latitudeDeg == ll.y);
  CHECK(m.longitudeDeg == ll.x);
  CHECK(std::abs(m.latitudeDeg - kLat) * 110852.0 <= 0.003);  // the sFT datasheet's 0.01 ft
  CHECK(st.mtextRichEditorBuf == FormatLatLongLabel(ll.y, ll.x));

  // GridToDrawingPoint is DrawingPointToGrid's inverse.
  const geo::GeoResult back = GridToDrawingPoint(st, kEFt, kNFt);
  REQUIRE(back.ok);
  CHECK(back.x == Catch::Approx(kEFt - kOriginX).margin(1e-6));
  CHECK(back.y == Catch::Approx(kNFt - kOriginY).margin(1e-6));
}

TEST_CASE("A Position Marker is one object: pick, MOVE, COPY, ERASE, UNDO; ROTATE refuses it (REQ-359)",
          "[req359]") {
  AppCommandState st = TexasDrawing();
  std::vector<std::string> log;
  REQUIRE(PlacePositionMarkerAtLocal(st, 100.0, 100.0, log) == 0);
  CommitMtextRichEditor(st, log);
  const CadAnnotation label0 = st.cadPositionMarkers[0].label;

  // The circle and the label both pick the marker.
  SelectedEntity hit{};
  float d2 = 0.f;
  REQUIRE(PickClosestCadEntity(st, 100.0, 100.0, 0.01f, &hit, &d2));
  CHECK(hit.type == SelectedEntity::Type::PositionMarker);
  const double lx = 0.5 * (label0.boxMinX + label0.boxMaxX);
  const double ly = 0.5 * (label0.boxMinY + label0.boxMaxY);
  REQUIRE(PickClosestCadEntity(st, lx, ly, 0.01f, &hit, &d2));
  CHECK(hit.type == SelectedEntity::Type::PositionMarker);

  // MOVE carries the label with the marker.
  st.selection = {MarkerSel(0)};
  Submit(st, "MOVE", log);
  Submit(st, "0,0", log);
  Submit(st, "5,-3", log);
  CancelActiveCommand(st, log);  // Esc: MOVE keeps asking for a base point
  CHECK(st.cadPositionMarkers[0].x == Catch::Approx(105.0));
  CHECK(st.cadPositionMarkers[0].y == Catch::Approx(97.0));
  CHECK(st.cadPositionMarkers[0].label.boxMinX == Catch::Approx(label0.boxMinX + 5.f));
  CHECK(st.cadPositionMarkers[0].label.boxMaxY == Catch::Approx(label0.boxMaxY - 3.f));

  // COPY duplicates marker + label + attributes; one undo takes it back.
  st.selection = {MarkerSel(0)};
  Submit(st, "COPY", log);
  Submit(st, "0,0", log);
  Submit(st, "0,50", log);
  CancelActiveCommand(st, log);
  REQUIRE(st.cadPositionMarkers.size() == 2);
  CHECK(st.cadPositionMarkerAttrs.size() == 2);
  CHECK(st.cadPositionMarkers[1].y == Catch::Approx(147.0));
  CHECK(st.cadPositionMarkers[1].label.text == st.cadPositionMarkers[0].label.text);
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadPositionMarkers.size() == 1);

  // ROTATE refuses it by name and leaves it where it is.
  st.selection = {MarkerSel(0)};
  Submit(st, "ROTATE", log);
  Submit(st, "0,0", log);
  Submit(st, "90", log);
  CancelActiveCommand(st, log);
  CHECK(st.cadPositionMarkers[0].x == Catch::Approx(105.0));
  bool named = false;
  for (const std::string& line : log)
    named = named || line.find("Position Marker(s) left unchanged") != std::string::npos;
  CHECK(named);

  // ERASE removes marker and label together; UNDO restores them.
  st.selection = {MarkerSel(0)};
  ExecuteDeleteSelection(st, log);
  CHECK(st.cadPositionMarkers.empty());
  CHECK(st.cadPositionMarkerAttrs.empty());
  REQUIRE(DoUndo(st, log));
  REQUIRE(st.cadPositionMarkers.size() == 1);
  CHECK(st.cadPositionMarkers[0].label.text == label0.text);
}

TEST_CASE("Position Markers and the geographic marker survive the trailer and stay per tab (REQ-359)",
          "[req359]") {
  AppCommandState st = TexasDrawing();
  std::vector<std::string> log;
  REQUIRE(PlacePositionMarkerAtLatLong(st, kLat, kLon, log) == 0);
  st.mtextRichEditorBuf = "Line one\nLine two";
  CommitMtextRichEditor(st, log);
  st.cadPositionMarkerAttrs[0].layer = "V-NODE";
  REQUIRE(SetGeographicMarker(st, 12.5, -7.25, 33.0, log));

  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(st), log));
  REQUIRE(back.cadPositionMarkers.size() == 1);
  const CadPositionMarker& a = st.cadPositionMarkers[0];
  const CadPositionMarker& b = back.cadPositionMarkers[0];
  CHECK(b.x == a.x);  // double, bit for bit
  CHECK(b.y == a.y);
  CHECK(b.latitudeDeg == a.latitudeDeg);
  CHECK(b.longitudeDeg == a.longitudeDeg);
  CHECK(b.label.text == a.label.text);
  CHECK(b.label.boxMinX == a.label.boxMinX);
  CHECK(back.cadPositionMarkerAttrs.at(0).layer == "V-NODE");
  CHECK(back.drawingSettings.markerX == st.drawingSettings.markerX);
  CHECK(back.drawingSettings.markerY == st.drawingSettings.markerY);
  CHECK(back.drawingSettings.markerNorthDeg == 33.0);

  // Two tabs, two sets of markers.
  st.documents.resize(3);
  SaveDocumentToSnapshot(st, 1);
  st.cadPositionMarkers.clear();
  st.cadPositionMarkerAttrs.clear();
  SaveDocumentToSnapshot(st, 2);
  RestoreDocumentFromSnapshot(st, 1);
  CHECK(st.cadPositionMarkers.size() == 1);
  RestoreDocumentFromSnapshot(st, 2);
  CHECK(st.cadPositionMarkers.empty());
}
