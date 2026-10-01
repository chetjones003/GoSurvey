// REQ-362 (GitHub issue #582 increment 6, D-2026-09-29-g / D-2026-09-30-a): reading an AutoCAD /
// Civil 3D drawing's GEODATA — the geographic marker, north, the zone when the definition names one,
// and REQ-360's scale settings stored with the transform left off — and (item 2, D-2026-09-30-d)
// writing one on DWG save; a GoSurvey DWG with a trailer still opens from the trailer.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "DwgIo.hpp"
#include "LibreDwgCad.hpp"
#include "geo/CoordinateSystems.hpp"

#if defined(__cplusplus) && !defined(restrict)
#define restrict
#endif
extern "C" {
#include <dwg.h>
#include <dwg_api.h>
}

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

// REQ-362 item 3: an R2000 GEODATA (the 2009, class-version-1 layout) that AutoCAD 2027 itself saved.
// AutoCAD reports for it: design point (1846238.73, 13629548.13), reference (-99.383333, 29.225),
// north (0.00327772, 0.999995), scale estimation 3, no sea-level correction, `TX83-CF`. LibreDWG
// 0.13.4 read one extra bit there and got north, the scale method and sea level wrong (TASK-299).
TEST_CASE("An AutoCAD-saved R2000 GEODATA reads with AutoCAD's values (REQ-362)", "[req362][dwg]") {
  LoadShippedDictionary();
  const std::string p = std::string(GOSURVEY_SAMPLES_DIR) + "/geodata-r2000-autocad.dwg";
  REQUIRE(std::filesystem::exists(p));

  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(st, p.c_str(), log));
  for (const std::string& l : log)
    UNSCOPED_INFO(l);

  const DrawingSettings& ds = st.drawingSettings;
  CHECK(std::abs(ds.markerX - 1846238.73) <= 0.001);
  CHECK(std::abs(ds.markerY - 13629548.13) <= 0.001);
  CHECK(ds.markerNorthDeg == Catch::Approx(89.8122).margin(0.0001));
  CHECK(ds.zoneCode == "TX83-CF");
  CHECK_FALSE(ds.transform.apply);
  CHECK(ds.transform.computation == DrawingSettings::Transform::Computation::ReferencePoint);
  CHECK_FALSE(ds.transform.applySeaLevel);
  CHECK(ds.transform.elevation == 0.0);
  CHECK(ds.transform.refLocalX == ds.markerX);
  CHECK(ds.transform.refLocalY == ds.markerY);
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

TEST_CASE("A GoSurvey DWG with a trailer opens from the trailer (REQ-362)", "[req362][dwg]") {
  LoadShippedDictionary();
  AppCommandState st;
  st.drawingInsUnits = 2;
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  st.drawingSettings.markerX = kEFt;
  st.drawingSettings.markerY = kNFt;
  st.drawingSettings.markerNorthDeg = 33.0;
  st.drawingSettings.transform.apply = true;
  st.drawingSettings.transform.userScaleFactor = 1.0001;
  st.drawingSettings.transform.computation = DrawingSettings::Transform::Computation::UserDefined;
  st.userLinesFlat = {0, 0, 0, 10, 0, 0};
  st.userLineAttrs.resize(1);
  std::vector<std::string> log;

  // The file carries a GEODATA too (item 2), but GoSurvey reads its own trailer: every setting comes
  // back exactly, including the ones GEODATA does not carry, and no GEODATA line is logged.
  const std::string withTrailer = TempDwg("gosurvey-req362-trailer.dwg");
  REQUIRE(ExportDwgFile(st, withTrailer.c_str(), log));
  CHECK(LogHas(log, "GEODATA written"));
  AppCommandState back;
  std::vector<std::string> openLog;
  REQUIRE(ImportDwgFile(back, withTrailer.c_str(), openLog));
  CHECK(back.drawingSettings == st.drawingSettings);
  CHECK_FALSE(LogHas(openLog, "GEODATA"));

  std::error_code ec;
  std::filesystem::remove(withTrailer, ec);
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

// --- REQ-362 item 2: writing GEODATA (D-2026-09-30-d) ------------------------------------------------

namespace {
// A geolocated drawing with the marker at NGS AG9976's Texas Central grid (US survey feet): its
// latitude / longitude are the datasheet's. The local-storage origin is not zero, to show the marker
// is WORLD.
AppCommandState GeolocatedAtAg9976() {
  AppCommandState st;
  st.drawingInsUnits = 2;  // feet (US survey, the default foot)
  st.worldDocumentOriginX = 3115000.0;
  st.worldDocumentOriginY = 10077000.0;
  st.drawingSettings.zoneCode = "HARN/TX.TX-CF";
  st.drawingSettings.markerX = kEFt;
  st.drawingSettings.markerY = kNFt;
  st.drawingSettings.markerNorthDeg = 33.0;
  DrawingSettings::Transform& t = st.drawingSettings.transform;
  t.applySeaLevel = true;
  t.elevation = 500.0;
  t.spheroidRadiusM = 6378137.0;
  st.userLinesFlat = {0, 0, 0, 10, 0, 0};
  st.userLineAttrs.resize(1);
  return st;
}
}  // namespace

TEST_CASE("The GEODATA a save writes: marker, lat/long, north, unit, zone, scale settings (REQ-362)",
          "[req362]") {
  LoadShippedDictionary();
  AppCommandState st = GeolocatedAtAg9976();
  DwgGeoData g;
  std::string why;
  REQUIRE(BuildDwgGeoData(st, &g, &why));
  CHECK(g.designX == kEFt);
  CHECK(g.designY == kNFt);
  CHECK(g.reference == DwgGeoData::Reference::Geographic);
  CHECK(std::abs(g.refX - kLon) <= 1e-7);
  CHECK(std::abs(g.refY - kLat) <= 1e-7);
  CHECK(g.northX == Catch::Approx(std::cos(33.0 * 3.14159265358979323846 / 180.0)));
  CHECK(g.northY == Catch::Approx(std::sin(33.0 * 3.14159265358979323846 / 180.0)));
  CHECK(g.horizontalUnits == 2);
  CHECK(g.horizontalUnitScale == Catch::Approx(1200.0 / 3937.0));
  CHECK(g.coordinateSystemDefinition == "HARN/TX.TX-CF");
  CHECK(g.scaleEstimation == 3);  // Reference Point
  CHECK(g.seaLevelCorrection);
  CHECK(g.seaLevelElevation == 500.0);
  CHECK(g.projectionRadius == 6378137.0);

  SECTION("User Defined scale") {
    st.drawingSettings.transform.computation = DrawingSettings::Transform::Computation::UserDefined;
    st.drawingSettings.transform.userScaleFactor = 0.99995;
    REQUIRE(BuildDwgGeoData(st, &g, &why));
    CHECK(g.scaleEstimation == 2);
    CHECK(g.userScaleFactor == 0.99995);
  }
}

TEST_CASE("No GEODATA without a usable zone, and the reason says why (REQ-362)", "[req362]") {
  LoadShippedDictionary();
  AppCommandState st = GeolocatedAtAg9976();
  DwgGeoData g;
  std::string why;
  SECTION("no zone") {
    st.drawingSettings.zoneCode.clear();
    CHECK_FALSE(BuildDwgGeoData(st, &g, &why));
    CHECK(why.find("no zone") != std::string::npos);
  }
  SECTION("a zone the dictionary does not know") {
    st.drawingSettings.zoneCode = "NOT-A-ZONE-362";
    CHECK_FALSE(BuildDwgGeoData(st, &g, &why));
    CHECK(why.find("cannot be computed") != std::string::npos);
  }
  SECTION("a Unitless drawing") {
    st.drawingInsUnits = 0;
    CHECK_FALSE(BuildDwgGeoData(st, &g, &why));
    CHECK(why.find("Unitless") != std::string::npos);
  }
}

TEST_CASE("R2018 DWG GEODATA uses class version 2 (issue #623)", "[req362][dwg][libredwg][issue623]") {
  LoadShippedDictionary();
  AppCommandState st = GeolocatedAtAg9976();
  st.dwgExportVersion = DwgSaveVersion::R2018;
  const std::string body = TempDwg("gosurvey-geodata-v2.dwg");
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, body.c_str(), log, /*asDxf=*/false));
  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  REQUIRE(dwg_read_file(body.c_str(), &dwg) < DWG_ERR_CRITICAL);
  bool found = false;
  for (unsigned i = 0; i < dwg.num_objects; ++i) {
    if (dwg.object[i].fixedtype != DWG_TYPE_GEODATA || dwg.object[i].tio.object == nullptr ||
        dwg.object[i].tio.object->tio.GEODATA == nullptr)
      continue;
    const Dwg_Object_GEODATA* g = dwg.object[i].tio.object->tio.GEODATA;
    CHECK(g->class_version == 2);
    CHECK(g->coord_type == 3);
    CHECK(g->ref_pt.x == Catch::Approx(kLon).margin(1e-6));
    CHECK(g->ref_pt.y == Catch::Approx(kLat).margin(1e-6));
    found = true;
  }
  dwg_free(&dwg);
  CHECK(found);
  std::error_code ec;
  std::filesystem::remove(body, ec);
}

TEST_CASE("A saved DWG's GEODATA alone reopens the location (REQ-362)", "[req362][dwg]") {
  LoadShippedDictionary();
  AppCommandState st = GeolocatedAtAg9976();
  st.drawingSettings.transform.computation = DrawingSettings::Transform::Computation::UserDefined;
  st.drawingSettings.transform.userScaleFactor = 1.0001;

  // The DWG body alone, without GoSurvey's trailer — what AutoCAD leaves after it re-saves the file.
  const std::string body = TempDwg("gosurvey-req362-write-body.dwg");
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, body.c_str(), log, /*asDxf=*/false));
  for (const std::string& l : log)
    UNSCOPED_INFO(l);
  CHECK(LogHas(log, "GEODATA written: zone HARN/TX.TX-CF"));

  AppCommandState back;
  std::vector<std::string> openLog;
  REQUIRE(ImportDwgFile(back, body.c_str(), openLog));
  for (const std::string& l : openLog)
    UNSCOPED_INFO(l);
  const DrawingSettings& ds = back.drawingSettings;
  CHECK(ds.zoneCode == "HARN/TX.TX-CF");
  CHECK(std::abs(ds.markerX - kEFt) <= 0.001);
  CHECK(std::abs(ds.markerY - kNFt) <= 0.001);
  CHECK(ds.markerNorthDeg == Catch::Approx(33.0).margin(0.001));
  const DrawingSettings::Transform& t = ds.transform;
  CHECK_FALSE(t.apply);  // stored for review, as for any GEODATA (item 1)
  CHECK(t.computation == DrawingSettings::Transform::Computation::UserDefined);
  CHECK(t.userScaleFactor == 1.0001);
  CHECK(t.applySeaLevel);
  CHECK(t.elevation == 500.0);
  CHECK(t.spheroidRadiusM == 6378137.0);
  // The reference point's grid, from the written latitude / longitude: AG9976 again.
  CHECK(std::abs(t.refGridE - kEFt) <= 0.01);
  CHECK(std::abs(t.refGridN - kNFt) <= 0.01);
  CHECK(LogHas(openLog, "GEODATA — zone HARN/TX.TX-CF"));

  std::error_code ec;
  std::filesystem::remove(body, ec);
}

TEST_CASE("A DWG save without a zone, and any DXF export, write no GEODATA (REQ-362)", "[req362][dwg][dxf]") {
  LoadShippedDictionary();
  std::error_code ec;
  SECTION("DWG without a zone: nothing written, and the log says why") {
    AppCommandState st = GeolocatedAtAg9976();
    st.drawingSettings.zoneCode.clear();
    const std::string body = TempDwg("gosurvey-req362-nozone.dwg");
    std::vector<std::string> log;
    REQUIRE(ExportLibreCadFile(st, body.c_str(), log, /*asDxf=*/false));
    CHECK(LogHas(log, "no GEODATA written: the drawing has no zone"));
    AppCommandState back;
    std::vector<std::string> openLog;
    REQUIRE(ImportDwgFile(back, body.c_str(), openLog));
    CHECK(back.drawingSettings.zoneCode.empty());
    CHECK_FALSE(LogHas(openLog, "GEODATA"));
    std::filesystem::remove(body, ec);
  }
  SECTION("DXF: REQ-362 writes GEODATA into the DWG only") {
    AppCommandState st = GeolocatedAtAg9976();
    const std::string dxf = TempDwg("gosurvey-req362.dxf");
    std::vector<std::string> log;
    REQUIRE(ExportLibreCadFile(st, dxf.c_str(), log, /*asDxf=*/true));
    CHECK_FALSE(LogHas(log, "GEODATA"));
    std::ifstream f(dxf, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(text.find("GEODATA") == std::string::npos);
    f.close();
    std::filesystem::remove(dxf, ec);
  }
}
