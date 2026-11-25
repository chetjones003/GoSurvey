// Issue #125 — DIMANGULAR must survive document-payload serialization and loading.
// Linked here (GoSurveySnapTests) because GoSurveyTests cannot link GsIo.cpp.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "CadDimGeom.hpp"
#include "GsAnnotationJson.hpp"
#include "GsIo.hpp"
#include "GsIoTestSupport.hpp"

namespace {

std::filesystem::path UniqueGsPath(const char* stem) {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gosurvey-issue125";
  std::filesystem::create_directories(dir);
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return dir / (std::string(stem) + "-" + std::to_string(stamp) + ".gst");
}

}  // namespace

TEST_CASE("DIMANGULAR survives document-payload serialization and loading (issue #125)", "[payload][dimangular]") {
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
  st.drawing.cadAnnotations.push_back(a);
  EntityAttributes attr;
  attr.id = 1;
  st.drawing.cadAnnotationAttrs.push_back(attr);
  st.nextEntityId = 2;

  const std::filesystem::path path = UniqueGsPath("dimangular");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyDocumentJsonForTest(st, path, log));

  {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(raw.find("\"kind\": \"dimangular\"") != std::string::npos);
    REQUIRE(raw.find("dimAngVertexX") != std::string::npos);
  }

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyDocumentJsonForTest(loaded, path, log));
  REQUIRE(loaded.drawing.cadAnnotations.size() == 1);
  const CadAnnotation& b = loaded.drawing.cadAnnotations[0];
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

TEST_CASE("kind text in a saved document JSON payload reloads as TEXT, not DIMANGULAR (issue #125)", "[payload][dimangular]") {
  AppCommandState st;
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Text;
  a.text = "just a label";
  a.insX = 1.f;
  a.insY = 2.f;
  a.dimAngVertexX = 99.f;
  a.dimAngVertexY = 88.f;
  st.drawing.cadAnnotations.push_back(a);
  EntityAttributes attr;
  attr.id = 1;
  st.drawing.cadAnnotationAttrs.push_back(attr);
  st.nextEntityId = 2;

  const std::filesystem::path path = UniqueGsPath("text-not-angular");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyDocumentJsonForTest(st, path, log));

  AppCommandState loaded;
  REQUIRE(LoadGoSurveyDocumentJsonForTest(loaded, path, log));
  REQUIRE(loaded.drawing.cadAnnotations.size() == 1);
  REQUIRE(loaded.drawing.cadAnnotations[0].kind == CadAnnotation::Kind::Text);
  REQUIRE(loaded.drawing.cadAnnotations[0].text == "just a label");
  REQUIRE(AnnotationKindTag(loaded.drawing.cadAnnotations[0].kind) == std::string("text"));

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

// Issue #1668 — a bolt pattern rotated exactly 62 degrees measured 62d01'07.7" because DIMANGULAR
// kept its picks in float, which near (3230, 5290) are spaced ~0.0005 ft apart. The points must
// survive in double, and the measured angle must be the exact one.
TEST_CASE("DIMANGULAR at state-plane-sized coordinates measures and saves exactly (issue #1668)",
          "[payload][dimangular]") {
  constexpr double kPi = 3.14159265358979323846;
  const double vx = 3230.9697, vy = 5290.5486;
  const double a1 = 0.3;  // an arbitrary, non-axis first ray
  const double a2 = a1 + 62.0 * kPi / 180.0;
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::DimAngular;
  a.dimAngVertexX = vx;
  a.dimAngVertexY = vy;
  a.dimExt1X = vx + std::cos(a1);  // 1 ft rays: the length that showed the error
  a.dimExt1Y = vy + std::sin(a1);
  a.dimExt2X = vx + std::cos(a2);
  a.dimExt2Y = vy + std::sin(a2);
  a.dimSignedOffset = 0.5f;

  float r1 = 0.f, r2 = 0.f, sweep = 0.f, bisx = 0.f, bisy = 0.f, theta = 0.f;
  REQUIRE(CadDimAngularComputeFrame(a, &r1, &r2, &sweep, &bisx, &bisy, &theta));
  const double arcSecondsOff = std::fabs(static_cast<double>(theta) * 180.0 / kPi - 62.0) * 3600.0;
  CHECK(arcSecondsOff < 0.05);  // was ~67" in float

  AppCommandState st;
  st.drawing.cadAnnotations.push_back(a);
  EntityAttributes attr;
  attr.id = 1;
  st.drawing.cadAnnotationAttrs.push_back(attr);
  st.nextEntityId = 2;
  const std::filesystem::path path = UniqueGsPath("dimangular-precision");
  std::vector<std::string> log;
  REQUIRE(SaveGoSurveyDocumentJsonForTest(st, path, log));
  AppCommandState loaded;
  REQUIRE(LoadGoSurveyDocumentJsonForTest(loaded, path, log));
  REQUIRE(loaded.drawing.cadAnnotations.size() == 1);
  const CadAnnotation& b = loaded.drawing.cadAnnotations[0];
  CHECK(b.dimAngVertexX == a.dimAngVertexX);
  CHECK(b.dimAngVertexY == a.dimAngVertexY);
  CHECK(b.dimExt1X == a.dimExt1X);
  CHECK(b.dimExt1Y == a.dimExt1Y);
  CHECK(b.dimExt2X == a.dimExt2X);
  CHECK(b.dimExt2Y == a.dimExt2Y);

  std::error_code ec;
  std::filesystem::remove(path, ec);
}
