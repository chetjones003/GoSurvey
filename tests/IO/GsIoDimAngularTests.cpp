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
#include "CadField.hpp"
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

  float r1 = 0.f, r2 = 0.f, sweep = 0.f, bisx = 0.f, bisy = 0.f;
  double theta = 0.0;
  REQUIRE(CadDimAngularComputeFrame(a, &r1, &r2, &sweep, &bisx, &bisy, &theta));
  const double arcSecondsOff = std::fabs(theta * 180.0 / kPi - 62.0) * 3600.0;
  CHECK(arcSecondsOff < 1e-5);  // was ~67" in float, ~0.01" when theta narrowed to float

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

// PR #1675 review finding: a 1,000,000.025 span narrows to 1,000,000.0 in float, losing precision
// visible at hundredth-foot display precision. Linear (horizontal & vertical) and aligned geometry,
// measurement refresh, and resolved display text must all preserve full double precision.
TEST_CASE("DIMLINEAR and DIMALIGNED keep double measurement precision at large spans (issue #1668)",
          "[payload][dimangular]") {
  AppCommandState st;
  st.drawing.activeDimensionStyle.name = "Standard";
  st.drawing.activeDimensionStyle.unitPrecision = 3;
  st.drawing.dimensionStyles = {st.drawing.activeDimensionStyle};
  const AngleDisplaySettings angle{};

  float sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0, tx = 0, ty = 0, nx = 0, ny = 0;

  // Horizontal DIMLINEAR: 1,000,000.025 span at large offset.
  CadAnnotation linH;
  linH.kind = CadAnnotation::Kind::DimLinear;
  linH.dimensionStyleName = "Standard";
  linH.dimLinearVertical = false;
  linH.dimExt1X = 500000.0;
  linH.dimExt1Y = 100000.0;
  linH.dimExt2X = 1500000.025;
  linH.dimExt2Y = 100000.0;
  linH.dimSignedOffset = 10.f;
  double measH = 0.0;
  REQUIRE(CadDimLinearGeometry(linH, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &measH));
  CHECK(measH == Catch::Approx(1000000.025).margin(1e-9));
  CadDimRefreshMeasurementText(&linH, st.drawing.activeDimensionStyle, angle);
  CHECK(linH.text == "1000000.025");
  CHECK(CadAnnotationResolvedText(st, linH) == "1000000.025");

  // Vertical DIMLINEAR: 1,000,000.025 span.
  CadAnnotation linV;
  linV.kind = CadAnnotation::Kind::DimLinear;
  linV.dimensionStyleName = "Standard";
  linV.dimLinearVertical = true;
  linV.dimExt1X = 200000.0;
  linV.dimExt1Y = 500000.0;
  linV.dimExt2X = 200000.0;
  linV.dimExt2Y = 1500000.025;
  linV.dimSignedOffset = 10.f;
  double measV = 0.0;
  REQUIRE(CadDimLinearGeometry(linV, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &measV));
  CHECK(measV == Catch::Approx(1000000.025).margin(1e-9));
  CadDimRefreshMeasurementText(&linV, st.drawing.activeDimensionStyle, angle);
  CHECK(linV.text == "1000000.025");
  CHECK(CadAnnotationResolvedText(st, linV) == "1000000.025");

  // Diagonal DIMALIGNED (3-4-5 triangle scaled to 1,000,000.025 hypotenuse).
  CadAnnotation aligned;
  aligned.kind = CadAnnotation::Kind::DimAligned;
  aligned.dimensionStyleName = "Standard";
  aligned.dimExt1X = 100000.0;
  aligned.dimExt1Y = 200000.0;
  aligned.dimExt2X = 100000.0 + 600000.015;
  aligned.dimExt2Y = 200000.0 + 800000.020;
  aligned.dimSignedOffset = 10.f;
  double measA = 0.0;
  REQUIRE(CadDimAlignedGeometry(aligned, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &measA));
  CHECK(measA == Catch::Approx(1000000.025).margin(1e-9));
  CadDimRefreshMeasurementText(&aligned, st.drawing.activeDimensionStyle, angle);
  CHECK(aligned.text == "1000000.025");
  CHECK(CadAnnotationResolvedText(st, aligned) == "1000000.025");

  // Hundredth-foot display precision (2 decimals) preserves the fractional hundredths (float yielded .00).
  st.drawing.activeDimensionStyle.unitPrecision = 2;
  st.drawing.dimensionStyles = {st.drawing.activeDimensionStyle};
  CadDimRefreshMeasurementText(&linH, st.drawing.activeDimensionStyle, angle);
  CHECK(linH.text == "1000000.02");  // 1000000.025 exact rounds half-to-even in %.2f
  CHECK(CadAnnotationResolvedText(st, aligned) == "1000000.03");
}
