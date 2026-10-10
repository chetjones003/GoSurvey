// REQ-170 increment 1: LibreDWG is compiled into the test binary and can write/read R2004.

#include "DwgIo.hpp"
#include "LibreDwg.hpp"

#if defined(__cplusplus) && !defined(restrict)
#define restrict
#endif
extern "C" {
#include <dwg.h>
}

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

TEST_CASE("LibreDwgPackageVersion is the pinned 0.14 release", "[dwg][libredwg]") {
  const std::string v = LibreDwgPackageVersion();
  REQUIRE_FALSE(v.empty());
  REQUIRE(v.find("0.14") != std::string::npos);
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

// Issue #600: LibreDWG 0.14+ must encode a from-scratch R2004 document (one LINE) and read it back.
TEST_CASE("LibreDwg writes R2004 that the in-process reader sees (issue #600)",
          "[dwg][libredwg][issue600]") {
  ScratchDir dir("r2004");
  const auto dwgPath = dir.path / "line-r2004.dwg";
  const std::string pathUtf8 = dwgPath.string();

  REQUIRE(LibreDwgWriteMinimalR2004(pathUtf8.c_str()));
  REQUIRE(std::filesystem::file_size(dwgPath) > 6);

  std::ifstream in(dwgPath, std::ios::binary);
  char magic[7] = {};
  in.read(magic, 6);
  REQUIRE(in.gcount() == 6);
  REQUIRE(std::string(magic, 6) == "AC1018");
  REQUIRE(LibreDwgReadVersionName(pathUtf8.c_str()) == "r2004");
}

TEST_CASE("LibreDwg writes R2010/R2013/R2018 minimal LINE files (R2018 export path)",
          "[dwg][libredwg][issue600]") {
  const struct Row {
    int libVer;
    const char* acTag;
    const char* readName;
  } rows[] = {
      {R_2010, "AC1024", "r2010"},
      {R_2013, "AC1027", "r2013"},
      {R_2018, "AC1032", "r2018"},
  };
  for (const Row& row : rows) {
    ScratchDir dir(row.acTag);
    const std::string pathUtf8 = (dir.path / "line.dwg").string();
    REQUIRE(LibreDwgWriteMinimalAtVersion(row.libVer, pathUtf8.c_str()));
    std::ifstream in(pathUtf8, std::ios::binary);
    char magic[7] = {};
    in.read(magic, 6);
    REQUIRE(std::string(magic, 6) == row.acTag);
    REQUIRE(LibreDwgReadVersionName(pathUtf8.c_str()) == row.readName);
  }
}
