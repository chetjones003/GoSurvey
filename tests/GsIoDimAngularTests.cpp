// Issue #125 — DIMANGULAR must survive SaveGoSurveyFile / LoadGoSurveyFile (the SAVEAS/OPEN path).
// Linked here (GoSurveySnapTests) because GoSurveyTests cannot link GsIo.cpp.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsAnnotationJson.hpp"
#include "GsIo.hpp"

namespace {

std::filesystem::path UniqueGsPath(const char* stem) {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gosurvey-issue125";
  std::filesystem::create_directories(dir);
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return dir / (std::string(stem) + "-" + std::to_string(stamp) + ".gs");
}

}  // namespace

TEST_CASE("DIMANGULAR survives SaveGoSurveyFile then LoadGoSurveyFile (issue #125)", "[gs][dimangular]") {
  AppCommandState st;
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::DimAngular;
  a.insX = 14.142f;
  a.insY = 14.142f;
  a.plottedHeightInches = 0.1f;
  a.fontFamily = "Arial";
  a.text = "90.0000\xc2\xb0";
  a.dimExt1X = 50.f;
  a.dimExt1Y = 0.f;
  a.dimExt2X = 0.f;
  a.dimExt2Y = 50.f;
  a.dimAngVertexX = 10.f;
  a.dimAngVertexY = 20.f;
  a.dimSignedOffset = 15.f;
  st.cadAnnotations.push_back(a);
  EntityAttributes attr;
  attr.id = 1;
  st.cadAnnotationAttrs.push_back(attr);
  st.nextEntityId = 2;

  const std::filesystem::path path = UniqueGsPath("dimangular");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyFile(st, path.string().c_str(), log));

  {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(raw.find("\"kind\": \"dimangular\"") != std::string::npos);
    REQUIRE(raw.find("dimAngVertexX") != std::string::npos);
  }

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyFile(loaded, path.string().c_str(), log));
  REQUIRE(loaded.cadAnnotations.size() == 1);
  const CadAnnotation& b = loaded.cadAnnotations[0];
  REQUIRE(b.kind == CadAnnotation::Kind::DimAngular);
  REQUIRE(AnnotationKindTag(b.kind) == std::string("dimangular"));
  REQUIRE(b.dimAngVertexX == Catch::Approx(10.f));
  REQUIRE(b.dimAngVertexY == Catch::Approx(20.f));
  REQUIRE(b.dimExt1X == Catch::Approx(50.f));
  REQUIRE(b.dimExt2Y == Catch::Approx(50.f));
  REQUIRE(b.text == a.text);
  REQUIRE(b.fontFamily == "Arial");
  REQUIRE(b.dimSignedOffset == Catch::Approx(15.f));

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST_CASE("kind text in a saved .gs reloads as TEXT, not DIMANGULAR (issue #125)", "[gs][dimangular]") {
  AppCommandState st;
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Text;
  a.text = "just a label";
  a.insX = 1.f;
  a.insY = 2.f;
  a.dimAngVertexX = 99.f;
  a.dimAngVertexY = 88.f;
  st.cadAnnotations.push_back(a);
  EntityAttributes attr;
  attr.id = 1;
  st.cadAnnotationAttrs.push_back(attr);
  st.nextEntityId = 2;

  const std::filesystem::path path = UniqueGsPath("text-not-angular");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyFile(st, path.string().c_str(), log));

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyFile(loaded, path.string().c_str(), log));
  REQUIRE(loaded.cadAnnotations.size() == 1);
  REQUIRE(loaded.cadAnnotations[0].kind == CadAnnotation::Kind::Text);
  REQUIRE(loaded.cadAnnotations[0].text == "just a label");
  REQUIRE(AnnotationKindTag(loaded.cadAnnotations[0].kind) == std::string("text"));

  std::error_code ec;
  std::filesystem::remove(path, ec);
}
