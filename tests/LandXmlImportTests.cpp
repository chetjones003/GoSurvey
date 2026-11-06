// Issue #1003 — Import supported LandXML data (REQ-506, sub-issue of #802).
//
// The importer reads what REQ-448's exporter writes, so the round-trip tests export a drawing to a
// real file and import that file into an empty drawing. Other tests write small LandXML documents by
// hand to cover units, point-number conflicts, skipped items and unreadable files.
#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"
#include "io/LandXmlIo.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

/// A scratch folder removed when the test ends.
struct ScratchDir {
  fs::path path;
  explicit ScratchDir(const char* name) {
    path = fs::temp_directory_path() / (std::string("gosurvey-landxml-import-") + name);
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~ScratchDir() {
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
};

bool LogMentions(const std::vector<std::string>& log, const std::string& text) {
  for (const std::string& line : log)
    if (line.find(text) != std::string::npos) return true;
  return false;
}

void WriteText(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
}

SurveyPoint MakePoint(int id, double easting, double northing, double elevation, const char* description = "") {
  SurveyPoint point;
  point.id = id;
  point.easting = easting;
  point.northing = northing;
  point.elevation = elevation;
  point.description = description;
  return point;
}

/// A built two-triangle TIN: a unit square at x,y in [0,1], z = 10, 11, 12, 13.
CadSurface MakeSquareSurface(const char* name) {
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {0, 0, 10, 1, 0, 11, 1, 1, 12, 0, 1, 13};
  tin->indices = {0, 1, 2, 0, 2, 3};
  CadSurface surface;
  surface.name = name;
  surface.tin = tin;
  return surface;
}

/// A feature line appended the way the FEATURELINE command stores it.
void AddFeatureLine(AppCommandState& st, const std::vector<double>& xyz, bool closed, const char* name,
                    const char* description = "") {
  if (st.featureLineOffsets.empty()) st.featureLineOffsets.push_back(0);
  const int vertices = static_cast<int>(xyz.size() / 3);
  st.featureLineVerts.insert(st.featureLineVerts.end(), xyz.begin(), xyz.end());
  st.featureLineElevPt.insert(st.featureLineElevPt.end(), static_cast<size_t>(vertices), uint8_t{0});
  st.featureLineOffsets.push_back(st.featureLineOffsets.back() + vertices);
  st.featureLineClosed.push_back(closed ? 1 : 0);
  CadFeatureLineInfo info;
  info.name = name;
  info.description = description;
  st.featureLineInfo.push_back(info);
}

/// Feature line \p index as its stored local vertices (x,y,z triples).
std::vector<double> FeatureVerts(const AppCommandState& st, size_t index) {
  const int first = st.featureLineOffsets[index], last = st.featureLineOffsets[index + 1];
  return std::vector<double>(st.featureLineVerts.begin() + first * 3, st.featureLineVerts.begin() + last * 3);
}

/// Imports \p text (written to \p dir/\p name) into \p st and returns the import result.
bool ImportText(AppCommandState& st, const ScratchDir& dir, const char* name, const std::string& text,
                std::vector<std::string>& log) {
  const fs::path file = dir.path / name;
  WriteText(file, text);
  return ImportLandXmlFile(st, file.u8string().c_str(), log);
}

const char* kHeader = R"(<?xml version="1.0" encoding="utf-8"?>
<LandXML xmlns="http://www.landxml.org/schema/LandXML-1.2" version="1.2">)";

}  // namespace

TEST_CASE("LandXML import restores points, a surface and feature lines from a REQ-448 export",
          "[landxml][issue1003][roundtrip]") {
  ScratchDir dir("roundtrip");
  AppCommandState source;
  source.worldDocumentOriginX = 2196000;
  source.worldDocumentOriginY = 1400000;
  source.drawingInsUnits = 6;  // meters
  // Survey points are stored local (world = local + origin), so these are local values.
  source.surveyPoints = {MakePoint(42, 0.125, 12.25, 98.125, "Control & <benchmark> \"north\""),
                         MakePoint(43, -5.5, 12.25, 98.125, "Tree")};
  source.surveyPoints[0].rawDescription = "CP1";
  source.cadSurfaces.push_back(MakeSquareSurface("Ground"));
  AddFeatureLine(source, {0, 0, 5, 10, 0, 6, 10, 10, 7}, false, "Top of curb", "Back & face");
  AddFeatureLine(source, {0, 0, 1, 4, 0, 2, 4, 3, 3}, true, "Pond rim");
  const fs::path exported = dir.path / "export.xml";
  std::vector<std::string> exportLog;
  REQUIRE(ExportLandXmlFile(source, exported.u8string().c_str(), exportLog));

  AppCommandState target;
  target.worldDocumentOriginX = 2196000;
  target.worldDocumentOriginY = 1400000;
  target.drawingInsUnits = 6;
  std::vector<std::string> log;
  REQUIRE(ImportLandXmlFile(target, exported.u8string().c_str(), log));

  REQUIRE(target.surveyPoints.size() == 2);
  CHECK(target.surveyPoints[0].id == 42);
  CHECK(target.surveyPoints[0].description == "Control & <benchmark> \"north\"");
  CHECK(target.surveyPoints[0].rawDescription == "CP1");
  CHECK(target.surveyPoints[0].easting == 0.125);
  CHECK(target.surveyPoints[0].northing == 12.25);
  CHECK(target.surveyPoints[0].elevation == 98.125);
  CHECK(target.surveyPoints[1].easting == -5.5);
  CHECK(target.pendingLandXmlConflictPoints.empty());

  REQUIRE(target.cadSurfaces.size() == 1);
  CHECK(target.cadSurfaces[0].name == "Ground");
  CHECK(target.cadSurfaces[0].addedPointXyz.size() == 12);
  CHECK(target.cadSurfaces[0].tin != nullptr);

  REQUIRE(target.featureLineInfo.size() == 2);
  CHECK(target.featureLineInfo[0].name == "Top of curb");
  CHECK(target.featureLineInfo[0].description == "Back & face");
  CHECK(target.featureLineClosed[0] == 0);
  CHECK(FeatureVerts(target, 0) == FeatureVerts(source, 0));
  CHECK(target.featureLineInfo[1].name == "Pond rim");
  CHECK(target.featureLineClosed[1] == 1);
  CHECK(FeatureVerts(target, 1) == FeatureVerts(source, 1));
}

TEST_CASE("LandXML import keeps world coordinates exact far from the origin", "[landxml][issue1003][origin]") {
  ScratchDir dir("origin");
  AppCommandState st;
  st.worldDocumentOriginX = 2196000.1;
  st.worldDocumentOriginY = 1400000.2;
  std::vector<std::string> log;
  REQUIRE(ImportText(st, dir, "origin.xml", std::string(kHeader) +
      "<CgPoints><CgPoint name=\"9\">1400000.7 2196000.3 55.5</CgPoint></CgPoints></LandXML>", log));
  REQUIRE(st.surveyPoints.size() == 1);
  // The file holds world northing 1400000.7 and easting 2196000.3; the store keeps local = world - origin.
  CHECK(st.surveyPoints[0].northing == 1400000.7 - 1400000.2);
  CHECK(st.surveyPoints[0].easting == 2196000.3 - 2196000.1);
  CHECK(st.surveyPoints[0].elevation == 55.5);
}

TEST_CASE("LandXML import converts a US survey foot file into a meter drawing", "[landxml][issue1003][units]") {
  ScratchDir dir("units");
  AppCommandState st;
  st.drawingInsUnits = 6;  // meters
  std::vector<std::string> log;
  REQUIRE(ImportText(st, dir, "feet.xml", std::string(kHeader) +
      "<Units><Imperial linearUnit=\"USSurveyFoot\"/></Units>"
      "<CgPoints><CgPoint name=\"1\">1000 2000 100</CgPoint></CgPoints></LandXML>", log));
  REQUIRE(st.surveyPoints.size() == 1);
  const double metersPerFoot = 1200.0 / 3937.0;
  CHECK(std::fabs(st.surveyPoints[0].northing - 1000.0 * metersPerFoot) < 1e-9);
  CHECK(std::fabs(st.surveyPoints[0].easting - 2000.0 * metersPerFoot) < 1e-9);
  CHECK(std::fabs(st.surveyPoints[0].elevation - 100.0 * metersPerFoot) < 1e-9);
}

TEST_CASE("LandXML import takes values as written for a unitless drawing and logs the unit",
          "[landxml][issue1003][units]") {
  ScratchDir dir("unitless");
  AppCommandState st;
  st.drawingInsUnits = 0;  // unitless; the default drawing is in feet
  std::vector<std::string> log;
  REQUIRE(ImportText(st, dir, "units.xml", std::string(kHeader) +
      "<Units><Metric linearUnit=\"meter\"/></Units>"
      "<CgPoints><CgPoint name=\"1\">3 4 5</CgPoint></CgPoints></LandXML>", log));
  CHECK(st.surveyPoints[0].northing == 3.0);
  CHECK(LogMentions(log, "unitless"));
}

TEST_CASE("LandXML import holds point numbers that already exist until the user answers",
          "[landxml][issue1003][conflict]") {
  ScratchDir dir("conflict");
  AppCommandState st;
  st.surveyPoints = {MakePoint(42, 1, 2, 3, "Existing")};
  std::vector<std::string> log;
  REQUIRE(ImportText(st, dir, "conflict.xml", std::string(kHeader) +
      "<CgPoints><CgPoint name=\"42\">20 10 30</CgPoint>"
      "<CgPoint name=\"44\">21 11 31</CgPoint></CgPoints></LandXML>", log));

  // The new number is written; the existing one is untouched and waits in the conflict list.
  REQUIRE(st.surveyPoints.size() == 2);
  CHECK(st.surveyPoints[0].description == "Existing");
  CHECK(st.surveyPoints[1].id == 44);
  REQUIRE(st.pendingLandXmlConflictPoints.size() == 1);
  CHECK(st.pendingLandXmlConflictPoints[0].id == 42);
  CHECK(st.pendingLandXmlConflictPoints[0].northing == 20.0);
  CHECK(LogMentions(log, "Overwrite or Offset"));

  // Overwrite replaces the existing point (the resolver the Qt question uses).
  std::vector<std::string> answerLog;
  ResolveConflictingWorldSurveyPoints(st, st.pendingLandXmlConflictPoints, true, 0, &answerLog);
  int count42 = 0;
  for (const SurveyPoint& point : st.surveyPoints)
    if (point.id == 42) {
      ++count42;
      CHECK(point.northing == 20.0);
    }
  CHECK(count42 == 1);

  // Offset keeps both by adding the offset to the imported number.
  AppCommandState offsetState;
  offsetState.surveyPoints = {MakePoint(42, 1, 2, 3, "Existing")};
  std::vector<std::string> offsetLog;
  REQUIRE(ImportText(offsetState, dir, "conflict2.xml", std::string(kHeader) +
      "<CgPoints><CgPoint name=\"42\">20 10 30</CgPoint></CgPoints></LandXML>", offsetLog));
  ResolveConflictingWorldSurveyPoints(offsetState, offsetState.pendingLandXmlConflictPoints, false, 100, &offsetLog);
  CHECK(offsetState.surveyPoints.size() == 2);
  CHECK(offsetState.surveyPoints[1].id == 142);
}

TEST_CASE("LandXML import skips and reports unsupported items and keeps the rest",
          "[landxml][issue1003][skip]") {
  ScratchDir dir("skip");
  AppCommandState st;
  st.cadSurfaces.push_back(MakeSquareSurface("Ground"));
  std::vector<std::string> log;
  REQUIRE(ImportText(st, dir, "skip.xml", std::string(kHeader) +
      "<CgPoints><CgPoint name=\"CP-A\">1 2 3</CgPoint>"
      "<CgPoint name=\"5\">1 2 3</CgPoint><CgPoint name=\"5\">9 9 9</CgPoint>"
      "<CgPoint name=\"6\">oops</CgPoint></CgPoints>"
      "<Surfaces><Surface name=\"Ground\"><Definition surfType=\"TIN\"><Pnts><P id=\"1\">0 0 1</P></Pnts></Definition></Surface>"
      "<Surface name=\"Grid\"><Definition surfType=\"grid\"/></Surface></Surfaces>"
      "<PlanFeatures><PlanFeature name=\"Mixed\"><CoordGeom>"
      "<Line><Start>0 0 1</Start><End>1 0 1</End></Line>"
      "<Curve rot=\"cw\"><Start>1 0 1</Start><End>2 0 1</End></Curve>"
      "<Line><Start>5 5 1</Start><End>6 5 1</End></Line>"
      "</CoordGeom></PlanFeature></PlanFeatures></LandXML>", log));

  CHECK(st.surveyPoints.size() == 1);  // only point 5, first occurrence
  CHECK(st.surveyPoints[0].id == 5);
  CHECK(LogMentions(log, "\"CP-A\""));
  CHECK(LogMentions(log, "repeated in the file"));
  CHECK(LogMentions(log, "coordinates are not three numbers"));
  CHECK(LogMentions(log, "a surface with that name already exists"));
  CHECK(LogMentions(log, "only TIN surfaces"));
  CHECK(LogMentions(log, "curve or other non-line geometry"));
  CHECK(LogMentions(log, "has gaps"));
  CHECK(st.cadSurfaces.size() == 1);  // the existing square is still the only surface
  CHECK(st.featureLineInfo.size() == 2);  // the gap splits the line in two
}

TEST_CASE("LandXML import leaves the drawing alone when the file cannot be read",
          "[landxml][issue1003][failure]") {
  ScratchDir dir("failure");
  AppCommandState st;
  st.surveyPoints = {MakePoint(1, 1, 2, 3)};
  std::vector<std::string> log;

  CHECK_FALSE(ImportText(st, dir, "not-xml.xml", "this is not XML at all <", log));
  CHECK(LogMentions(log, "not a readable XML file"));

  log.clear();
  CHECK_FALSE(ImportText(st, dir, "other.xml", "<Other><CgPoints/></Other>", log));
  CHECK(LogMentions(log, "no LandXML root element"));

  log.clear();
  CHECK_FALSE(ImportLandXmlFile(st, (dir.path / "missing.xml").u8string().c_str(), log));
  CHECK(LogMentions(log, "could not open"));

  CHECK(st.surveyPoints.size() == 1);
  CHECK(st.pendingLandXmlConflictPoints.empty());
}

TEST_CASE("IMPORTLANDXML reads the file named on the command line", "[landxml][issue1003][command]") {
  ScratchDir dir("command");
  const fs::path target = dir.path / "site plan.xml";
  WriteText(target, std::string(kHeader) +
      "<CgPoints><CgPoint name=\"7\">1 2 3</CgPoint></CgPoints></LandXML>");
  AppCommandState st;
  st.drawingTabs.resize(2);
  st.activeDrawingIdx = 1;

  std::vector<std::string> log;
  std::string command = "IMPORTLANDXML \"" + target.u8string() + "\"";
  std::vector<char> buffer(command.begin(), command.end());
  buffer.push_back('\0');
  ProcessCommandLineSubmit(buffer.data(), static_cast<int>(buffer.size()), st, log);
  REQUIRE(st.surveyPoints.size() == 1);
  CHECK(st.surveyPoints[0].id == 7);
  CHECK(LogMentions(log, "imported 1 point(s)"));

  // Bare form names the missing file rather than doing nothing.
  log.clear();
  char bare[] = "IMPORTLANDXML";
  ProcessCommandLineSubmit(bare, sizeof(bare), st, log);
  CHECK(LogMentions(log, "type a file"));
}
