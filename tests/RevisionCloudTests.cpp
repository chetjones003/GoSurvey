// REQ-441 (GitHub issue #803) - BuildRevisionCloud: the scallop geometry behind REVCLOUD.
//
// Pure geom2d, <cmath> only. The command-line half (picks, options, Object mode, undo) is covered
// by tests/headless/transcripts/req441-revcloud.txt and RevcloudCommandTests.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "util/geom2d.hpp"

using Catch::Approx;

namespace {

// A 10 x 6 rectangle, counter-clockwise from the origin.
const std::vector<double> kRectCcw = {0, 0, 10, 0, 10, 6, 0, 6};

// How far outside the rectangle (0..10, 0..6) a point is; negative inside.
double OutsideRect(double x, double y) { return std::max({-x, x - 10.0, -y, y - 6.0}); }

// The point halfway round segment i of a built cloud.
void ArcMid(const std::vector<double>& xy, const std::vector<float>& b, std::size_t i, double* mx, double* my) {
  const std::size_t j = (i + 1) % b.size();
  const BulgeArcSpan a = BulgeArc(xy[i * 2], xy[i * 2 + 1], xy[j * 2], xy[j * 2 + 1], b[i]);
  REQUIRE(a.valid);
  const double u = a.startAngle + a.sweep * 0.5;
  *mx = a.cx + a.radius * std::cos(u);
  *my = a.cy + a.radius * std::sin(u);
}

}  // namespace

TEST_CASE("a cloud round a rectangle closes on itself, every segment an arc no longer than the arc length",
          "[revcloud][req441]") {
  std::vector<double> xy;
  std::vector<float> b;
  std::string why;
  REQUIRE(BuildRevisionCloud(kRectCcw, 1.0, false, &xy, &b, &why));

  // One bulge per vertex, two coordinates per vertex.
  REQUIRE(xy.size() == b.size() * 2);
  // Every scallop is an arc (a non-zero bulge) and none is longer than the 1.0 asked for.
  for (std::size_t i = 0; i < b.size(); ++i) {
    const std::size_t j = (i + 1) % b.size();
    REQUIRE(b[i] != 0.f);
    CHECK(BulgeSegmentLength(xy[i * 2], xy[i * 2 + 1], xy[j * 2], xy[j * 2 + 1], b[i]) <= 1.0 + 1e-9);
  }
  // 10 long -> 12 scallops, 6 long -> 7 (an arc is ~1.157x its chord, so the chord must be <= 0.864).
  CHECK(b.size() == 2u * 12u + 2u * 7u);
  // The first vertex is the first corner, and the loop returns to it rather than repeating it.
  CHECK(xy[0] == Approx(0.0));
  CHECK(xy[1] == Approx(0.0));
}

TEST_CASE("the arcs bulge away from the area, so the cloud contains it", "[revcloud][req441]") {
  std::vector<double> xy;
  std::vector<float> b;
  REQUIRE(BuildRevisionCloud(kRectCcw, 1.0, false, &xy, &b, nullptr));
  for (std::size_t i = 0; i < b.size(); ++i) {
    double mx = 0, my = 0;
    ArcMid(xy, b, i, &mx, &my);
    CHECK(OutsideRect(mx, my) > 0.05);  // beyond the edge, never inside the rectangle
  }
}

TEST_CASE("which way round the corners were clicked does not change which side the arcs bulge",
          "[revcloud][req441]") {
  const std::vector<double> cw = {0, 6, 10, 6, 10, 0, 0, 0};  // the same rectangle, clockwise
  std::vector<double> xy;
  std::vector<float> b;
  REQUIRE(BuildRevisionCloud(cw, 1.0, false, &xy, &b, nullptr));
  CHECK(b[0] < 0.f);  // clockwise travel: outside is on the LEFT, so a negative bulge
  for (std::size_t i = 0; i < b.size(); ++i) {
    double mx = 0, my = 0;
    ArcMid(xy, b, i, &mx, &my);
    CHECK(OutsideRect(mx, my) > 0.05);
  }
}

TEST_CASE("reverse direction swings every arc into the area", "[revcloud][req441]") {
  std::vector<double> xy, xyRev;
  std::vector<float> b, bRev;
  REQUIRE(BuildRevisionCloud(kRectCcw, 1.0, false, &xy, &b, nullptr));
  REQUIRE(BuildRevisionCloud(kRectCcw, 1.0, true, &xyRev, &bRev, nullptr));
  REQUIRE(b.size() == bRev.size());
  for (std::size_t i = 0; i < b.size(); ++i)
    CHECK(bRev[i] == Approx(-b[i]));
  double mx = 0, my = 0;
  ArcMid(xyRev, bRev, 0, &mx, &my);
  CHECK(OutsideRect(mx, my) < 0.0);
}

TEST_CASE("a corner repeated, or the path closing back onto its start, is not a second corner",
          "[revcloud][req441]") {
  const std::vector<double> messy = {0, 0, 0, 0, 10, 0, 10, 6, 10, 6, 0, 6, 0, 0};
  std::vector<double> a, c;
  std::vector<float> ba, bc;
  REQUIRE(BuildRevisionCloud(messy, 1.0, false, &a, &ba, nullptr));
  REQUIRE(BuildRevisionCloud(kRectCcw, 1.0, false, &c, &bc, nullptr));
  CHECK(ba.size() == bc.size());
}

TEST_CASE("a cloud that cannot be built says why and writes nothing", "[revcloud][req441]") {
  std::vector<double> xy = {99};
  std::vector<float> b = {99.f};
  std::string why;

  SECTION("fewer than three corners") {
    CHECK_FALSE(BuildRevisionCloud({0, 0, 5, 5}, 1.0, false, &xy, &b, &why));
    CHECK(why.find("three") != std::string::npos);
  }
  SECTION("an arc length at or below zero, or not a number") {
    CHECK_FALSE(BuildRevisionCloud(kRectCcw, 0.0, false, &xy, &b, &why));
    CHECK_FALSE(BuildRevisionCloud(kRectCcw, -1.0, false, &xy, &b, &why));
    CHECK_FALSE(BuildRevisionCloud(kRectCcw, std::nan(""), false, &xy, &b, &why));
    CHECK(why.find("arc length") != std::string::npos);
  }
  SECTION("corners in a straight line enclose nothing") {
    CHECK_FALSE(BuildRevisionCloud({0, 0, 5, 0, 10, 0}, 1.0, false, &xy, &b, &why));
    CHECK(why.find("no area") != std::string::npos);
  }
  SECTION("an arc length so small the cloud would be enormous") {
    CHECK_FALSE(BuildRevisionCloud(kRectCcw, 1e-6, false, &xy, &b, &why));
    CHECK(why.find("too small") != std::string::npos);
  }
  // The outputs were not touched by any of the refusals.
  CHECK(xy == std::vector<double>{99});
  CHECK(b == std::vector<float>{99.f});
}
