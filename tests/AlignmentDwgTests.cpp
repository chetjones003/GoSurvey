// REQ-496 / issue #121 - alignments in real DWG files: GoSurvey save -> reopen through the trailer, the ordinary
// LINE / ARC / LWPOLYLINE entities written for AutoCAD, and their re-read when the trailer is gone.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CadCommands.hpp"
#include "DwgIo.hpp"
#include "GsIo.hpp"
#include "LibreDwgCad.hpp"
#include "LibreDwgCivil.hpp"

#include <algorithm>
#include <string_view>
#if defined(__cplusplus) && !defined(restrict)
#define restrict
#endif
extern "C" {
#include <dwg.h>
#include <dwg_api.h>
}

namespace {

struct ScratchDir {
  std::filesystem::path path;
  explicit ScratchDir(const char* tag) {
    path = std::filesystem::temp_directory_path() / ("gosurvey-alignment-dwg-" + std::string(tag));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  std::vector<char> buf(text.begin(), text.end());
  buf.resize(std::max<size_t>(buf.size() + 1, 512), '\0');
  ProcessCommandLineSubmit(buf.data(), buf.size(), st, log);
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  return st;
}

bool LogHas(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& line : log)
    if (line.find(needle) != std::string::npos) return true;
  return false;
}

/// A state-plane road: tangent / spiral-curve-spiral / tangent / curve / tangent, with every stored field set.
AppCommandState RichRoad(bool spirals) {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, std::string("ALIGNMENTCURVESETTINGS RADIUS=200") + (spirals ? ", SPIRALIN=60, SPIRALOUT=60" : ""), log);
  Submit(st, "ALIGNMENT Main Road, TYPE=Rail, DESCRIPTION=Haul road, START=5+00, LAYER=C-ROAD", log);
  for (const char* line : {"ALIGNMENTLAYOUT Main Road, CURVES", "6500000,2100000", "6500000,2100600", "6500500,2100900",
                           "6501100,2100900", "6501400,2100500", ""})
    Submit(st, line, log);
  Submit(st, "ALIGNMENTSTYLE NEW, Haul", log);
  Submit(st, "ALIGNMENTSTYLE ASSIGN, Main Road, Haul", log);
  Submit(st, "ALIGNMENTLABELSET NEW, Major", log);
  Submit(st, "ALIGNMENTLABELSET ASSIGN, Main Road, Major", log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Haul Checks, RAIL", log);
  Submit(st, "ALIGNMENTCRITERIA Main Road, Haul Checks", log);
  Submit(st, "ALIGNMENTSPEED Main Road, 5+00, 50", log);
  Submit(st, "ALIGNMENTLOCK Main Road, 1, LENGTH, ON", log);
  Submit(st, "ALIGNMENTSTATION Main Road, EQ, ADD, 900, 1000, INC", log);
  Submit(st, "ALIGNMENTLABEL Main Road, STATIONLABEL, RELATIVE, 7+00, 10", log);
  Submit(st, "OFFSETALIGNMENT Main Road, 12", log);
  return st;
}

void RequireSameAlignment(const alignment::Alignment& a, const alignment::Alignment& b) {
  CHECK(b.id == a.id);
  CHECK(b.name == a.name);
  CHECK(b.description == a.description);
  CHECK(b.layer == a.layer);
  CHECK(b.style == a.style);
  CHECK(b.labelSet == a.labelSet);
  CHECK(b.checkSetName == a.checkSetName);
  CHECK(b.type == a.type);
  CHECK(b.startingStation == a.startingStation);
  CHECK(b.parentId == a.parentId);
  CHECK(b.offsetDistance == a.offsetDistance);
  REQUIRE(b.Entities().size() == a.Entities().size());
  for (size_t i = 0; i < a.Entities().size(); ++i) {
    const alignment::Entity& x = a.Entities()[i];
    const alignment::Entity& y = b.Entities()[i];
    CHECK(y.kind == x.kind);
    CHECK(std::fabs(y.start.x - x.start.x) <= 1e-9);
    CHECK(std::fabs(y.start.y - x.start.y) <= 1e-9);
    CHECK(std::fabs(y.startAzimuth - x.startAzimuth) <= 1e-9);
    CHECK(std::fabs(y.length - x.length) <= 1e-9);
    CHECK(std::fabs(y.radius - x.radius) <= 1e-9);
    CHECK(std::fabs(y.startRadius - x.startRadius) <= 1e-9);
    CHECK(y.right == x.right);
    CHECK(y.constraint == x.constraint);
    CHECK(y.locks == x.locks);
    CHECK(y.definedByLength == x.definedByLength);
  }
  REQUIRE(b.equations.size() == a.equations.size());
  for (size_t i = 0; i < a.equations.size(); ++i) {
    CHECK(b.equations[i].rawBack == a.equations[i].rawBack);
    CHECK(b.equations[i].ahead == a.equations[i].ahead);
    CHECK(b.equations[i].increasing == a.equations[i].increasing);
  }
  REQUIRE(b.designSpeeds.size() == a.designSpeeds.size());
  REQUIRE(b.stationLabels.size() == a.stationLabels.size());
  for (size_t i = 0; i < a.stationLabels.size(); ++i) {
    CHECK(b.stationLabels[i].internalStation == a.stationLabels[i].internalStation);
    CHECK(b.stationLabels[i].offset == a.stationLabels[i].offset);
  }
}

/// World coordinates of a station, so drawings with different document origins compare on the ground.
alignment::Point2 WorldAt(const AppCommandState& st, const alignment::Alignment& a, double station) {
  const alignment::Point2 p = a.GetPointAtStation(station);
  return {p.x + st.worldDocumentOriginX, p.y + st.worldDocumentOriginY};
}

struct TaggedCounts {
  int lines = 0, arcs = 0, polylines = 0, polylineVertices = 0;
};

/// Counts the GOSURVEY-tagged alignment entities a DWG holds, as AutoCAD would read them.
TaggedCounts CountTaggedEntities(const std::string& path) {
  TaggedCounts c;
  Dwg_Data dwg{};
  REQUIRE(dwg_read_file(path.c_str(), &dwg) < DWG_ERR_CRITICAL);
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object& o = dwg.object[i];
    if (o.supertype != DWG_SUPERTYPE_ENTITY || o.tio.entity == nullptr || o.tio.entity->num_eed == 0) continue;
    const Dwg_Eed_Data* first = o.tio.entity->eed[0].data;
    if (first == nullptr || first->code != 0) continue;
    const std::string text = first->u.eed_0.is_tu != 0
                                 ? libredwgcad_detail::DecodeDwgString(first->u.eed_0.string, true)
                                 : std::string(reinterpret_cast<const char*>(first->u.eed_0.string), first->u.eed_0.length);
    if (text != "ALIGNMENT") continue;
    if (o.fixedtype == DWG_TYPE_LINE) ++c.lines;
    if (o.fixedtype == DWG_TYPE_ARC) ++c.arcs;
    if (o.fixedtype == DWG_TYPE_LWPOLYLINE) {
      ++c.polylines;
      c.polylineVertices += static_cast<int>(o.tio.entity->tio.LWPOLYLINE->num_points);
    }
  }
  dwg_free(&dwg);
  return c;
}

std::string TrailerJson(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string json;
  REQUIRE(TryGoSurveyDwgPayloadFromBytes(bytes, json));
  return json;
}

}  // namespace

TEST_CASE("Alignments survive a DWG save and reopen with every stored value", "[req496][alignment][dwg]") {
  for (const bool spirals : {false, true}) {
    const AppCommandState st = RichRoad(spirals);
    // Spiral offsets are refused (REQ-488), so only the curve road has its linked offset.
    const size_t count = spirals ? 1 : 2;
    REQUIRE(st.alignments.size() == count);
    REQUIRE(st.alignments[0].Entities().size() == (spirals ? 13u : 7u));  // 4 tangents, 3 curves (+ 6 spirals)
    REQUIRE(st.alignments[0].type == alignment::AlignmentType::Rail);
    REQUIRE(st.alignments[0].layer == "C-ROAD");
    REQUIRE(st.alignments[0].style == "Haul");
    REQUIRE(st.alignments[0].labelSet == "Major");
    REQUIRE(st.alignments[0].checkSetName == "Haul Checks");
    REQUIRE(st.alignments[0].equations.size() == 1);
    REQUIRE(st.alignments[0].designSpeeds.size() == 1);
    REQUIRE(st.alignments[0].stationLabels.size() == 1);
    REQUIRE(st.alignments[0].Entities()[0].locks != 0);
    if (!spirals) REQUIRE(st.alignments[1].parentId == st.alignments[0].id);

    ScratchDir dir(spirals ? "roundtrip-spirals" : "roundtrip");
    const std::string path = (dir.path / "road.dwg").string();
    std::vector<std::string> log;
    REQUIRE(SaveDrawingDocument(st, path.c_str(), log));
    AppCommandState back = Drawing();
    REQUIRE(OpenDrawingDocument(back, path.c_str(), log));
    REQUIRE(back.alignments.size() == count);
    for (size_t i = 0; i < count; ++i) RequireSameAlignment(st.alignments[i], back.alignments[i]);
    for (double station : {500.0, 812.5, 1400.0, 1900.0}) {  // on the ground, whatever the document origin
      const alignment::Point2 p = WorldAt(st, st.alignments[0], station);
      const alignment::Point2 q = WorldAt(back, back.alignments[0], station);
      CHECK(std::fabs(p.x - q.x) <= 1e-9);
      CHECK(std::fabs(p.y - q.y) <= 1e-9);
    }
    CHECK(std::fabs(back.alignments[0].Length() - st.alignments[0].Length()) <= 1e-9);
    // The trailer is authoritative: the interchange entities are not read as a second copy.
    CHECK(back.userLinesFlat.empty());
    CHECK(back.userArcs.empty());
    CHECK(back.userPolylineVerts.empty());
  }
}

TEST_CASE("An alignment is written for AutoCAD as lines, arcs and one polyline per spiral", "[req496][alignment][dwg]") {
  const AppCommandState st = RichRoad(true);
  ScratchDir dir("entities");
  const std::string path = (dir.path / "road.dwg").string();
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, path.c_str(), log));
  const TaggedCounts c = CountTaggedEntities(path);
  CHECK(c.lines == 4);
  CHECK(c.arcs == 3);
  CHECK(c.polylines == 6);
  // 60-unit spirals ending at R 200 at a 0.001 chord height: spacing ~1.26, so ~49 vertices each, not 60 000 lines.
  CHECK(c.polylineVertices > 6 * 20);
  CHECK(c.polylineVertices < 6 * 200);
}

TEST_CASE("Spiral polyline vertices are exact and their chords stay within the export tolerance",
          "[req496][alignment][dwg]") {
  const AppCommandState st = RichRoad(true);
  ScratchDir dir("chords");
  const std::string path = (dir.path / "road.dwg").string();
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, path.c_str(), log));
  const alignment::Alignment& road = st.alignments[0];
  Dwg_Data dwg{};
  REQUIRE(dwg_read_file(path.c_str(), &dwg) < DWG_ERR_CRITICAL);
  int checked = 0;
  double worstChord = 0.0, worstVertex = 0.0;
  for (BITCODE_BL i = 0; i < dwg.num_objects && checked < 6; ++i) {
    const Dwg_Object& o = dwg.object[i];
    if (o.fixedtype != DWG_TYPE_LWPOLYLINE || o.tio.entity == nullptr) continue;
    const Dwg_Entity_LWPOLYLINE* p = o.tio.entity->tio.LWPOLYLINE;
    for (BITCODE_BL v = 0; v < p->num_points; ++v) {
      const double x = p->points[v].x - st.worldDocumentOriginX, y = p->points[v].y - st.worldDocumentOriginY;
      worstVertex = std::max(worstVertex, std::fabs(road.GetStationOffset(x, y).offset));
      if (v + 1 == p->num_points) break;
      const double mx = 0.5 * (p->points[v].x + p->points[v + 1].x) - st.worldDocumentOriginX;
      const double my = 0.5 * (p->points[v].y + p->points[v + 1].y) - st.worldDocumentOriginY;
      worstChord = std::max(worstChord, std::fabs(road.GetStationOffset(mx, my).offset));
    }
    ++checked;
  }
  dwg_free(&dwg);
  CHECK(checked == 6);
  CHECK(worstVertex <= 1e-6);  // every vertex reads back where it was written (LibreDWG bit_write_DD, REQ-496)
  CHECK(worstChord <= 0.0011);
}

TEST_CASE("Station and offset on a spiral are exact, not the nearest grid sample", "[req484][req496][alignment]") {
  const AppCommandState st = RichRoad(true);
  const alignment::Alignment& road = st.alignments[0];
  REQUIRE(road.Entities()[1].kind == alignment::EntityKind::Spiral);
  const double s0 = road.EntityStartStation(1);
  for (int k = 0; k <= 48; ++k) {
    const double s = s0 + road.Entities()[1].length * static_cast<double>(k) / 48.0;
    for (double offset : {0.0, 7.5, -12.0}) {
      const alignment::Point2 p = road.GetPointFromStationOffset(s, offset);
      const alignment::StationOffset so = road.GetStationOffset(p.x, p.y);
      CHECK(std::fabs(so.internalStation - s) <= 1e-8);
      CHECK(std::fabs(so.offset - offset) <= 1e-8);
    }
  }
}

TEST_CASE("A state-plane polyline reads back from a DWG at full precision", "[req496][dwg]") {
  AppCommandState st = Drawing();
  for (int k = 0; k < 40; ++k) {  // neighbours share their high bytes, so most vertices are written as patches
    const double t = 0.37 * static_cast<double>(k);
    for (double v : {6500000.0 + t * 1.013, 2100000.0 + t * t * 0.0071, 0.0}) st.userPolylineVerts.push_back(v);
  }
  st.userPolylineOffsets = {0, 40};
  st.userPolylineClosed = {0};
  st.userPolylineAttrs = {EntityAttributes{}};
  ScratchDir dir("polyline-precision");
  const std::string path = (dir.path / "pl.dwg").string();
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, path.c_str(), log, false));
  Dwg_Data dwg{};
  REQUIRE(dwg_read_file(path.c_str(), &dwg) < DWG_ERR_CRITICAL);
  int polylines = 0;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object& o = dwg.object[i];
    if (o.fixedtype != DWG_TYPE_LWPOLYLINE || o.tio.entity == nullptr) continue;
    const Dwg_Entity_LWPOLYLINE* p = o.tio.entity->tio.LWPOLYLINE;
    REQUIRE(p->num_points == 40);
    for (BITCODE_BL v = 0; v < p->num_points; ++v) {
      CHECK(p->points[v].x == st.userPolylineVerts[v * 3] + st.worldDocumentOriginX);
      CHECK(p->points[v].y == st.userPolylineVerts[v * 3 + 1] + st.worldDocumentOriginY);
    }
    ++polylines;
  }
  dwg_free(&dwg);
  CHECK(polylines == 1);
}

TEST_CASE("A DWG without GoSurvey's data reads the alignment back from its entities", "[req496][req485][alignment][dwg]") {
  AppCommandState st = RichRoad(false);
  st.alignments.resize(1);  // the road only: four tangents and three curves
  REQUIRE(st.alignments[0].Entities().size() == 7);
  ScratchDir dir("foreign");
  const std::string path = (dir.path / "road.dwg").string();
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, path.c_str(), log, false));  // ordinary entities only, as AutoCAD saves them

  AppCommandState in = Drawing();
  std::vector<std::string> readLog;
  REQUIRE(ImportLibreCadFile(in, path.c_str(), readLog, false));
  REQUIRE(in.alignments.size() == 1);
  const alignment::Alignment& a = st.alignments[0];
  const alignment::Alignment& b = in.alignments[0];
  CHECK(b.name == "Main Road");
  CHECK(b.layer == "C-ROAD");
  CHECK(b.startingStation == Catch::Approx(500.0).margin(1e-9));
  REQUIRE(b.Entities().size() == a.Entities().size());
  for (size_t i = 0; i < a.Entities().size(); ++i) {
    CHECK(b.Entities()[i].kind == a.Entities()[i].kind);
    CHECK(b.Entities()[i].length == Catch::Approx(a.Entities()[i].length).margin(1e-6));
    CHECK(b.Entities()[i].radius == Catch::Approx(a.Entities()[i].radius).margin(1e-6));
    CHECK(b.Entities()[i].right == a.Entities()[i].right);
  }
  const alignment::Point2 p0 = WorldAt(st, a, 500.0), q0 = WorldAt(in, b, 500.0);
  CHECK(std::fabs(p0.x - q0.x) <= 1e-6);
  CHECK(std::fabs(p0.y - q0.y) <= 1e-6);
  CHECK(LogHas(readLog, "alignment \"Main Road\" rebuilt"));
  CHECK(in.userLinesFlat.empty());  // read as the alignment, not also as linework
  CHECK(in.userArcs.empty());
}

TEST_CASE("Spirals in a DWG without GoSurvey's data come back as tangents, and the log says so",
          "[req496][alignment][dwg]") {
  AppCommandState st = RichRoad(true);
  st.alignments.resize(1);
  ScratchDir dir("foreign-spiral");
  const std::string path = (dir.path / "road.dwg").string();
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, path.c_str(), log, false));
  AppCommandState in = Drawing();
  std::vector<std::string> readLog;
  REQUIRE(ImportLibreCadFile(in, path.c_str(), readLog, false));
  REQUIRE(in.alignments.size() == 1);
  CHECK(in.alignments[0].Length() == Catch::Approx(st.alignments[0].Length()).margin(0.01));
  CHECK(LogHas(readLog, "spirals were read as tangents"));
}

TEST_CASE("Tagged entities that do not chain stay lines and arcs, with the reason", "[req496][alignment][dwg]") {
  AppCommandState st = RichRoad(false);
  st.alignments.resize(1);
  alignment::Alignment far = st.alignments[0];
  far.Translate(5000.0, 0.0);  // same name, not connected: the conversion refuses the gap
  st.alignments.push_back(far);
  ScratchDir dir("foreign-gap");
  const std::string path = (dir.path / "road.dwg").string();
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, path.c_str(), log, false));
  AppCommandState in = Drawing();
  std::vector<std::string> readLog;
  REQUIRE(ImportLibreCadFile(in, path.c_str(), readLog, false));
  CHECK(in.alignments.empty());
  CHECK(LogHas(readLog, "could not be rebuilt"));
  CHECK(in.userLinesFlat.size() == 8 * 6);  // 4 tangents x 2 copies, 6 values each
  CHECK(in.userArcs.size() == 6);
}

TEST_CASE("A drawing saved before alignments re-saves with the same data", "[req496][alignment][dwg]") {
  const std::string source = std::string(GOSURVEY_SAMPLES_DIR) + "/font-demo.dwg";
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  REQUIRE(OpenDrawingDocument(st, source.c_str(), log));
  CHECK(st.alignments.empty());
  ScratchDir dir("legacy");
  const std::string path = (dir.path / "font-demo.dwg").string();
  REQUIRE(SaveDrawingDocument(st, path.c_str(), log));
  nlohmann::json before = nlohmann::json::parse(TrailerJson(source));
  nlohmann::json after = nlohmann::json::parse(TrailerJson(path));
  before.erase("version");
  after.erase("version");
  for (const char* key : {"alignments", "alignmentStyles", "alignmentLabelSets", "alignmentLabelStyles",
                          "alignmentCheckSets"})
    CHECK_FALSE(after.contains(key));
  // Other features since this file was saved add their own default keys; alignment support adds none.
  for (const nlohmann::json& change : nlohmann::json::diff(before, after)) {
    INFO(change.dump());
    CHECK(change["path"].get<std::string>().find("lignment") == std::string::npos);
  }
  // And a second save of the re-saved drawing changes nothing (REQ-079).
  AppCommandState again = Drawing();
  REQUIRE(OpenDrawingDocument(again, path.c_str(), log));
  const std::string path2 = (dir.path / "font-demo-2.dwg").string();
  REQUIRE(SaveDrawingDocument(again, path2.c_str(), log));
  CHECK(TrailerJson(path2) == TrailerJson(path));
}

TEST_CASE("A Civil 3D alignment imports as an editable alignment on Civil 3D's ground positions",
          "[req496][alignment][dwg][civil3d]") {
  const std::string base = std::string(GOSURVEY_SAMPLES_DIR) + "/civil3d-alignment-fixture";
  std::ifstream refFile(base + "-reference.json");
  REQUIRE(refFile);
  const nlohmann::json ref = nlohmann::json::parse(refFile);
  AppCommandState in = Drawing();
  std::vector<std::string> log;
  REQUIRE(ImportLibreCadFile(in, (base + ".dwg").c_str(), log, false));
  INFO([&] { std::string all; for (const std::string& l : log) all += l + "\n"; return all; }());
  REQUIRE(in.alignments.size() == 1);
  const alignment::Alignment& a = in.alignments[0];
  CHECK(a.name == ref["name"].get<std::string>());
  CHECK(a.startingStation == ref["startingStation"].get<double>());
  REQUIRE(a.Entities().size() == 9);  // T, spiral-curve-spiral, T, curve, T, curve, T
  CHECK(a.Entities()[1].kind == alignment::EntityKind::Spiral);
  CHECK(a.Entities()[2].radius == Catch::Approx(237.5).margin(1e-9));
  CHECK(a.Entities()[5].radius == Catch::Approx(412.25).margin(1e-9));
  CHECK(a.Entities()[7].radius == Catch::Approx(180.0).margin(1e-9));
  // GoSurvey's clothoid and Civil 3D's place the spiral group's ends ~7e-6 ft apart (REQ-503 open question 3); the
  // tangent before the group matches exactly.
  constexpr double kClothoidAgreement = 2e-5;
  CHECK(std::fabs(a.Length() - ref["length"].get<double>()) <= kClothoidAgreement);
  REQUIRE(a.equations.size() == 1);
  CHECK(a.equations[0].rawBack == 1400.0);
  CHECK(a.equations[0].ahead == 1650.0);
  CHECK(a.equations[0].increasing);
  // Every 100 ft station Civil 3D reported, at its raw station: exact before the spirals, within the clothoid
  // agreement after them.
  double worstBefore = 0.0, worstAfter = 0.0;
  for (const nlohmann::json& s : ref["stations"]) {
    const alignment::Point2 p = WorldAt(in, a, s[0].get<double>());
    const double d = std::max(std::fabs(p.x - s[1].get<double>()), std::fabs(p.y - s[2].get<double>()));
    double& worst = s[0].get<double>() < a.EntityStartStation(1) ? worstBefore : worstAfter;
    worst = std::max(worst, d);
  }
  CHECK(ref["stations"].size() == 24);
  CHECK(worstBefore <= 1e-6);
  CHECK(worstAfter <= kClothoidAgreement);
  CHECK(LogHas(log, "Civil 3D alignment \"GS Fixture\": 9 entities, 1 station equation(s)"));
  CHECK_FALSE(LogHas(log, "\"AECC_ALIGNMENT(semantic data not decoded"));  // its preview is not read as well
}

TEST_CASE("Civil 3D station equations keep their direction", "[req496][alignment][dwg][civil3d]") {
  AppCommandState in = Drawing();
  std::vector<std::string> log;
  REQUIRE(ImportLibreCadFile(in, (std::string(GOSURVEY_SAMPLES_DIR) + "/civil3d-alignment-equations.dwg").c_str(),
                             log, false));
  REQUIRE(in.alignments.size() == 1);
  const alignment::Alignment& a = in.alignments[0];
  REQUIRE(a.equations.size() == 2);
  CHECK(a.equations[0].rawBack == 1234.5);
  CHECK(a.equations[0].ahead == 1345.25);
  CHECK(a.equations[0].increasing);
  CHECK(a.equations[1].rawBack == 1876.75);
  CHECK(a.equations[1].ahead == 1765.5);
  CHECK_FALSE(a.equations[1].increasing);
}

TEST_CASE("The Civil 3D alignment decoder refuses what it cannot read", "[req496][alignment][civil3d]") {
  CHECK_FALSE(libredwgcivil::DecodeAlignment(nullptr, 0, 0, 0).has_value());
  const std::vector<unsigned char> zeros(1000, 0);
  CHECK_FALSE(libredwgcivil::DecodeAlignment(zeros.data(), zeros.size() * 8, zeros.size() * 8, 0).has_value());

  // The fixture's own record, with the sentinel before the 412.25 curve broken: that curve is no longer found, so
  // the link from its tangents leads nowhere and the importer refuses the alignment rather than guessing.
  Dwg_Data dwg{};
  REQUIRE(dwg_read_file((std::string(GOSURVEY_SAMPLES_DIR) + "/civil3d-alignment-fixture.dwg").c_str(), &dwg) <
          DWG_ERR_CRITICAL);
  std::vector<unsigned char> bits;
  size_t bitCount = 0, stringStart = 0, stringBits = 0;
  for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
    const Dwg_Object& o = dwg.object[i];
    if (o.type < 500 || o.type - 500 >= dwg.num_classes ||
        std::string_view(dwg.dwg_class[o.type - 500].dxfname) != "AECC_ALIGNMENT")
      continue;
    bitCount = o.num_unknown_bits;
    bits.assign(o.unknown_bits, o.unknown_bits + (bitCount + 7) / 8);
    stringBits = o.stringstream_size;  // laid out as the importer reads it
    stringStart = o.bitsize - (o.common_size + o.bitsize_pos) - (stringBits >= 32768 ? 33 : 17) - stringBits;
  }
  dwg_free(&dwg);
  REQUIRE(bitCount > 0);
  const auto whole = libredwgcivil::DecodeAlignment(bits.data(), bitCount, stringStart, stringBits);
  REQUIRE(whole.has_value());
  CHECK(whole->tangents.size() == 4);
  REQUIRE(whole->corners.size() == 3);
  constexpr size_t kSentinelBeforeCurve2 = 2474;  // the third -1.0 before the 412.25 curve
  bits[kSentinelBeforeCurve2 / 8 + 2] ^= 0x10;
  const auto broken = libredwgcivil::DecodeAlignment(bits.data(), bitCount, stringStart, stringBits);
  REQUIRE(broken.has_value());
  CHECK(broken->corners.size() == 2);
  CHECK(std::none_of(broken->corners.begin(), broken->corners.end(),
                     [](const libredwgcivil::AlignmentCornerRecord& c) { return c.radius == 412.25; }));
}

TEST_CASE("Per-PI curve settings build each corner its own way", "[req496][alignment]") {
  const std::vector<alignment::Point2> pis = {{0, 0}, {0, 600}, {500, 900}, {1100, 900}};
  std::vector<alignment::CurveSettings> corners(4, alignment::CurveSettings{false, 0.0, 0.0, 0.0});
  corners[1] = {true, 237.5, 60.0, 75.0};
  corners[2] = {true, 412.25, 0.0, 0.0};
  alignment::Alignment a;
  std::string why;
  REQUIRE(alignment::BuildFromPis(pis, corners, &a, &why));
  REQUIRE(a.Entities().size() == 7);  // T, S, C, S, T, C, T
  CHECK(a.Entities()[1].length == Catch::Approx(60.0).margin(1e-9));
  CHECK(a.Entities()[3].length == Catch::Approx(75.0).margin(1e-9));
  CHECK(a.Entities()[5].radius == Catch::Approx(412.25).margin(1e-9));
  corners.pop_back();
  CHECK_FALSE(alignment::BuildFromPis(pis, corners, &a, &why));  // one setting per PI
  CHECK(a.Empty());
}

// Hidden fixture writer for the AutoCAD oracle (REQ-496 acceptance 2): run with GOSURVEY_ORACLE_DIR=<dir>
// GoSurveyTests "[.alignment-audit]", then audit <dir>/*.dwg with accoreconsole.
TEST_CASE("Write alignment DWGs for accoreconsole", "[.alignment-audit]") {
  const char* root = std::getenv("GOSURVEY_ORACLE_DIR");
  REQUIRE(root != nullptr);
  std::filesystem::create_directories(root);
  std::vector<std::string> log;
  REQUIRE(SaveDrawingDocument(RichRoad(true), (std::filesystem::path(root) / "alignment-spirals.dwg").string().c_str(), log));
  REQUIRE(SaveDrawingDocument(RichRoad(false), (std::filesystem::path(root) / "alignment-curves.dwg").string().c_str(), log));
}

// Reads back what AutoCAD saved from those fixtures (GOSURVEY_ORACLE_DIR/acad-*.dwg): no trailer survives an AutoCAD
// save, so the alignments come from the tagged entities.
TEST_CASE("Read alignment DWGs saved by AutoCAD", "[.alignment-acad-read]") {
  const char* root = std::getenv("GOSURVEY_ORACLE_DIR");
  REQUIRE(root != nullptr);
  const std::string path = (std::filesystem::path(root) / "acad-alignment-curves.dwg").string();
  AppCommandState in = Drawing();
  std::vector<std::string> log;
  REQUIRE(OpenDrawingDocument(in, path.c_str(), log));
  for (const std::string& line : log) WARN(line);
  REQUIRE(in.alignments.size() == 2);
  const AppCommandState st = RichRoad(false);
  for (size_t i = 0; i < 2; ++i) {
    CHECK(in.alignments[i].Entities().size() == st.alignments[i].Entities().size());
    CHECK(in.alignments[i].Length() == Catch::Approx(st.alignments[i].Length()).margin(1e-6));
  }
}
