// Issue #1002 — Export supported drawing data to LandXML (sub-issue of #802).
//
// Every test reads the written file back with an XML parser, as an independent LandXML reader
// would, instead of comparing text. The structure checked here (attribute and element names, the
// "northing easting elevation" order, 1-based surface ids, one Line per feature-line segment) was
// taken from the LandXML 1.2 schema and the same files were validated against it once, offline
// (workshop/tasks/TASK-1002-landxml-export.md).
#include <catch2/catch_test_macros.hpp>

#include <pugixml.hpp>

#include "CadCommands.hpp"
#include "io/DwgIo.hpp"
#include "io/LandXmlIo.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

/// A scratch folder removed when the test ends, so a failing test does not litter the temp dir.
struct ScratchDir {
  fs::path path;
  explicit ScratchDir(const char* name) {
    path = fs::temp_directory_path() / (std::string("gosurvey-landxml-") + name);
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

struct Written {
  pugi::xml_document document;
  std::vector<std::string> log;
  bool ok = false;
  pugi::xml_node root() { return document.child("LandXML"); }
};

/// Exports \p st into \p dir/\p name and, when that succeeds, parses the file.
std::unique_ptr<Written> Export(const AppCommandState& st, const ScratchDir& dir, const char* name) {
  auto written = std::make_unique<Written>();
  const std::string path = (dir.path / name).u8string();
  written->ok = ExportLandXmlFile(st, path.c_str(), written->log);
  if (written->ok) {
    const auto parsed = written->document.load_file(fs::u8path(path).c_str());
    INFO("parse error: " << parsed.description());
    REQUIRE(parsed);
  }
  return written;
}

std::vector<double> Numbers(const char* text) {
  std::istringstream in(text);
  std::vector<double> values;
  for (double value; in >> value;) values.push_back(value);
  return values;
}

int CountChildren(pugi::xml_node node, const char* name) {
  return static_cast<int>(std::distance(node.children(name).begin(), node.children(name).end()));
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

/// A feature line appended to the CSR store the way the FEATURELINE command does.
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

}  // namespace

TEST_CASE("LandXML exports every survey point in world NEZ with its number, description and code",
          "[landxml][issue1002]") {
  ScratchDir dir("points");
  AppCommandState st;
  st.worldDocumentOriginX = 2196000;
  st.worldDocumentOriginY = 1400000;
  st.surveyPoints = {MakePoint(42, 0.000125, 12.25, 98.125, "Control & <benchmark> \"north\" 'x'"),
                     MakePoint(43, -5.5, 12.25, 98.125, "Tree")};
  st.surveyPoints[0].rawDescription = "CP1";

  const auto written = Export(st, dir, "points.xml");
  REQUIRE(written->ok);
  auto root = written->root();
  CHECK(std::string(root.attribute("xmlns").value()) == "http://www.landxml.org/schema/LandXML-1.2");
  CHECK(std::string(root.attribute("version").value()) == "1.2");
  CHECK_FALSE(std::string(root.attribute("date").value()).empty());
  CHECK_FALSE(std::string(root.attribute("time").value()).empty());
  CHECK(std::string(root.child("Application").attribute("name").value()) == "GoSurvey");

  auto points = root.child("CgPoints");
  REQUIRE(CountChildren(points, "CgPoint") == 2);
  auto first = points.child("CgPoint");
  CHECK(first.attribute("name").as_int() == 42);
  CHECK(std::string(first.attribute("desc").value()) == "Control & <benchmark> \"north\" 'x'");
  CHECK(std::string(first.attribute("code").value()) == "CP1");
  const auto coordinates = Numbers(first.text().get());
  REQUIRE(coordinates.size() == 3);
  // Northing first, then easting; the document origin is added back (geometry is stored local).
  CHECK(coordinates[0] == 1400012.25);
  CHECK(coordinates[1] == 2196000.000125);
  CHECK(coordinates[2] == 98.125);
  auto second = first.next_sibling("CgPoint");
  CHECK(second.attribute("name").as_int() == 43);
  CHECK_FALSE(second.attribute("code"));  // no field code was recorded
  CHECK(Numbers(second.text().get())[1] == 2195994.5);
}

TEST_CASE("LandXML coordinates keep full double precision without locale formatting",
          "[landxml][issue1002]") {
  ScratchDir dir("precision");
  AppCommandState st;
  st.worldDocumentOriginX = 6123456.789012345;
  st.worldDocumentOriginY = 1987654.321098765;
  const double x = 123.456789012, y = -45.678901234, z = 0.1;
  st.surveyPoints = {MakePoint(1, x, y, z)};

  const auto written = Export(st, dir, "precision.xml");
  REQUIRE(written->ok);
  const auto coordinates = Numbers(written->root().child("CgPoints").child("CgPoint").text().get());
  REQUIRE(coordinates.size() == 3);
  CHECK(coordinates[0] == y + st.worldDocumentOriginY);
  CHECK(coordinates[1] == x + st.worldDocumentOriginX);
  CHECK(coordinates[2] == z);
  CHECK(std::string(written->root().child("CgPoints").child("CgPoint").text().get()).find('e') == std::string::npos);
}

TEST_CASE("LandXML declares the drawing unit and foot definition", "[landxml][issue1002][units]") {
  struct Row {
    int insUnits;
    DrawingSettings::FootDefinition foot;
    const char* system;
    const char* linear;
  };
  const Row rows[] = {
      {2, DrawingSettings::FootDefinition::UsSurvey, "Imperial", "USSurveyFoot"},
      {2, DrawingSettings::FootDefinition::International, "Imperial", "foot"},
      {1, DrawingSettings::FootDefinition::UsSurvey, "Imperial", "inch"},
      {4, DrawingSettings::FootDefinition::UsSurvey, "Metric", "millimeter"},
      {6, DrawingSettings::FootDefinition::UsSurvey, "Metric", "meter"},
  };
  ScratchDir dir("units");
  for (const Row& row : rows) {
    AppCommandState st;
    st.drawingInsUnits = row.insUnits;
    st.drawingSettings.footDefinition = row.foot;
    st.surveyPoints = {MakePoint(1, 1, 2, 3)};
    const auto written = Export(st, dir, "units.xml");
    REQUIRE(written->ok);
    INFO("INSUNITS " << row.insUnits << " -> " << row.system << " " << row.linear);
    auto units = written->root().child("Units");
    REQUIRE(units);
    auto system = units.child(row.system);
    REQUIRE(system);
    CHECK(std::string(system.attribute("linearUnit").value()) == row.linear);
    // The schema requires all of these on the unit element.
    for (const char* required : {"areaUnit", "volumeUnit", "temperatureUnit", "pressureUnit"})
      CHECK_FALSE(std::string(system.attribute(required).value()).empty());
  }
}

TEST_CASE("LandXML does not invent a unit for a unitless drawing", "[landxml][issue1002][units]") {
  ScratchDir dir("unitless");
  AppCommandState st;
  st.drawingInsUnits = 0;
  st.surveyPoints = {MakePoint(1, 1, 2, 3)};
  const auto written = Export(st, dir, "unitless.xml");
  REQUIRE(written->ok);
  CHECK_FALSE(written->root().child("Units"));
  CHECK(LogMentions(written->log, "unitless"));
}

TEST_CASE("LandXML exports a built TIN surface with 1-based ids and its name", "[landxml][issue1002][surface]") {
  ScratchDir dir("surface");
  AppCommandState st;
  st.worldDocumentOriginX = 1000;
  st.worldDocumentOriginY = 5000;
  CadSurface surface = MakeSquareSurface("Existing <Ground> & \"Fill\"");
  surface.description = "Topo 2026";
  st.cadSurfaces.push_back(surface);

  const auto written = Export(st, dir, "surface.xml");
  REQUIRE(written->ok);
  auto surfaces = written->root().child("Surfaces");
  REQUIRE(CountChildren(surfaces, "Surface") == 1);
  auto node = surfaces.child("Surface");
  CHECK(std::string(node.attribute("name").value()) == "Existing <Ground> & \"Fill\"");
  CHECK(std::string(node.attribute("desc").value()) == "Topo 2026");
  auto definition = node.child("Definition");
  CHECK(std::string(definition.attribute("surfType").value()) == "TIN");
  CHECK(definition.attribute("elevMin").as_double() == 10.0);
  CHECK(definition.attribute("elevMax").as_double() == 13.0);

  auto points = definition.child("Pnts");
  REQUIRE(CountChildren(points, "P") == 4);
  int expectedId = 1;
  for (auto p : points.children("P")) CHECK(p.attribute("id").as_int() == expectedId++);
  const auto third = Numbers(points.child("P").next_sibling("P").next_sibling("P").text().get());
  REQUIRE(third.size() == 3);
  CHECK(third[0] == 5001.0);  // northing = y + origin
  CHECK(third[1] == 1001.0);  // easting = x + origin
  CHECK(third[2] == 12.0);

  auto faces = definition.child("Faces");
  REQUIRE(CountChildren(faces, "F") == 2);
  CHECK(Numbers(faces.child("F").text().get()) == std::vector<double>{1, 2, 3});
  CHECK(Numbers(faces.child("F").next_sibling("F").text().get()) == std::vector<double>{1, 3, 4});
}

TEST_CASE("LandXML names a surface it cannot export and still writes the rest", "[landxml][issue1002][surface]") {
  ScratchDir dir("surface-skips");
  AppCommandState st;
  st.surveyPoints = {MakePoint(1, 1, 2, 3)};
  st.cadSurfaces.push_back(MakeSquareSurface("Good"));
  CadSurface unbuilt;
  unbuilt.name = "Unbuilt";
  st.cadSurfaces.push_back(unbuilt);
  CadSurface grid = MakeSquareSurface("GridSurface");
  grid.kind = SurfaceKind::Grid;
  st.cadSurfaces.push_back(grid);
  CadSurface broken = MakeSquareSurface("Broken");
  const_cast<CadTin&>(*broken.tin).indices[2] = 99;  // refers to a vertex that does not exist
  st.cadSurfaces.push_back(broken);

  const auto written = Export(st, dir, "skips.xml");
  REQUIRE(written->ok);
  auto surfaces = written->root().child("Surfaces");
  REQUIRE(CountChildren(surfaces, "Surface") == 1);
  CHECK(std::string(surfaces.child("Surface").attribute("name").value()) == "Good");
  CHECK(LogMentions(written->log, "skipped surface \"Unbuilt\""));
  CHECK(LogMentions(written->log, "skipped surface \"GridSurface\""));
  CHECK(LogMentions(written->log, "skipped surface \"Broken\""));
  CHECK(CountChildren(written->root().child("CgPoints"), "CgPoint") == 1);
}

TEST_CASE("LandXML exports a drawing without any surfaces", "[landxml][issue1002][surface]") {
  ScratchDir dir("no-surfaces");
  AppCommandState st;
  st.surveyPoints = {MakePoint(1, 1, 2, 3)};
  const auto written = Export(st, dir, "no-surfaces.xml");
  REQUIRE(written->ok);
  CHECK_FALSE(written->root().child("Surfaces"));
  CHECK_FALSE(written->root().child("PlanFeatures"));
}

TEST_CASE("LandXML exports feature lines as one Line per segment with 3D ends and their names",
          "[landxml][issue1002][featureline]") {
  ScratchDir dir("featurelines");
  AppCommandState st;
  st.worldDocumentOriginX = 100;
  st.worldDocumentOriginY = 200;
  AddFeatureLine(st, {0, 0, 5, 10, 0, 6, 10, 10, 7}, false, "Top of <curb>", "Back & face");
  AddFeatureLine(st, {0, 0, 1, 4, 0, 2, 4, 3, 3}, true, "Pond rim");
  AddFeatureLine(st, {7, 7, 7}, false, "Lonely");  // one vertex: not a line

  const auto written = Export(st, dir, "featurelines.xml");
  REQUIRE(written->ok);
  auto features = written->root().child("PlanFeatures");
  REQUIRE(CountChildren(features, "PlanFeature") == 2);

  auto open = features.child("PlanFeature");
  CHECK(std::string(open.attribute("name").value()) == "Top of <curb>");
  CHECK(std::string(open.attribute("desc").value()) == "Back & face");
  auto openLines = open.child("CoordGeom");
  REQUIRE(CountChildren(openLines, "Line") == 2);
  auto firstLine = openLines.child("Line");
  CHECK(Numbers(firstLine.child("Start").text().get()) == std::vector<double>{200, 100, 5});
  CHECK(Numbers(firstLine.child("End").text().get()) == std::vector<double>{200, 110, 6});
  auto secondLine = firstLine.next_sibling("Line");
  CHECK(Numbers(secondLine.child("Start").text().get()) == std::vector<double>{200, 110, 6});
  CHECK(Numbers(secondLine.child("End").text().get()) == std::vector<double>{210, 110, 7});

  auto closed = open.next_sibling("PlanFeature");
  CHECK(std::string(closed.attribute("name").value()) == "Pond rim");
  CHECK_FALSE(closed.attribute("desc"));
  auto closedLines = closed.child("CoordGeom");
  REQUIRE(CountChildren(closedLines, "Line") == 3);  // two segments plus the closing one
  auto closing = closedLines.child("Line").next_sibling("Line").next_sibling("Line");
  CHECK(Numbers(closing.child("Start").text().get()) == std::vector<double>{203, 104, 3});
  CHECK(Numbers(closing.child("End").text().get()) == std::vector<double>{200, 100, 1});
  CHECK(LogMentions(written->log, "skipped feature line \"Lonely\""));
}

TEST_CASE("LandXML exports points, a surface and a feature line together", "[landxml][issue1002]") {
  ScratchDir dir("all");
  AppCommandState st;
  st.surveyPoints = {MakePoint(1, 1, 2, 3)};
  st.cadSurfaces.push_back(MakeSquareSurface("EG"));
  AddFeatureLine(st, {0, 0, 0, 1, 1, 1}, false, "FL");
  const auto written = Export(st, dir, "all.xml");
  REQUIRE(written->ok);
  CHECK(LogMentions(written->log, "wrote 1 point(s), 1 surface(s) and 1 feature line(s)"));
  CHECK(written->root().child("CgPoints"));
  CHECK(written->root().child("Surfaces"));
  CHECK(written->root().child("PlanFeatures"));
}

TEST_CASE("LandXML refuses an empty drawing and leaves an existing file alone", "[landxml][issue1002]") {
  ScratchDir dir("empty");
  const fs::path target = dir.path / "keep.xml";
  { std::ofstream(target) << "previous export"; }
  AppCommandState st;
  std::vector<std::string> log;
  CHECK_FALSE(ExportLandXmlFile(st, target.u8string().c_str(), log));
  CHECK(LogMentions(log, "nothing to export"));
  std::ifstream in(target);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(content == "previous export");
  CHECK_FALSE(fs::exists(fs::path(target.u8string() + ".tmp")));

  // An unbuilt surface alone is not exportable data either.
  CadSurface unbuilt;
  unbuilt.name = "Unbuilt";
  st.cadSurfaces.push_back(unbuilt);
  log.clear();
  CHECK_FALSE(ExportLandXmlFile(st, target.u8string().c_str(), log));
  CHECK(LogMentions(log, "nothing to export"));
}

TEST_CASE("LandXML replaces characters XML cannot hold instead of writing an unreadable file",
          "[landxml][issue1002]") {
  ScratchDir dir("sanitize");
  AppCommandState st;
  std::string description = "Line1\nLine2\tTab";
  description += '\x01';        // control character: not allowed in XML 1.0
  description += "\xFF\xFE";    // not UTF-8
  description += "caf\xC3\xA9"; // valid UTF-8 must survive
  st.surveyPoints = {MakePoint(1, 1, 2, 3, description.c_str())};
  const auto written = Export(st, dir, "sanitize.xml");
  REQUIRE(written->ok);
  const std::string read = written->root().child("CgPoints").child("CgPoint").attribute("desc").value();
  CHECK(read == "Line1\nLine2\tTab\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "caf\xC3\xA9");
  CHECK(LogMentions(written->log, "replaced 3 character(s)"));
}

TEST_CASE("LandXML skips non-finite points and warns about repeated point numbers", "[landxml][issue1002]") {
  ScratchDir dir("odd-points");
  AppCommandState st;
  st.surveyPoints = {MakePoint(1, 1, 2, 3), MakePoint(2, std::numeric_limits<double>::quiet_NaN(), 2, 3),
                     MakePoint(1, 5, 6, 7)};
  const auto written = Export(st, dir, "odd.xml");
  REQUIRE(written->ok);
  CHECK(CountChildren(written->root().child("CgPoints"), "CgPoint") == 2);
  CHECK(LogMentions(written->log, "skipped point 2"));
  CHECK(LogMentions(written->log, "point number 1 is used more than once"));
}

TEST_CASE("LandXML reports a failed write and leaves no partial file", "[landxml][issue1002]") {
  ScratchDir dir("write-failure");
  AppCommandState st;
  st.surveyPoints = {MakePoint(1, 1, 2, 3)};

  std::vector<std::string> log;
  const fs::path missingFolder = dir.path / "no-such-folder" / "out.xml";
  CHECK_FALSE(ExportLandXmlFile(st, missingFolder.u8string().c_str(), log));
  CHECK(LogMentions(log, "EXPORTLANDXML"));
  CHECK(LogMentions(log, "could not write"));

  // The destination is a folder: the final replace fails, and the temporary file is cleaned up.
  const fs::path folder = dir.path / "folder.xml";
  fs::create_directories(folder);
  log.clear();
  CHECK_FALSE(ExportLandXmlFile(st, folder.u8string().c_str(), log));
  CHECK(LogMentions(log, "could not replace"));
  CHECK(fs::is_directory(folder));
  CHECK_FALSE(fs::exists(fs::path(folder.u8string() + ".tmp")));

  log.clear();
  CHECK_FALSE(ExportLandXmlFile(st, "", log));
  CHECK(LogMentions(log, "no destination"));
}

TEST_CASE("LandXML overwrites a previous export and accepts a non-ASCII path", "[landxml][issue1002]") {
  ScratchDir dir("overwrite");
  AppCommandState st;
  st.surveyPoints = {MakePoint(1, 1, 2, 3)};
  const fs::path target = dir.path / fs::u8path("s\xC3\xA9" "ance \xC3\xBC" "ber.xml");
  { std::ofstream(target) << "old"; }
  std::vector<std::string> log;
  REQUIRE(ExportLandXmlFile(st, target.u8string().c_str(), log));
  pugi::xml_document document;
  REQUIRE(document.load_file(target.c_str()));
  CHECK(document.child("LandXML").child("CgPoints"));
}

TEST_CASE("EXPORTLANDXML writes the file named on the command line", "[landxml][issue1002][command]") {
  ScratchDir dir("command");
  AppCommandState st;
  st.surveyPoints = {MakePoint(7, 1, 2, 3, "Spot")};
  const fs::path target = dir.path / "a folder" / "site plan.xml";
  fs::create_directories(target.parent_path());

  std::vector<std::string> log;
  std::string command = "EXPORTLANDXML \"" + target.u8string() + "\"";
  std::vector<char> buffer(command.begin(), command.end());
  buffer.push_back('\0');
  ProcessCommandLineSubmit(buffer.data(), static_cast<int>(buffer.size()), st, log);
  REQUIRE(fs::exists(target));
  pugi::xml_document document;
  REQUIRE(document.load_file(target.c_str()));
  CHECK(document.child("LandXML").child("CgPoints").child("CgPoint").attribute("name").as_int() == 7);
  CHECK(LogMentions(log, "wrote 1 point(s)"));

  // Bare form names the missing destination rather than doing nothing.
  log.clear();
  char bare[] = "EXPORTLANDXML";
  ProcessCommandLineSubmit(bare, sizeof(bare), st, log);
  CHECK(LogMentions(log, "type a destination"));
}

TEST_CASE("LandXML exports a real built surface from the demo drawing", "[landxml][issue1002][integration]") {
  const fs::path demo = fs::path(GOSURVEY_SAMPLES_DIR) / "surface-demo.dwg";
  REQUIRE(fs::exists(demo));
  ScratchDir dir("demo");
  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(ImportDwgFile(st, demo.u8string().c_str(), log));
  REQUIRE(CreateSurfaceFromPointGroups(st, "Test EG", {"Existing Ground"}, log) >= 0);
  REQUIRE(st.cadSurfaces.back().tin);
  const CadTin& tin = *st.cadSurfaces.back().tin;
  REQUIRE(tin.triangleCount() > 0);

  const auto written = Export(st, dir, "demo.xml");
  REQUIRE(written->ok);
  CHECK(CountChildren(written->root().child("CgPoints"), "CgPoint") == static_cast<int>(st.surveyPoints.size()));
  pugi::xml_node surface;
  for (auto node : written->root().child("Surfaces").children("Surface"))
    if (std::string(node.attribute("name").value()) == "Test EG") surface = node;
  REQUIRE(surface);
  auto definition = surface.child("Definition");
  CHECK(CountChildren(definition.child("Pnts"), "P") == tin.vertexCount());
  CHECK(CountChildren(definition.child("Faces"), "F") == tin.triangleCount());
  // First vertex in world coordinates, northing first.
  const auto first = Numbers(definition.child("Pnts").child("P").text().get());
  REQUIRE(first.size() == 3);
  CHECK(first[0] == tin.vertsXyz[1] + st.worldDocumentOriginY);
  CHECK(first[1] == tin.vertsXyz[0] + st.worldDocumentOriginX);
  CHECK(first[2] == tin.vertsXyz[2]);
}
