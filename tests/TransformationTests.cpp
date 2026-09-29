// REQ-360 (GitHub issue #582 increment 4): the Drawing Settings Transformation tab — local ↔ grid
// with a grid scale factor, a sea level factor and a rotation. The pure maths (src/geo/), CS-MAP's
// scale factor against an NGS datasheet, the drawing conversion every grid / lat-long computation
// goes through (Mark Position included), the refusals, undo, the trailer, and the DRAWINGSETTINGS pick.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "ViewportPickPolicy.hpp"
#include "geo/CoordinateSystems.hpp"
#include "geo/LocalGridTransform.hpp"

namespace {

using Tx = DrawingSettings::Transform;
using PickTarget = AppCommandState::DrawingSettingsPickState::Target;

void LoadShippedDictionary() {
  if (!geo::DictionariesLoaded())
    REQUIRE(geo::LoadDictionaries(GOSURVEY_CSMAP_DICTIONARY_DIR));
}

double Dms(double d, double m, double s) { return d + m / 60.0 + s / 3600.0; }

// NGS datasheet, PID AG9976 "UNIV OF TEXAS TOWER SEC", TX/TRAVIS (retrieved 2026-09-29):
//   NAD 83(1993) POSITION- 30 17 10.51249(N) 097 44 21.71739(W)
//   SPC TX C - 3,071,595.000   949,528.007   MT  0.99995905   +1 20 09.8
//   SPC TX C -10,077,391.26  3,115,243.14   sFT  0.99995905   +1 20 09.8
// (the last two columns are the scale factor and the convergence)
const double kLat = Dms(30, 17, 10.51249);
const double kLon = -Dms(97, 44, 21.71739);
constexpr double kNM = 3071595.000;
constexpr double kEM = 949528.007;
constexpr double kNFt = 10077391.26;
constexpr double kEFt = 3115243.14;
constexpr double kDatasheetScaleFactor = 0.99995905;

/// A feet drawing (US survey foot) in Texas Central HARN, US feet: one drawing unit = one zone unit.
AppCommandState TexasFeetDrawing() {
  LoadShippedDictionary();
  AppCommandState st;
  st.drawingInsUnits = 2;
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  return st;
}

/// The hand case: L_ref (1000, 2000) ↔ G_ref (3,000,000, 10,000,000), k = 0.9999 typed, θ = 1°.
Tx HandCase() {
  Tx t;
  t.apply = true;
  t.computation = Tx::Computation::UserDefined;
  t.userScaleFactor = 0.9999;
  t.refLocalX = 1000.0;
  t.refLocalY = 2000.0;
  t.refGridE = 3000000.0;
  t.refGridN = 10000000.0;
  t.rotation = Tx::Rotation::ToNorth;
  t.toNorthDeg = 1.0;
  return t;
}

}  // namespace

TEST_CASE("Local to grid and back, pure maths (REQ-360)", "[req360]") {
  geo::LocalGridTransform t;
  t.refLocalX = 1000.0;
  t.refLocalY = 2000.0;
  t.refGridX = 3000000.0;
  t.refGridY = 10000000.0;
  t.scale = 0.9999;
  t.rotationRad = 1.0 * 3.14159265358979323846 / 180.0;
  // By hand: d = (500, 600); R(1°)·d = (500 cos1° − 600 sin1°, 500 sin1° + 600 cos1°)
  //        = (489.452404, 608.634820); × 0.9999 = (489.403458, 608.573957).
  const geo::XY g = geo::LocalToGrid(t, 1500.0, 2600.0);
  CHECK(g.x == Catch::Approx(3000489.403458).margin(0.0001));
  CHECK(g.y == Catch::Approx(10000608.573957).margin(0.0001));
  const geo::XY back = geo::GridToLocal(t, g.x, g.y);
  CHECK(back.x == Catch::Approx(1500.0).epsilon(1e-9));
  CHECK(back.y == Catch::Approx(2600.0).epsilon(1e-9));

  // Sea level factor: R = 20,906,000 ft, h = 1000 ft.
  CHECK(geo::SeaLevelScaleFactor(20906000.0, 1000.0) == Catch::Approx(0.999952169).margin(1e-9));
  CHECK(geo::SeaLevelScaleFactor(100.0, -100.0) == 0.0);  // no factor at the centre

  // The three ways to state a rotation agree: grid north at local azimuth 30°.
  const double toNorth = geo::RotationFromToNorthDeg(30.0);
  CHECK(geo::RotationFromAzimuthsDeg(30.0, 0.0) == Catch::Approx(toNorth).epsilon(1e-12));
  double fromPoints = 0.0;
  const double s30 = std::sin(30.0 * 3.14159265358979323846 / 180.0);
  const double c30 = std::cos(30.0 * 3.14159265358979323846 / 180.0);
  REQUIRE(geo::RotationFromPoints(0.0, 0.0, 100.0 * s30, 100.0 * c30, 5.0, 5.0, 5.0, 105.0, &fromPoints));
  CHECK(fromPoints == Catch::Approx(toNorth).epsilon(1e-12));
  // …and a local line at that azimuth comes out as grid north.
  geo::LocalGridTransform r;
  r.rotationRad = toNorth;
  const geo::XY north = geo::LocalToGrid(r, s30, c30);
  CHECK(north.x == Catch::Approx(0.0).margin(1e-12));
  CHECK(north.y == Catch::Approx(1.0).epsilon(1e-12));

  // A rotation point on the reference point (local or grid) has no direction: refused.
  CHECK_FALSE(geo::RotationFromPoints(1.0, 2.0, 1.0, 2.0, 0.0, 0.0, 10.0, 0.0, &fromPoints));
  CHECK_FALSE(geo::RotationFromPoints(1.0, 2.0, 5.0, 2.0, 7.0, 7.0, 7.0, 7.0, &fromPoints));

  CHECK(geo::AzimuthDeg(0.0, 1.0) == 0.0);
  CHECK(geo::AzimuthDeg(1.0, 0.0) == Catch::Approx(90.0));
  CHECK(geo::AzimuthDeg(-1.0, 0.0) == Catch::Approx(270.0));
}

TEST_CASE("CS-MAP's scale factor at NGS AG9976 matches the datasheet (REQ-360)", "[req360]") {
  LoadShippedDictionary();
  for (const auto& [code, e, n] : {std::tuple{"HARN/TX.TX-C", kEM, kNM}, std::tuple{"HARN/TX.TX-CF", kEFt, kNFt}}) {
    const geo::GeoResult k = geo::GridScaleFactor(code, e, n);
    REQUIRE(k.ok);
    CHECK(std::abs(k.x - kDatasheetScaleFactor) <= 1e-8);
  }
  CHECK_FALSE(geo::GridScaleFactor("LL84", -97.0, 30.0).ok);  // a lat/long system has none
  CHECK(geo::EllipsoidSemiMajorMeters("HARN/TX.TX-CF").value_or(0.0) == 6378137.0);  // GRS 80

  // Through the drawing: Reference Point computation at G_ref = AG9976.
  AppCommandState st = TexasFeetDrawing();
  Tx& t = st.drawingSettings.transform;
  t.apply = true;
  t.refGridE = kEFt;
  t.refGridN = kNFt;
  DrawingTransformFactors f = ResolveDrawingTransform(st.drawingSettings, st.drawingInsUnits);
  REQUIRE(f.transformOk);
  CHECK(std::abs(f.gridFactor - kDatasheetScaleFactor) <= 1e-8);
  CHECK(f.combined == f.gridFactor);  // sea level factor off
  CHECK(f.spheroidRadiusM == 6378137.0);  // the default: the zone ellipsoid's
  CHECK(f.zoneUnitName == "US Survey Feet");

  // Sea level on: h = 1000 ft with R = 20,906,000 ft typed in meters (US survey foot).
  t.applySeaLevel = true;
  t.elevation = 1000.0;
  t.spheroidRadiusM = 20906000.0 * 1200.0 / 3937.0;
  f = ResolveDrawingTransform(st.drawingSettings, st.drawingInsUnits);
  REQUIRE(f.transformOk);
  CHECK(f.seaFactor == Catch::Approx(0.999952169).margin(1e-9));
  CHECK(f.combined == Catch::Approx(f.gridFactor * f.seaFactor).epsilon(1e-15));
}

TEST_CASE("The drawing conversion goes through the transform only when applied (REQ-360)", "[req360]") {
  AppCommandState st = TexasFeetDrawing();
  st.worldDocumentOriginX = 1000.0;
  st.worldDocumentOriginY = 2000.0;
  st.drawingSettings.transform = HandCase();
  st.drawingSettings.transform.apply = false;

  // Off: grid = world in zone units (feet drawing, US-feet zone: the same numbers).
  geo::GeoResult g = DrawingPointToGrid(st, 500.0, 600.0);
  REQUIRE(g.ok);
  CHECK(g.x == Catch::Approx(1500.0).margin(1e-9));
  CHECK(g.y == Catch::Approx(2600.0).margin(1e-9));
  // …and in a meter zone, world converted to meters with the drawing's foot.
  st.drawingSettings.zoneCode = "HARN/TX.TX-C";
  g = DrawingPointToGrid(st, 500.0, 600.0);
  REQUIRE(g.ok);
  CHECK(g.x == Catch::Approx(1500.0 * 1200.0 / 3937.0).margin(1e-9));
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";

  // On: the hand case, LOCAL (500, 600) = WORLD (1500, 2600).
  st.drawingSettings.transform.apply = true;
  g = DrawingPointToGrid(st, 500.0, 600.0);
  REQUIRE(g.ok);
  CHECK(g.x == Catch::Approx(3000489.403458).margin(0.0001));
  CHECK(g.y == Catch::Approx(10000608.573957).margin(0.0001));
  const geo::GeoResult back = GridToDrawingPoint(st, g.x, g.y);
  REQUIRE(back.ok);
  CHECK(back.x == Catch::Approx(500.0).epsilon(1e-9));
  CHECK(back.y == Catch::Approx(600.0).epsilon(1e-9));

  // Azimuth (local 1° becomes grid 0°) and Rotation point state the same θ: the same grid.
  st.drawingSettings.transform.rotation = Tx::Rotation::Azimuth;
  st.drawingSettings.transform.localAzimuthDeg = 91.0;
  st.drawingSettings.transform.gridAzimuthDeg = 90.0;
  geo::GeoResult a = DrawingPointToGrid(st, 500.0, 600.0);
  REQUIRE(a.ok);
  CHECK(a.x == Catch::Approx(g.x).margin(1e-6));
  CHECK(a.y == Catch::Approx(g.y).margin(1e-6));

  // A meter drawing in the same US-feet zone: the drawing unit is converted before the transform.
  AppCommandState m = TexasFeetDrawing();
  m.drawingInsUnits = 6;
  m.drawingSettings.transform = HandCase();
  m.drawingSettings.transform.refLocalX = 1000.0 * 1200.0 / 3937.0;  // the same ground point, in meters
  m.drawingSettings.transform.refLocalY = 2000.0 * 1200.0 / 3937.0;
  const geo::GeoResult gm = DrawingPointToGrid(m, 1500.0 * 1200.0 / 3937.0, 2600.0 * 1200.0 / 3937.0);
  REQUIRE(gm.ok);
  CHECK(gm.x == Catch::Approx(3000489.403458).margin(0.0001));
  CHECK(gm.y == Catch::Approx(10000608.573957).margin(0.0001));
}

TEST_CASE("Mark Position uses the transform (REQ-360 item 7)", "[req360]") {
  AppCommandState st = TexasFeetDrawing();
  st.worldDocumentOriginX = 3115000.0;
  st.worldDocumentOriginY = 10077000.0;
  std::vector<std::string> log;
  const int plain = PlacePositionMarkerAtLatLong(st, kLat, kLon, log);
  REQUIRE(plain >= 0);
  const double plainX = st.cadPositionMarkers[static_cast<size_t>(plain)].x;

  // Ground = grid scaled about AG9976 and rotated 1°: the marker lands elsewhere in the drawing,
  // and that drawing point converts back to AG9976's grid coordinate.
  Tx t;
  t.apply = true;
  t.refLocalX = kEFt;
  t.refLocalY = kNFt;
  t.refGridE = kEFt;
  t.refGridN = kNFt;
  t.applySeaLevel = true;
  t.elevation = 594.0;  // the datasheet's NAVD 88 height, feet
  t.rotation = Tx::Rotation::ToNorth;
  t.toNorthDeg = 1.0;
  DrawingSettings s = st.drawingSettings;
  s.transform = t;
  REQUIRE(ApplyDrawingSettings(st, st.drawingInsUnits, st.modelUnitsPerPlottedInch, s, log));
  // At the reference point itself the transform is the identity: move the check 1000 ft away.
  const geo::GeoResult near = DrawingPointToGrid(st, kEFt - 3115000.0 + 1000.0, kNFt - 10077000.0);
  REQUIRE(near.ok);
  CHECK(std::hypot(near.x - kEFt, near.y - kNFt) < 1000.0);  // scaled down (k < 1)
  const int placed = PlacePositionMarkerAtLatLong(st, kLat, kLon, log);
  REQUIRE(placed >= 0);
  const CadPositionMarker& m = st.cadPositionMarkers[static_cast<size_t>(placed)];
  const geo::GeoResult grid = DrawingPointToGrid(st, m.x, m.y);
  REQUIRE(grid.ok);
  CHECK(grid.x == Catch::Approx(kEFt).margin(0.001));
  CHECK(grid.y == Catch::Approx(kNFt).margin(0.001));
  CHECK(m.x == Catch::Approx(plainX).margin(0.001));  // AG9976 IS the reference point: same place

  // A point 1000 ft east of the reference, marked by drawing position, gets the transformed lat/long.
  const geo::GeoResult east = DrawingPointToLatLong(st, kEFt - 3115000.0 + 1000.0, kNFt - 10077000.0);
  st.drawingSettings.transform.apply = false;
  const geo::GeoResult eastPlain = DrawingPointToLatLong(st, kEFt - 3115000.0 + 1000.0, kNFt - 10077000.0);
  REQUIRE(east.ok);
  REQUIRE(eastPlain.ok);
  CHECK(std::abs(east.y - eastPlain.y) > 1e-5);  // rotated 1° over 1000 ft: ~17 ft of latitude
}

TEST_CASE("Bad transform input is refused; the transform is one undo step and follows the zone (REQ-360)",
          "[req360]") {
  AppCommandState st = TexasFeetDrawing();
  std::vector<std::string> log;

  DrawingSettings s = st.drawingSettings;
  s.transform = HandCase();
  s.transform.rotation = Tx::Rotation::RotationPoint;
  s.transform.rotLocalX = s.transform.refLocalX;  // coincident with the reference point
  s.transform.rotLocalY = s.transform.refLocalY;
  s.transform.rotGridE = s.transform.refGridE + 10.0;
  CHECK_FALSE(ValidateDrawingTransform(s).empty());
  CHECK_FALSE(ApplyDrawingSettings(st, st.drawingInsUnits, st.modelUnitsPerPlottedInch, s, log));
  CHECK(log.back().find("coincides") != std::string::npos);
  CHECK_FALSE(st.drawingSettings.transform.apply);  // nothing changed

  s.transform = HandCase();
  s.transform.userScaleFactor = 0.0;
  CHECK_FALSE(ApplyDrawingSettings(st, st.drawingInsUnits, st.modelUnitsPerPlottedInch, s, log));
  s.transform.userScaleFactor = 0.9999;
  s.transform.apply = false;
  s.transform.userScaleFactor = -1.0;  // not checked while the transform is off
  CHECK(ValidateDrawingTransform(s).empty());

  s.transform = HandCase();
  REQUIRE(ApplyDrawingSettings(st, st.drawingInsUnits, st.modelUnitsPerPlottedInch, s, log));
  CHECK(st.drawingSettings.transform == HandCase());
  REQUIRE(DoUndo(st, log));
  CHECK_FALSE(st.drawingSettings.transform.apply);
  REQUIRE(DoRedo(st, log));
  CHECK(st.drawingSettings.transform == HandCase());

  // The transform belongs to the zone: No Datum, No Projection or Remove Location resets it.
  s = st.drawingSettings;
  s.zoneCode.clear();
  REQUIRE(ApplyDrawingSettings(st, st.drawingInsUnits, st.modelUnitsPerPlottedInch, s, log));
  CHECK(st.drawingSettings.transform == Tx{});
  REQUIRE(DoUndo(st, log));
  CHECK(st.drawingSettings.transform == HandCase());
  REQUIRE(RemoveGeoLocation(st, log));
  CHECK(st.drawingSettings.transform == Tx{});
}

TEST_CASE("Every transform setting survives the trailer (REQ-360)", "[req360]") {
  AppCommandState src;
  src.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  Tx& t = src.drawingSettings.transform;
  t = HandCase();
  t.applySeaLevel = true;
  t.elevation = 594.25;
  t.spheroidRadiusM = 6372000.5;
  t.refPointNumber = 17;
  t.rotation = Tx::Rotation::RotationPoint;
  t.rotLocalX = 1234.5;
  t.rotLocalY = 2345.25;
  t.rotGridE = 3000234.125;
  t.rotGridN = 10000345.0625;
  t.rotPointNumber = 42;
  t.localAzimuthDeg = 12.5;
  t.gridAzimuthDeg = 13.75;
  std::vector<std::string> log;
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(src), log));
  CHECK(back.drawingSettings.transform == t);

  // A drawing written before REQ-360 opens with the defaults.
  AppCommandState plain;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(plain), log));
  CHECK(back.drawingSettings.transform == Tx{});
}

TEST_CASE("DRAWINGSETTINGS picks a point, a survey point or a direction for the tab (REQ-360)", "[req360]") {
  AppCommandState st = TexasFeetDrawing();
  st.worldDocumentOriginX = 1000.0;
  st.worldDocumentOriginY = 2000.0;
  SurveyPoint p;
  p.id = 101;
  p.easting = 25.0;  // LOCAL
  p.northing = 50.0;
  st.surveyPoints.push_back(p);
  std::vector<std::string> log;

  CHECK_FALSE(StartDrawingSettingsPick(st, PickTarget::None, log));
  st.lastCommand = AppCommandState::Kind::Line;
  REQUIRE(StartDrawingSettingsPick(st, PickTarget::ReferencePoint, log));
  CHECK(st.active == AppCommandState::Kind::DrawingSettingsPick);
  CHECK(st.lastCommand == AppCommandState::Kind::Line);  // an internal pick is not repeated by Enter
  CHECK(ViewportClickRouteFor(st) == ViewportClickRoute::SnappedPointPick);
  CHECK(std::string(GeoCommandPrompt(st)) == "Specify reference point:");
  SubmitViewportPick(st, 25.0, 50.0, log);  // on the survey point
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(st.drawingSettingsPick.done);
  CHECK(st.drawingSettingsPick.pointNumber == 101);
  CHECK(st.drawingSettingsPick.worldX == 1025.0);  // WORLD
  CHECK(st.drawingSettingsPick.worldY == 2050.0);

  // A plain point: no point number.
  REQUIRE(StartDrawingSettingsPick(st, PickTarget::RotationPoint, log));
  SubmitViewportPick(st, 30.0, 50.0, log);
  CHECK(st.drawingSettingsPick.done);
  CHECK(st.drawingSettingsPick.pointNumber == 0);

  // A direction: two points; due east is azimuth 90°.
  REQUIRE(StartDrawingSettingsPick(st, PickTarget::ToNorth, log));
  SubmitViewportPick(st, 0.0, 0.0, log);
  CHECK(st.active == AppCommandState::Kind::DrawingSettingsPick);
  SubmitViewportPick(st, 0.0, 0.0, log);  // the same point again: refused, still picking
  CHECK(st.active == AppCommandState::Kind::DrawingSettingsPick);
  SubmitViewportPick(st, 10.0, 0.0, log);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(st.drawingSettingsPick.done);
  CHECK(st.drawingSettingsPick.azimuthDeg == Catch::Approx(90.0));

  // Esc: the window comes back with nothing picked.
  REQUIRE(StartDrawingSettingsPick(st, PickTarget::LocalAzimuth, log));
  CancelActiveCommand(st, log);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(st.drawingSettingsPick.target == PickTarget::LocalAzimuth);
  CHECK_FALSE(st.drawingSettingsPick.done);

  // Another command running: refused.
  REQUIRE(StartDrawingSettingsPick(st, PickTarget::ReferencePoint, log));
  CHECK_FALSE(StartDrawingSettingsPick(st, PickTarget::RotationPoint, log));
  CancelActiveCommand(st, log);
}
