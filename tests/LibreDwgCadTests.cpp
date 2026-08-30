#include "DxfIo.hpp"
#include "DwgIo.hpp"

#include "CadCommands.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
