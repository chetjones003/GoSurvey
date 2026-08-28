#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "contourgen.hpp"
#include "gridsurface.hpp"
#include "surfacequery.hpp"
#include "surfacestats.hpp"
#include "tinbuild.hpp"

#include <limits>
#include <vector>

using Catch::Approx;

TEST_CASE("2x2 grid bilinear at cell centre", "[req137][grid]") {
  const std::vector<float> z{0.f, 0.f, 10.f, 10.f};
  double mid = 0.0;
  REQUIRE(GridElevationAt(0.0, 0.0, 1.0, 1.0, 2, 2, z, 0.5, 0.5, &mid));
  REQUIRE_THAT(mid, Catch::Matchers::WithinAbs(5.0, 0.01));
  GridSurfaceQuery q(0.0, 0.0, 1.0, 1.0, 2, 2, z);
  double zq = 0.0;
  REQUIRE(q.elevationAt(0.5, 0.5, &zq));
  REQUIRE_THAT(zq, Catch::Matchers::WithinAbs(5.0, 0.01));
}

TEST_CASE("TIN ISurfaceQuery matches TinElevationAt", "[req137][query]") {
  std::vector<TinInputPoint> pts{{0, 0, 0.f}, {10, 0, 0.f}, {0, 10, 0.f}, {10, 10, 0.f}};
  const TinBuildResult t = BuildTin(pts);
  REQUIRE(t.ok());
  TinSurfaceQuery q(t.vertsXyz, t.indices);
  for (const auto& p : {std::pair<double, double>{2, 2}, {5, 1}, {8, 8}}) {
    double a = 0.0, b = 0.0;
    REQUIRE(TinElevationAt(t.vertsXyz, t.indices, p.first, p.second, &a));
    REQUIRE(q.elevationAt(p.first, p.second, &b));
    REQUIRE_THAT(a, Catch::Matchers::WithinAbs(b, 0.01));
  }
}

TEST_CASE("Due-east downhill reports aspect 90 and a slope angle", "[req138][aspect]") {
  // Z = -X: fall is due east.
  const std::vector<float> v{0.f, 0.f, 0.f, 10.f, 0.f, -10.f, 0.f, 10.f, 0.f};
  const std::vector<std::uint32_t> idx{0, 1, 2};
  TinSurfaceQuery q(v, idx);
  double asp = 0.0, ang = 0.0, z = 0.0;
  REQUIRE(q.elevationAt(2.0, 2.0, &z));
  REQUIRE(q.aspectDegAt(2.0, 2.0, &asp));
  REQUIRE(q.slopeAngleDegAt(2.0, 2.0, &ang));
  REQUIRE_THAT(asp, Catch::Matchers::WithinAbs(90.0, 1.0));
  REQUIRE(ang > 0.5);
}

TEST_CASE("Query miss does not invent a slope", "[req138][outside]") {
  const std::vector<float> v{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  const std::vector<std::uint32_t> idx{0, 1, 2};
  TinSurfaceQuery q(v, idx);
  double pct = 0.0;
  REQUIRE_FALSE(q.slopePercentAt(50.0, 50.0, &pct));
}

TEST_CASE("Chaikin increases open contour vertex count", "[req138][smooth]") {
  ContourResult r;
  r.vertsXyz = {0.f, 0.f, 1.f, 10.f, 0.f, 1.f, 20.f, 0.f, 1.f};
  r.offsets = {0, 3};
  r.levels = {1.0};
  r.closed = {0};
  const int before = static_cast<int>(r.vertsXyz.size() / 3);
  SmoothContoursChaikin(&r, 1);
  REQUIRE(static_cast<int>(r.vertsXyz.size() / 3) > before);
}

TEST_CASE("Zero label spacing emits no labels", "[req138][labels]") {
  ContourResult r;
  r.vertsXyz = {0.f, 0.f, 1.f, 10.f, 0.f, 1.f};
  r.offsets = {0, 2};
  r.levels = {1.0};
  r.closed = {0};
  std::vector<ContourLabelPoint> labs;
  CollectContourLabels(r, 0.0, &labs);
  REQUIRE(labs.empty());
  CollectContourLabels(r, 5.0, &labs);
  REQUIRE_FALSE(labs.empty());
}

TEST_CASE("Interior edge swap changes two triangles", "[req139][swap]") {
  std::vector<float> verts{0, 0, 0, 10, 0, 0, 10, 10, 0, 0, 10, 0};
  std::vector<std::uint32_t> idx{0, 1, 2, 0, 2, 3};
  const auto a = idx;
  REQUIRE(TinSwapInteriorEdgeNear(verts, idx, 5.0, 5.0));
  REQUIRE(idx != a);
  REQUIRE(idx.size() == 6);
  std::vector<std::uint32_t> miss = a;
  REQUIRE_FALSE(TinSwapInteriorEdgeNear(verts, miss, 100.0, 100.0));
  REQUIRE(miss == a);
}

TEST_CASE("Deleting an interior edge removes both triangles", "[req150][tin]") {
  std::vector<float> verts{0, 0, 0, 10, 0, 0, 10, 10, 0, 0, 10, 0};
  std::vector<std::uint32_t> idx{0, 1, 2, 0, 2, 3};
  REQUIRE(TinDeleteInteriorEdgeNear(idx, verts, 5.0, 5.0));
  REQUIRE(idx.size() < 6);
  std::vector<std::uint32_t> miss{0, 1, 2, 0, 2, 3};
  REQUIRE_FALSE(TinDeleteInteriorEdgeNear(miss, verts, 100.0, 100.0));
  REQUIRE(miss.size() == 6);
}

TEST_CASE("One-triangle stats min area equals max area", "[req140][stats]") {
  const std::vector<float> verts{0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 10.f, 0.f};
  const std::vector<std::uint32_t> idx{0, 1, 2};
  const SurfaceStats s = ComputeSurfaceStats(verts, idx);
  REQUIRE(s.built);
  REQUIRE(s.triangles == 1);
  CHECK(s.minTriArea2d == Approx(s.maxTriArea2d));
  CHECK(s.minTriArea2d == Approx(50.0));
}

TEST_CASE("Grid volume with no overlapping finite nodes is refused", "[req137][gridvol]") {
  const std::vector<float> a{0.f, 0.f, 0.f, 0.f};
  const float n = std::numeric_limits<float>::quiet_NaN();
  const std::vector<float> b{n, n, n, n};
  std::vector<float> out;
  std::string why;
  REQUIRE_FALSE(GridVolumeSubtract(0, 0, 1, 1, 2, 2, a, b, &out, &why));
  REQUIRE(out.empty());
}
