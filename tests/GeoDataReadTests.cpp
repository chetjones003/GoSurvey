// REQ-362 (GitHub issue #582 increment 6, D-2026-09-29-g / D-2026-09-30-a): reading an AutoCAD /
// Civil 3D drawing's GEODATA — the geographic marker, north, the zone when the definition names one,
// and REQ-360's scale settings stored with the transform left off. GoSurvey writes no GEODATA
// (deferred to issue #590), so a GoSurvey DWG with a trailer opens from the trailer unchanged.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "DwgIo.hpp"
#include "LibreDwgCad.hpp"
#include "geo/CoordinateSystems.hpp"

namespace {

void LoadShippedDictionary() {
  if (!geo::DictionariesLoaded())
    REQUIRE(geo::LoadDictionaries(GOSURVEY_CSMAP_DICTIONARY_DIR));
}

bool LogHas(const std::vector<std::string>& log, const std::string& needle) {
  for (const std::string& l : log)
    if (l.find(needle) != std::string::npos)
      return true;
  return false;
}

double Dms(double d, double m, double s) { return d + m / 60.0 + s / 3600.0; }

// NGS PID AG9976 (the REQ-358 / REQ-359 control point): lat/long and its Texas Central grid, sFT.
const double kLat = Dms(30, 17, 10.51249);
const double kLon = -Dms(97, 44, 21.71739);
constexpr double kNFt = 10077391.26;
constexpr double kEFt = 3115243.14;

std::string TempDwg(const char* name) {
  const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
  std::error_code ec;
  std::filesystem::remove(p, ec);
  return p.u8string();
}

}  // namespace

TEST_CASE("GEODATA coordinate-system definition: bare code, XML id, none (REQ-362)", "[req362]") {
  CHECK(GeoDataCoordinateSystemCode("") == "");
  CHECK(GeoDataCoordinateSystemCode("   ") == "");
  CHECK(GeoDataCoordinateSystemCode("TX83-CF") == "TX83-CF");
  CHECK(GeoDataCoordinateSystemCode("  HARN/TX.TX-CF\r\n") == "HARN/TX.TX-CF");
  CHECK(GeoDataCoordinateSystemCode("two words") == "");  // not a code
  // The AutoCAD / Map 3D XML form: the first ...CoordinateSystem element's id, not the Dictionary's.
  const std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-16\" standalone=\"no\" ?>"
      "<Dictionary version=\"1.0\" xmlns=\"http://www.osgeo.org/mapguide/coordinatesystem\">"
      "<ProjectedCoordinateSystem uuid=\"x\" id=\"TX83-CF\"><Name>TX83-CF</Name>"
      "</ProjectedCoordinateSystem><GeodeticDatum id=\"NAD83\"/></Dictionary>";
  CHECK(GeoDataCoordinateSystemCode(xml) == "TX83-CF");
  CHECK(GeoDataCoordinateSystemCode("<GeographicCoordinateSystem id='LL84'/>") == "LL84");
  CHECK(GeoDataCoordinateSystemCode("<Dictionary><Name>none</Name></Dictionary>") == "");
}

TEST_CASE("A Civil 3D DWG's GEODATA sets the marker, north and scale method, no zone (REQ-362)",
          "[req362][dwg]") {
  LoadShippedDictionary();
  const std::string p = std::string(GOSURVEY_SAMPLES_DIR) + "/duke-main-clean-r2018.dwg";
  REQUIRE(std::filesystem::exists(p));

  AppCommandState st;
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";  // the file replaces the location with its own
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(st, p.c_str(), log));
  for (const std::string& l : log)
    UNSCOPED_INFO(l);

  const DrawingSettings& ds = st.drawingSettings;
  CHECK(std::abs(ds.markerX - 1846238.730) <= 0.001);
  CHECK(std::abs(ds.markerY - 13629548.130) <= 0.001);
  CHECK(ds.markerNorthDeg == Catch::Approx(89.812).margin(0.001));
  CHECK(ds.zoneCode.empty());
  CHECK_FALSE(ds.transform.apply);  // stored for review, never applied on open
  CHECK(ds.transform.computation == DrawingSettings::Transform::Computation::ReferencePoint);
  CHECK(ds.transform.refLocalX == ds.markerX);
  CHECK(ds.transform.refLocalY == ds.markerY);
  CHECK(LogHas(log, "names no coordinate system"));
  CHECK(LogHas(log, "geographic marker at 1846238.730, 13629548.130"));
}

TEST_CASE("A GEODATA naming a zone sets it and the reference grid point (REQ-362)", "[req362]") {
  LoadShippedDictionary();
  DwgGeoData g;
  g.designX = 1000.0;
  g.designY = 2000.0;
  g.reference = DwgGeoData::Reference::Geographic;
  g.refX = kLon;
  g.refY = kLat;
  g.northX = 0.0;
  g.northY = 1.0;
  g.scaleEstimation = 2;  // user specified
  g.userScaleFactor = 0.99995;
  g.seaLevelCorrection = true;
  g.seaLevelElevation = 500.0;
  g.projectionRadius = 6378137.0;

  SECTION("as the id of an XML definition") {
    g.coordinateSystemDefinition = "<ProjectedCoordinateSystem id=\"HARN/TX.TX-CF\"></ProjectedCoordinateSystem>";
    AppCommandState st;
    std::vector<std::string> log;
    ApplyDwgGeoData(st, g, log);
    const DrawingSettings& ds = st.drawingSettings;
    CHECK(ds.zoneCode == "HARN/TX.TX-CF");
    CHECK(ds.markerX == 1000.0);
    CHECK(ds.markerY == 2000.0);
    CHECK(ds.markerNorthDeg == Catch::Approx(90.0));
    const DrawingSettings::Transform& t = ds.transform;
    CHECK_FALSE(t.apply);
    CHECK(t.computation == DrawingSettings::Transform::Computation::UserDefined);
    CHECK(t.userScaleFactor == 0.99995);
    CHECK(t.applySeaLevel);
    CHECK(t.elevation == 500.0);
    CHECK(t.spheroidRadiusM == 6378137.0);
    CHECK(t.refLocalX == 1000.0);
    CHECK(t.refLocalY == 2000.0);
    // The reference point's grid coordinate: AG9976 to the NGS datasheet's 0.01 ft.
    CHECK(std::abs(t.refGridE - kEFt) <= 0.01);
    CHECK(std::abs(t.refGridN - kNFt) <= 0.01);
    CHECK(LogHas(log, "zone HARN/TX.TX-CF"));
  }
  SECTION("as a bare code") {
    g.coordinateSystemDefinition = "HARN/TX.TX-CF";
    AppCommandState st;
    std::vector<std::string> log;
    ApplyDwgGeoData(st, g, log);
    CHECK(st.drawingSettings.zoneCode == "HARN/TX.TX-CF");
  }
  SECTION("a projected-grid reference point is already the grid coordinate") {
    g.coordinateSystemDefinition = "HARN/TX.TX-CF";
    g.reference = DwgGeoData::Reference::ProjectedGrid;
    g.refX = kEFt;
    g.refY = kNFt;
    AppCommandState st;
    std::vector<std::string> log;
    ApplyDwgGeoData(st, g, log);
    CHECK(st.drawingSettings.transform.refGridE == kEFt);
    CHECK(st.drawingSettings.transform.refGridN == kNFt);
  }
  SECTION("a local-grid reference point gives no grid reference") {
    g.coordinateSystemDefinition = "HARN/TX.TX-CF";
    g.reference = DwgGeoData::Reference::None;
    AppCommandState st;
    std::vector<std::string> log;
    ApplyDwgGeoData(st, g, log);
    CHECK(st.drawingSettings.transform.refGridE == 0.0);
    CHECK(st.drawingSettings.transform.refGridN == 0.0);
  }
  SECTION("an unknown code is kept verbatim, with no grid reference") {
    g.coordinateSystemDefinition = "NOT-A-ZONE-362";
    AppCommandState st;
    std::vector<std::string> log;
    ApplyDwgGeoData(st, g, log);
    CHECK(st.drawingSettings.zoneCode == "NOT-A-ZONE-362");
    CHECK(st.drawingSettings.transform.refGridE == 0.0);
    CHECK(st.drawingSettings.transform.refGridN == 0.0);
    CHECK(LogHas(log, "NOT-A-ZONE-362 is unknown"));
  }
  SECTION("north direction below the X axis wraps to 0..360") {
    g.northX = 1.0;
    g.northY = -1.0;
    AppCommandState st;
    std::vector<std::string> log;
    ApplyDwgGeoData(st, g, log);
    CHECK(st.drawingSettings.markerNorthDeg == Catch::Approx(315.0));
  }
  SECTION("no north direction means grid north; a bad user factor keeps Reference Point") {
    g.northX = 0.0;
    g.northY = 0.0;
    g.userScaleFactor = 0.0;
    AppCommandState st;
    std::vector<std::string> log;
    ApplyDwgGeoData(st, g, log);
    CHECK(st.drawingSettings.markerNorthDeg == 90.0);
    CHECK(st.drawingSettings.transform.computation == DrawingSettings::Transform::Computation::ReferencePoint);
  }
}

TEST_CASE("A GoSurvey DWG with a trailer opens from the trailer; no GEODATA opens as before (REQ-362)",
          "[req362][dwg]") {
  LoadShippedDictionary();
  AppCommandState st;
  st.drawingInsUnits = 2;
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  st.drawingSettings.markerX = 12.5;
  st.drawingSettings.markerY = -7.25;
  st.drawingSettings.markerNorthDeg = 33.0;
  st.drawingSettings.transform.apply = true;
  st.drawingSettings.transform.userScaleFactor = 1.0001;
  st.drawingSettings.transform.computation = DrawingSettings::Transform::Computation::UserDefined;
  st.userLinesFlat = {0, 0, 0, 10, 0, 0};
  st.userLineAttrs.resize(1);
  std::vector<std::string> log;

  // With the trailer: every geolocation setting comes back exactly, and no GEODATA line is logged.
  const std::string withTrailer = TempDwg("gosurvey-req362-trailer.dwg");
  REQUIRE(ExportDwgFile(st, withTrailer.c_str(), log));
  AppCommandState back;
  std::vector<std::string> openLog;
  REQUIRE(ImportDwgFile(back, withTrailer.c_str(), openLog));
  CHECK(back.drawingSettings == st.drawingSettings);
  CHECK_FALSE(LogHas(openLog, "GEODATA"));

  // Without the trailer (the DWG body alone, as another program's file would be): no GEODATA,
  // so the location stays at the defaults — the file opens as before REQ-362.
  const std::string bodyOnly = TempDwg("gosurvey-req362-body.dwg");
  REQUIRE(ExportLibreCadFile(st, bodyOnly.c_str(), log, /*asDxf=*/false));
  AppCommandState plain;
  std::vector<std::string> plainLog;
  REQUIRE(ImportDwgFile(plain, bodyOnly.c_str(), plainLog));
  CHECK(plain.drawingSettings.zoneCode.empty());
  CHECK(plain.drawingSettings.markerX == 0.0);
  CHECK(plain.drawingSettings.markerY == 0.0);
  CHECK(plain.drawingSettings.markerNorthDeg == 90.0);
  CHECK(plain.drawingSettings.transform == DrawingSettings::Transform{});
  CHECK_FALSE(LogHas(plainLog, "GEODATA"));

  std::error_code ec;
  std::filesystem::remove(withTrailer, ec);
  std::filesystem::remove(bodyOnly, ec);
}

TEST_CASE("BLOCKIMPORT from a GEODATA drawing leaves this drawing's location alone (REQ-362)",
          "[req362][dwg][blockimport]") {
  LoadShippedDictionary();
  const std::string p = std::string(GOSURVEY_SAMPLES_DIR) + "/duke-main-clean-r2018.dwg";
  AppCommandState st;
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  st.drawingSettings.markerX = 5.0;
  const DrawingSettings before = st.drawingSettings;
  std::vector<std::string> log;
  ImportCadBlocksFromPath(st, p.c_str(), log);
  CHECK(st.drawingSettings == before);
  CHECK_FALSE(LogHas(log, "GEODATA"));  // the source file's location is not reported as ours
}
