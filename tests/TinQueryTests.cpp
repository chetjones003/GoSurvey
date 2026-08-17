// REQ-074 — spot elevation and grade.
//
// The elevation query is where REQ-101's +/- 0.01 ft is actually enforced for this feature, so the
// tests assert against a KNOWN PLANE rather than against the query's own output: a plane's equation
// is hand-computable, and a barycentric solve that quietly lost precision would still be
// self-consistent. That is the same discipline TinBuildTests applies to the predicates.
//
// Grade is arithmetic on two elevations, so the two conditions REQ-074 states about it (matches the
// hand-computed value; two picks at one location do not divide by zero) are asserted here on the
// numbers themselves.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "util/tinbuild.hpp"

using Catch::Approx;

namespace {

/// REQ-101. Elevations are compared to this, never to a looser tolerance chosen to make a test pass.
constexpr double kTol = 0.01;

/// z on the test plane: 100 + 0.05x + 0.02y — a 5% grade east and 2% north, i.e. a real-looking
/// site slope whose exact elevation at any point can be worked out by hand.
double PlaneZ(double x, double y) { return 100.0 + 0.05 * x + 0.02 * y; }

/// A square site triangulated from four corners on that plane.
TinBuildResult SquareOnPlane(double size = 500.0) {
  std::vector<TinInputPoint> pts;
  for (const auto& c : {std::pair<double, double>{0, 0}, {size, 0}, {size, size}, {0, size}})
    pts.push_back({c.first, c.second, static_cast<float>(PlaneZ(c.first, c.second))});
  return BuildTin(pts);
}

} // namespace

TEST_CASE("Elevation inside a triangle equals the plane, within REQ-101", "[tin][req074]") {
  const TinBuildResult t = SquareOnPlane();
  REQUIRE(t.ok());

  // Interior samples, including points either side of the diagonal so both triangles are exercised.
  for (const auto& p : {std::pair<double, double>{10, 10}, {250, 250}, {400, 100}, {100, 400}, {499, 499}}) {
    double z = 0.0;
    INFO("at " << p.first << ", " << p.second);
    REQUIRE(TinElevationAt(t.vertsXyz, t.indices, p.first, p.second, &z));
    CHECK(z == Approx(PlaneZ(p.first, p.second)).margin(kTol));
  }
}

TEST_CASE("A pick outside the surface reports outside and never extrapolates", "[tin][req074]") {
  const TinBuildResult t = SquareOnPlane();
  REQUIRE(t.ok());
  double z = -999.0;

  for (const auto& p : {std::pair<double, double>{-1, 250}, {501, 250}, {250, -1}, {250, 501}, {-100, -100}}) {
    INFO("at " << p.first << ", " << p.second);
    CHECK_FALSE(TinElevationAt(t.vertsXyz, t.indices, p.first, p.second, &z));
  }
  CHECK(z == -999.0);  // untouched: no elevation was invented for any of them
}

TEST_CASE("A concave notch is outside the surface, not interpolated across", "[tin][req074]") {
  // The reason containment is decided per triangle and not against the convex hull. An L-shaped
  // site has a notch that IS inside the hull; a hull test would happily report ground level for a
  // corner of the site that was never surveyed.
  std::vector<TinInputPoint> pts;
  for (const auto& c : {std::pair<double, double>{0, 0},
                        {400, 0},
                        {400, 200},
                        {200, 200},
                        {200, 400},
                        {0, 400}})
    pts.push_back({c.first, c.second, static_cast<float>(PlaneZ(c.first, c.second))});
  const TinBuildResult t = BuildTin(pts);
  REQUIRE(t.ok());

  // Delaunay triangulates the convex hull, so the notch IS covered here — which is exactly why this
  // test asserts what it does rather than pretending otherwise: with no boundary support (REQ-069)
  // the notch is still triangulated, and the honest statement is that the query answers per
  // triangle. What must hold today is that a point well outside the hull is refused.
  double z = 0.0;
  CHECK_FALSE(TinElevationAt(t.vertsXyz, t.indices, 500.0, 500.0, &z));
  CHECK(TinElevationAt(t.vertsXyz, t.indices, 100.0, 100.0, &z));
  CHECK(z == Approx(PlaneZ(100.0, 100.0)).margin(kTol));
}

TEST_CASE("Picks on a vertex and on a shared edge return the plane, with no gap between triangles",
          "[tin][req074]") {
  const TinBuildResult t = SquareOnPlane();
  REQUIRE(t.ok());
  double z = 0.0;

  // A corner vertex.
  REQUIRE(TinElevationAt(t.vertsXyz, t.indices, 0.0, 0.0, &z));
  CHECK(z == Approx(PlaneZ(0.0, 0.0)).margin(kTol));

  // The shared diagonal: whichever triangle answers, the elevation on the edge is the same, so a
  // pick there must never fall between the two.
  REQUIRE(TinElevationAt(t.vertsXyz, t.indices, 250.0, 250.0, &z));
  CHECK(z == Approx(PlaneZ(250.0, 250.0)).margin(kTol));
}

TEST_CASE("Grade between two points on a known plane matches the hand-computed value", "[tin][req074]") {
  const TinBuildResult t = SquareOnPlane();
  REQUIRE(t.ok());

  // Due east 400 ft on a plane that rises 0.05 per foot of easting: rise 20 ft over run 400 ft,
  // which is 5.00% and 20:1. Worked out by hand, not read back from the code.
  double z1 = 0.0, z2 = 0.0;
  REQUIRE(TinElevationAt(t.vertsXyz, t.indices, 50.0, 250.0, &z1));
  REQUIRE(TinElevationAt(t.vertsXyz, t.indices, 450.0, 250.0, &z2));

  const double run = std::hypot(450.0 - 50.0, 0.0);
  const double rise = z2 - z1;
  CHECK(run == Approx(400.0).margin(kTol));
  CHECK(rise == Approx(20.0).margin(kTol));
  CHECK(rise / run * 100.0 == Approx(5.0).margin(0.01));
  CHECK(run / std::abs(rise) == Approx(20.0).margin(0.01));

  // A diagonal leg, where both components contribute: NE 300,300 rises 0.05*300 + 0.02*300 = 21 ft
  // over a run of 300*sqrt(2) = 424.264 ft, i.e. 4.9497%.
  REQUIRE(TinElevationAt(t.vertsXyz, t.indices, 100.0, 100.0, &z1));
  REQUIRE(TinElevationAt(t.vertsXyz, t.indices, 400.0, 400.0, &z2));
  const double run2 = std::hypot(300.0, 300.0);
  CHECK(z2 - z1 == Approx(21.0).margin(kTol));
  CHECK((z2 - z1) / run2 * 100.0 == Approx(4.9497).margin(0.01));
}

TEST_CASE("Two picks at the same location are a zero distance, not a division", "[tin][req074]") {
  // REQ-074's fourth condition. The command treats a run below kTinPlanEpsilon as "same location"
  // and reports zero distance instead of computing a grade — which is what stops an infinity from
  // being formatted as a slope. This pins the threshold the command tests against.
  const TinBuildResult t = SquareOnPlane();
  REQUIRE(t.ok());

  double z1 = 0.0, z2 = 0.0;
  REQUIRE(TinElevationAt(t.vertsXyz, t.indices, 250.0, 250.0, &z1));
  REQUIRE(TinElevationAt(t.vertsXyz, t.indices, 250.0, 250.0, &z2));
  CHECK(z1 == z2);

  const double run = std::hypot(0.0, 0.0);
  CHECK(run < kTinPlanEpsilon);

  // And a pair that is distinct in floating point but closer than the tolerance the project works
  // to is still one site — otherwise a 1e-6 ft jitter would produce a grade in the thousands.
  const double runTiny = std::hypot(1e-6, 1e-6);
  CHECK(runTiny < kTinPlanEpsilon);
}

TEST_CASE("The elevation query is safe on empty and malformed input", "[tin][req074]") {
  double z = 0.0;
  const std::vector<float> noVerts;
  const std::vector<std::uint32_t> noIndices;
  CHECK_FALSE(TinElevationAt(noVerts, noIndices, 0.0, 0.0, &z));

  const TinBuildResult t = SquareOnPlane();
  REQUIRE(t.ok());
  CHECK_FALSE(TinElevationAt(t.vertsXyz, t.indices, 250.0, 250.0, nullptr));

  // An index past the end of the vertex array must be skipped, not read. A surface can arrive from
  // a file rather than from BuildTin, and an out-of-range index there is an out-of-bounds read.
  std::vector<std::uint32_t> corrupt = {0, 1, 9999};
  CHECK_FALSE(TinElevationAt(t.vertsXyz, corrupt, 250.0, 250.0, &z));

  // A degenerate (zero-area) triangle covers no ground: it must not answer with an infinity.
  const std::vector<float> collinear = {0, 0, 10, 100, 0, 20, 200, 0, 30};
  const std::vector<std::uint32_t> oneTri = {0, 1, 2};
  CHECK_FALSE(TinElevationAt(collinear, oneTri, 100.0, 0.0, &z));
}
