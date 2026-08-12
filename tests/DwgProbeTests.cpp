// Coverage for DWG version detection and converter discovery (REQ-052 / ADR-024 / TASK-031).
//
// These are the parts of DWG support that decide whether GoSurvey even attempts a conversion, and
// they are the parts that must fail *informatively* rather than silently (REQ-201). The conversion
// orchestration itself lives in DwgIo.cpp and still cannot be linked here — it calls into DxfIo.

#include "DwgIo.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

/// Unique scratch directory, removed on scope exit.
struct ScratchDir {
  std::filesystem::path path;
  explicit ScratchDir(const char* tag) {
    static int counter = 0;
    path = std::filesystem::temp_directory_path() /
           ("gosurvey-test-" + std::string(tag) + "-" + std::to_string(++counter));
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

std::filesystem::path WriteFile(const std::filesystem::path& p, const std::string& bytes) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return p;
}

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
  _putenv_s(name, value ? value : "");
#else
  if (value)
    setenv(name, value, 1);
  else
    unsetenv(name);
#endif
}

/// Restores GOSURVEY_DWG_CONVERTER (and the discovery cache) however the test exits.
struct ConverterEnvGuard {
  ~ConverterEnvGuard() {
    SetEnv("GOSURVEY_DWG_CONVERTER", "");
    FindDwgConverter(/*forceRescan=*/true);
  }
};

} // namespace

TEST_CASE("DwgVersionNameFromTag maps every known release tag", "[dwg][probe]") {
  REQUIRE(DwgVersionNameFromTag("AC1032") == "AutoCAD 2018");
  REQUIRE(DwgVersionNameFromTag("AC1027") == "AutoCAD 2013");
  REQUIRE(DwgVersionNameFromTag("AC1024") == "AutoCAD 2010");
  REQUIRE(DwgVersionNameFromTag("AC1021") == "AutoCAD 2007");
  REQUIRE(DwgVersionNameFromTag("AC1018") == "AutoCAD 2004");
  REQUIRE(DwgVersionNameFromTag("AC1015") == "AutoCAD 2000");
  REQUIRE(DwgVersionNameFromTag("AC1014") == "AutoCAD R14");
  REQUIRE(DwgVersionNameFromTag("AC1012") == "AutoCAD R13");
  REQUIRE(DwgVersionNameFromTag("AC1009") == "AutoCAD R11/R12");
  REQUIRE(DwgVersionNameFromTag("AC1006") == "AutoCAD R10");
}

TEST_CASE("DwgVersionNameFromTag separates 'not a DWG' from 'a DWG we don't know'",
          "[dwg][probe]") {
  // The distinction matters: one is a wrong-file-type error the user must see, the other is a
  // newer release we should still identify as a drawing.
  SECTION("an unknown AC-prefixed tag is reported as an unknown DWG") {
    REQUIRE(DwgVersionNameFromTag("AC1099") == "unknown DWG (AC1099)");
  }
  SECTION("a non-DWG tag yields an empty string") {
    REQUIRE(DwgVersionNameFromTag("hello!").empty());
    REQUIRE(DwgVersionNameFromTag("%PDF-1").empty());
  }
  SECTION("a tag of the wrong length is not a DWG") {
    REQUIRE(DwgVersionNameFromTag("").empty());
    REQUIRE(DwgVersionNameFromTag("AC10").empty());
    REQUIRE(DwgVersionNameFromTag("AC10320").empty());
  }
}

TEST_CASE("DwgVersionName reads the tag off a real file", "[dwg][probe]") {
  ScratchDir dir("dwgver");

  SECTION("a file carrying a DWG tag is identified") {
    const auto p = WriteFile(dir.path / "ok.dwg", std::string("AC1032") + std::string(64, '\0'));
    REQUIRE(DwgVersionName(p.string().c_str()) == "AutoCAD 2018");
  }

  SECTION("a non-DWG file is refused rather than mis-parsed") {
    const auto p = WriteFile(dir.path / "notadwg.dwg", "this is plainly not a drawing at all");
    REQUIRE(DwgVersionName(p.string().c_str()).empty());
  }

  SECTION("a file shorter than the tag is refused") {
    const auto p = WriteFile(dir.path / "tiny.dwg", "AC1");
    REQUIRE(DwgVersionName(p.string().c_str()).empty());
  }

  SECTION("an empty file is refused") {
    const auto p = WriteFile(dir.path / "empty.dwg", "");
    REQUIRE(DwgVersionName(p.string().c_str()).empty());
  }

  SECTION("a missing file, an empty path and null are all refused without crashing") {
    REQUIRE(DwgVersionName((dir.path / "nope.dwg").string().c_str()).empty());
    REQUIRE(DwgVersionName("").empty());
    REQUIRE(DwgVersionName(nullptr).empty());
  }
}

TEST_CASE("FindDwgConverter honours the GOSURVEY_DWG_CONVERTER override", "[dwg][probe]") {
  ConverterEnvGuard guard;
  ScratchDir dir("dwgconv");

  SECTION("an accoreconsole override is classified as the AutoCAD console") {
    const auto exe = WriteFile(dir.path / "accoreconsole.exe", "stub");
    SetEnv("GOSURVEY_DWG_CONVERTER", exe.string().c_str());
    const DwgConverter& c = FindDwgConverter(/*forceRescan=*/true);
    REQUIRE(c.available());
    REQUIRE(c.kind == DwgConverterKind::AutoCadCore);
    REQUIRE(c.exePath == exe.string());
  }

  SECTION("classification is case-insensitive") {
    // Windows filenames are case-insensitive, so "AccoreConsole.exe" is the same program.
    const auto exe = WriteFile(dir.path / "AccoreConsole.exe", "stub");
    SetEnv("GOSURVEY_DWG_CONVERTER", exe.string().c_str());
    const DwgConverter& c = FindDwgConverter(/*forceRescan=*/true);
    REQUIRE(c.kind == DwgConverterKind::AutoCadCore);
  }

  SECTION("any other override is treated as ODA File Converter") {
    const auto exe = WriteFile(dir.path / "ODAFileConverter.exe", "stub");
    SetEnv("GOSURVEY_DWG_CONVERTER", exe.string().c_str());
    const DwgConverter& c = FindDwgConverter(/*forceRescan=*/true);
    REQUIRE(c.available());
    REQUIRE(c.kind == DwgConverterKind::OdaFileConverter);
  }

  SECTION("an override pointing at a nonexistent file is ignored, not trusted") {
    SetEnv("GOSURVEY_DWG_CONVERTER", (dir.path / "does-not-exist.exe").string().c_str());
    const DwgConverter& c = FindDwgConverter(/*forceRescan=*/true);
    // Falls through to real discovery, which may or may not find something on this machine —
    // the point is that it never reports the bogus path as usable.
    REQUIRE(c.exePath != (dir.path / "does-not-exist.exe").string());
  }

  SECTION("a located converter always carries a display name for the UI") {
    const auto exe = WriteFile(dir.path / "accoreconsole.exe", "stub");
    SetEnv("GOSURVEY_DWG_CONVERTER", exe.string().c_str());
    const DwgConverter& c = FindDwgConverter(/*forceRescan=*/true);
    REQUIRE_FALSE(c.displayName.empty());
  }
}

TEST_CASE("FindDwgConverter caches until a rescan is requested", "[dwg][probe]") {
  ConverterEnvGuard guard;
  ScratchDir dir("dwgcache");
  const auto exe = WriteFile(dir.path / "accoreconsole.exe", "stub");

  SetEnv("GOSURVEY_DWG_CONVERTER", exe.string().c_str());
  const DwgConverter& first = FindDwgConverter(/*forceRescan=*/true);
  REQUIRE(first.kind == DwgConverterKind::AutoCadCore);

  // Changing the environment must NOT take effect until a rescan — the cache is what keeps the
  // File menu from hitting the filesystem every frame while it draws.
  SetEnv("GOSURVEY_DWG_CONVERTER", "");
  REQUIRE(FindDwgConverter().kind == DwgConverterKind::AutoCadCore);
}

TEST_CASE("a default-constructed DwgConverter reports itself unavailable", "[dwg][probe]") {
  const DwgConverter c;
  REQUIRE(c.kind == DwgConverterKind::None);
  REQUIRE_FALSE(c.available());
}
