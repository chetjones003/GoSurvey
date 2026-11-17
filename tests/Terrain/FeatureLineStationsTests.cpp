#include <catch2/catch_test_macros.hpp>

#include <GoSurvey/Survey/Alignment.hpp>
#include <GoSurvey/Terrain/FeatureLineStations.hpp>

#include <cmath>
#include <vector>

namespace {
constexpr double kDeg = alignment::kPi / 180.0;
}

TEST_CASE("Feature line stations include the PC and PT and keep every curve chord within the tolerance", "[req498][alignment]") {
  alignment::Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 100.0));
  REQUIRE(a.AppendCurveByDelta(100.0, 90.0 * kDeg, true));
  REQUIRE(a.AppendTangent(50.0));
  const std::vector<double> s = alignment::FeatureLineStations(a, 0.0, 0.01);
  const double pt = 100.0 + 100.0 * 90.0 * kDeg;
  bool hasPc = false, hasPt = false;
  for (const double v : s) {
    if (std::fabs(v - 100.0) < 1e-9) hasPc = true;
    if (std::fabs(v - pt) < 1e-9) hasPt = true;
  }
  CHECK(hasPc);  // REQ-498 AC1: PC
  CHECK(hasPt);  // REQ-498 AC1: PT
  // Every chord between neighbouring curve stations lies within the tolerance of the arc: R - chord midpoint distance.
  for (size_t i = 1; i < s.size(); ++i) {
    if (s[i - 1] < 100.0 - 1e-9 || s[i] > pt + 1e-9) continue;
    const alignment::Point2 p = a.GetPointAtStation(s[i - 1]);
    const alignment::Point2 q = a.GetPointAtStation(s[i]);
    const alignment::Point2 mid{(p.x + q.x) * 0.5, (p.y + q.y) * 0.5};
    // The line starts heading north (azimuth 0) and turns right, so the arc's centre is 100 to the east, at (100, 0).
    const double dist = std::hypot(mid.x - 100.0, mid.y);
    CHECK(100.0 - dist <= 0.01 + 1e-9);
  }
}
