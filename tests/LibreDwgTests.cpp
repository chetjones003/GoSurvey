// REQ-170 increment 1: LibreDWG is compiled into the test binary and can write/read R2004.

#include "DwgIo.hpp"
#include "LibreDwg.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

struct ScratchDir {
  std::filesystem::path path;
  explicit ScratchDir(const char* tag) {
    static int counter = 0;
    path = std::filesystem::temp_directory_path() /
           ("gosurvey-libredwg-" + std::string(tag) + "-" + std::to_string(++counter));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
};

}  // namespace

TEST_CASE("LibreDwgPackageVersion is the pinned 0.13.3 release", "[dwg][libredwg]") {
  const std::string v = LibreDwgPackageVersion();
  REQUIRE_FALSE(v.empty());
  REQUIRE(v.find("0.13.3") != std::string::npos);
}

TEST_CASE("LibreDwgWriteMinimalR2000 refuses an empty path", "[dwg][libredwg]") {
  REQUIRE_FALSE(LibreDwgWriteMinimalR2000(nullptr));
  REQUIRE_FALSE(LibreDwgWriteMinimalR2000(""));
}

TEST_CASE("LibreDwgReadVersionName refuses a missing file", "[dwg][libredwg]") {
  REQUIRE(LibreDwgReadVersionName("Z:/gosurvey-no-such-file.dwg").empty());
}

TEST_CASE("LibreDwg writes R2000 that the in-process reader and DwgProbe both see",
          "[dwg][libredwg]") {
  ScratchDir dir("r2000");
  const auto dwgPath = dir.path / "line.dwg";
  const std::string pathUtf8 = dwgPath.string();

  REQUIRE(LibreDwgWriteMinimalR2000(pathUtf8.c_str()));
  REQUIRE(std::filesystem::file_size(dwgPath) > 6);

  std::ifstream in(dwgPath, std::ios::binary);
  char magic[7] = {};
  in.read(magic, 6);
  REQUIRE(in.gcount() == 6);
  const std::string tag(magic, 6);
  REQUIRE(tag == "AC1015");
  REQUIRE(DwgVersionName(pathUtf8.c_str()) == "AutoCAD 2000");

  REQUIRE(LibreDwgReadVersionName(pathUtf8.c_str()) == "r2000");
}
