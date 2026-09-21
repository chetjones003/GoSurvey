// Issue #486 — a CadPipeRun (path, name, nominal size, pressure class) must survive
// SaveGoSurveyTemplateFile / LoadGoSurveyTemplateFile (the SAVEAS/OPEN path).
// Linked here (GoSurveySnapTests) because GoSurveyTests cannot link GsIo.cpp.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
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

// --- Piping networks (issue #486 increment B3 / REQ-345) ---------------------------------------

TEST_CASE("A named piping network with runs survives save/load with indices intact",
          "[gs][pipesys][issue486]") {
  AppCommandState st;
  CadPipeRun a; a.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0}; a.nominalSize = "4in";
  CadPipeRun b; b.vertsXyz = {0.0, 0.0, 0.0, 0.0, 10.0, 0.0}; b.nominalSize = "2in";
  st.cadPipeRuns = {a, b};
  st.cadPipeRunAttrs = {EntityAttributes{}, EntityAttributes{}};
  CadPipingSystem sys;
  sys.name = "Cooling Loop 1";
  sys.pipeRunIndices = {0, 1};
  st.cadPipingSystems.push_back(sys);

  const std::filesystem::path path = UniqueGsPath("pipesys");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyTemplateFile(st, path.u8string().c_str(), log));

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyTemplateFile(loaded, path.u8string().c_str(), log));
  REQUIRE(loaded.cadPipingSystems.size() == 1);
  CHECK(loaded.cadPipingSystems[0].name == "Cooling Loop 1");
  CHECK(loaded.cadPipingSystems[0].pipeRunIndices == std::vector<int>({0, 1}));

  std::filesystem::remove(path);
}

TEST_CASE("A drawing with no piping networks omits the pipingSystems key and loads cleanly",
          "[gs][pipesys][issue486]") {
  AppCommandState st;
  const std::filesystem::path path = UniqueGsPath("no-pipesys");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyTemplateFile(st, path.u8string().c_str(), log));

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyTemplateFile(loaded, path.u8string().c_str(), log));
  CHECK(loaded.cadPipingSystems.empty());

  std::filesystem::remove(path);
}

TEST_CASE("An out-of-range pipeRunIndices entry is dropped rather than trusted",
          "[gs][pipesys][issue486]") {
  // A hand-edited or corrupted file could reference an index past the loaded pipeRuns array;
  // REQ-201 says nothing invalid is ever stored, so the read path must filter it, not crash or
  // carry a dangling index forward.
  AppCommandState st;
  CadPipeRun a; a.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0}; a.nominalSize = "4in";
  st.cadPipeRuns = {a};
  st.cadPipeRunAttrs = {EntityAttributes{}};
  CadPipingSystem sys;
  sys.name = "Loop A";
  sys.pipeRunIndices = {0};
  st.cadPipingSystems.push_back(sys);

  const std::filesystem::path path = UniqueGsPath("pipesys-corrupt");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyTemplateFile(st, path.u8string().c_str(), log));

  // Hand-corrupt the saved JSON to add an out-of-range index.
  {
    std::ifstream in(path);
    nlohmann::json doc;
    in >> doc;
    in.close();
    REQUIRE(doc.contains("document"));
    REQUIRE(doc["document"].contains("pipingSystems"));
    doc["document"]["pipingSystems"][0]["pipeRunIndices"].push_back(99);
    std::ofstream out(path);
    out << doc.dump(2);
  }

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyTemplateFile(loaded, path.u8string().c_str(), log));
  REQUIRE(loaded.cadPipingSystems.size() == 1);
  CHECK(loaded.cadPipingSystems[0].pipeRunIndices == std::vector<int>({0}));  // 99 dropped

  std::filesystem::remove(path);
}
