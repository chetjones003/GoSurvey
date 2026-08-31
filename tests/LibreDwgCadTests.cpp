#include "DxfIo.hpp"
#include "DwgIo.hpp"
#include "LibreDwgCad.hpp"

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct ScratchDir {
  std::filesystem::path path;
  explicit ScratchDir(const char* tag) {
    static int counter = 0;
    path = std::filesystem::temp_directory_path() /
           ("gosurvey-cadio-" + std::string(tag) + "-" + std::to_string(++counter));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

void OneLine(AppCommandState& st) {
  st.userLinesFlat = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
  st.userLineAttrs = {EntityAttributes{}};
}

}  // namespace

TEST_CASE("LibreDWG DXF round-trips a model-space LINE", "[dxf][libredwg]") {
  ScratchDir dir("dxf");
  const auto p = (dir.path / "line.dxf").string();
  AppCommandState st;
  OneLine(st);
  std::vector<std::string> log;
  REQUIRE(ExportDxfFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDxfFile(in, p.c_str(), log));
  REQUIRE(in.userLinesFlat.size() == 6);
  REQUIRE(in.userLinesFlat[3] == Catch::Approx(10.f).margin(0.05f));
}

TEST_CASE("LibreDWG DWG round-trips a model-space LINE", "[dwg][libredwg]") {
  ScratchDir dir("dwg");
  const auto p = (dir.path / "line.dwg").string();
  AppCommandState st;
  OneLine(st);
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  REQUIRE(DwgVersionName(p.c_str()) == "AutoCAD 2000");
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.userLinesFlat.size() == 6);
  REQUIRE(in.userLinesFlat[3] == Catch::Approx(10.f).margin(0.05f));
}

TEST_CASE("DWG import refuses a non-DWG path", "[dwg][libredwg]") {
  ScratchDir dir("nodwg");
  const auto p = dir.path / "fake.dwg";
  {
    std::ofstream f(p, std::ios::binary);
    f << "not a dwg";
  }
  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE_FALSE(ImportDwgFile(st, p.string().c_str(), log));
}

TEST_CASE("GoSurvey DWG preserves a survey point (REQ-175)", "[dwg][libredwg][req175]") {
  ScratchDir dir("svy");
  const auto p = (dir.path / "svy.dwg").string();
  AppCommandState st;
  OneLine(st);
  SurveyPoint pt;
  pt.id = 42;
  pt.easting = 100.f;
  pt.northing = 200.f;
  pt.elevation = 12.5f;
  pt.description = "IPF";
  pt.labelStyle = SurveyPointLabelStyle::None;
  st.surveyPoints.push_back(pt);
  std::vector<std::string> log;
  REQUIRE(ExportDwgFile(st, p.c_str(), log));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.surveyPoints.size() == 1);
  CHECK(in.surveyPoints[0].id == 42);
  CHECK(in.surveyPoints[0].easting == Catch::Approx(100.f).margin(0.05f));
  CHECK(in.surveyPoints[0].northing == Catch::Approx(200.f).margin(0.05f));
  CHECK(in.surveyPoints[0].elevation == Catch::Approx(12.5f).margin(0.05f));
  CHECK(in.surveyPoints[0].description == "IPF");
  REQUIRE(in.userLinesFlat.size() == 6);
}

// issue #140 — the DWG layer table imported with garbled names / wrong colours / no linetypes.
TEST_CASE("LibreDWG decodes UTF-16LE (R2007+) table strings", "[dwg][libredwg][issue140]") {
  // The pre-fix code did std::string((char*)buf), which truncates a TU buffer at the first
  // NUL byte -> "P" instead of "Parcel Line", then the empty-name guard dropped the layer.
  const std::uint16_t wide[] = {'P', 'a', 'r', 'c', 'e', 'l', ' ', 'L', 'i', 'n', 'e', 0};
  CHECK(libredwgcad_detail::DecodeDwgString(wide, /*utf16le=*/true) == "Parcel Line");
  CHECK(libredwgcad_detail::DecodeDwgString("Parcel Line", /*utf16le=*/false) == "Parcel Line");
  CHECK(libredwgcad_detail::DecodeDwgString(nullptr, true).empty());
}

TEST_CASE("LibreDWG layer colour: negative ACI keeps its colour", "[dwg][libredwg][issue140]") {
  // method 0xc2 = entity/table default colour, index = signed ACI. A layer that is OFF stores a
  // negative ACI; the pre-fix code collapsed idx < 0 to "ByLayer", losing the colour.
  CHECK(libredwgcad_detail::ColorToStorage(3, 0xc2, 0) == "#00FF00");       // ACI 3 = green
  CHECK(libredwgcad_detail::ColorToStorage(-3, 0xc2, 0) == "#00FF00");      // off layer, same green
  CHECK(libredwgcad_detail::ColorToStorage(256, 0xc0, 0) == "ByLayer");
  CHECK(libredwgcad_detail::ColorToStorage(0, 0xc1, 0) == "ByBlock");
  CHECK(libredwgcad_detail::ColorToStorage(0, 0xc3, 0x1E90FFu) == "#1E90FF"); // true colour
  CHECK(libredwgcad_detail::ColorToStorage(0, 0xc3, 0) == "ByBlock");         // 0xc3 sentinel
  CHECK(libredwgcad_detail::ColorToStorage(256, 0xc3, 0x100u) == "ByLayer");  // 0xc3 sentinel
}

TEST_CASE("Foreign DWG without payload still imports a LINE (REQ-175)", "[dwg][libredwg][req175]") {
  ScratchDir dir("foreign");
  const auto p = (dir.path / "cad-only.dwg").string();
  AppCommandState st;
  OneLine(st);
  std::vector<std::string> log;
  REQUIRE(ExportLibreCadFile(st, p.c_str(), log, /*asDxf=*/false));
  AppCommandState in;
  REQUIRE(ImportDwgFile(in, p.c_str(), log));
  REQUIRE(in.surveyPoints.empty());
  REQUIRE(in.userLinesFlat.size() == 6);
  REQUIRE(in.userLinesFlat[3] == Catch::Approx(10.f).margin(0.05f));
}
