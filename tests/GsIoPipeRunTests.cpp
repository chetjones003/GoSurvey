// Issue #486 — a CadPipeRun (path, name, nominal size, pressure class) must survive
// SaveGoSurveyTemplateFile / LoadGoSurveyTemplateFile (the SAVEAS/OPEN path).
// Linked here (GoSurveySnapTests) because GoSurveyTests cannot link GsIo.cpp.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"

namespace {

std::filesystem::path UniqueGsPath(const char* stem) {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gosurvey-issue486-gsio";
  std::filesystem::create_directories(dir);
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return dir / (std::string(stem) + "-" + std::to_string(stamp) + ".gs");
}

} // namespace

TEST_CASE("A named CadPipeRun survives SaveGoSurveyTemplateFile then LoadGoSurveyTemplateFile",
          "[gs][piperun][issue486]") {
  AppCommandState st;
  CadPipeRun run;
  run.name = "Cooling Loop A";
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 10.0, 10.0, 0.0};
  run.nominalSize = "4in";
  run.pressureClassTag = "CS150";
  st.cadPipeRuns.push_back(run);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});

  const std::filesystem::path path = UniqueGsPath("piperun");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyTemplateFile(st, path.u8string().c_str(), log));

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyTemplateFile(loaded, path.u8string().c_str(), log));
  REQUIRE(loaded.cadPipeRuns.size() == 1);
  const CadPipeRun& r = loaded.cadPipeRuns[0];
  CHECK(r.name == "Cooling Loop A");
  CHECK(r.nominalSize == "4in");
  CHECK(r.pressureClassTag == "CS150");
  REQUIRE(r.vertsXyz.size() == 9);
  CHECK(r.vertsXyz[3] == Catch::Approx(10.0));
  CHECK(r.vertsXyz[7] == Catch::Approx(10.0));
  REQUIRE(loaded.cadPipeRunAttrs.size() == 1);

  std::filesystem::remove(path);
}

TEST_CASE("A drawing with no pipe runs omits the pipeRuns key and still loads cleanly",
          "[gs][piperun][issue486]") {
  AppCommandState st;  // no pipe runs — ADR-020 (d) additive/omitted-when-empty
  const std::filesystem::path path = UniqueGsPath("no-piperun");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyTemplateFile(st, path.u8string().c_str(), log));

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyTemplateFile(loaded, path.u8string().c_str(), log));
  CHECK(loaded.cadPipeRuns.empty());

  std::filesystem::remove(path);
}
