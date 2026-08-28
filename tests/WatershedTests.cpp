// REQ-132 / REQ-133 / REQ-134 — watershed, water-drop, catchment.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "util/watershed.hpp"

using Catch::Approx;

namespace {

// Rectangle 0..10 × 0..10, z = 0.5 y (south is downhill). Two triangles, one basin to y = 0.
void PlaneSouth(std::vector<float>* v, std::vector<std::uint32_t>* i) {
  *v = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 10.f, 5.f, 0.f, 10.f, 5.f};
  *i = {0, 1, 2, 0, 2, 3};
}

// West z = x (drains to x = 0); east z = 20 − x (drains to x = 20). Ridge at x = 10.
void Ridge(std::vector<float>* v, std::vector<std::uint32_t>* i) {
  *v = {0.f,  0.f, 0.f,  10.f, 0.f,  10.f, 10.f, 10.f, 10.f, 0.f,  10.f, 0.f,
        20.f, 0.f, 0.f,  20.f, 10.f, 0.f};
  *i = {0, 1, 3, 1, 2, 3, 1, 4, 5, 1, 5, 2};
}

// Square rim at z = 10, centre at z = 0.
void Pit(std::vector<float>* v, std::vector<std::uint32_t>* i) {
  *v = {0.f, 0.f, 10.f, 10.f, 0.f, 10.f, 10.f, 10.f, 10.f, 0.f, 10.f, 10.f, 5.f, 5.f, 0.f};
  *i = {0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4};
}

} // namespace

TEST_CASE("Null TIN is refused", "[surface][req132][watershed]") {
  const WatershedResult a = ComputeWatershed({}, {});
  CHECK_FALSE(a.ok);
  CHECK(a.error == "null TIN");

  const std::vector<float> verts{0.f, 0.f, 0.f};
  const WatershedResult b = ComputeWatershed(verts, {});
  CHECK_FALSE(b.ok);
  CHECK(b.error == "null TIN");
}

TEST_CASE("Single-basin plane drains to the south boundary", "[surface][req132][watershed]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  PlaneSouth(&v, &i);
  const WatershedResult w = ComputeWatershed(v, i);
  REQUIRE(w.ok);
  REQUIRE(w.basins.size() == 1);
  CHECK(w.basins[0].drain == DrainKind::Boundary);
  CHECK(w.basins[0].area2d == Approx(100.0).margin(0.01));
  CHECK(w.basins[0].drainY == Approx(0.0).margin(0.05));
  CHECK(w.triangleBasinId.size() == 2);
  CHECK(w.triangleBasinId[0] == w.triangleBasinId[1]);
}

TEST_CASE("Ridge yields two basins that do not cross", "[surface][req132][watershed]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  Ridge(&v, &i);
  const WatershedResult w = ComputeWatershed(v, i);
  REQUIRE(w.ok);
  REQUIRE(w.basins.size() == 2);
  CHECK(w.triangleBasinId.size() == 4);
  CHECK(w.triangleBasinId[0] == w.triangleBasinId[1]);
  CHECK(w.triangleBasinId[2] == w.triangleBasinId[3]);
  CHECK(w.triangleBasinId[0] != w.triangleBasinId[2]);
  int nBound = 0;
  for (const WatershedBasin& b : w.basins) {
    if (b.drain == DrainKind::Boundary)
      ++nBound;
    CHECK(b.drain != DrainKind::Depression);
  }
  CHECK(nBound == 2);
}

TEST_CASE("Internal depression is classified as such", "[surface][req132][watershed]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  Pit(&v, &i);
  const WatershedResult w = ComputeWatershed(v, i);
  REQUIRE(w.ok);
  REQUIRE_FALSE(w.basins.empty());
  bool anyDep = false;
  bool anyBound = false;
  for (const WatershedBasin& b : w.basins) {
    if (b.drain == DrainKind::Depression)
      anyDep = true;
    if (b.drain == DrainKind::Boundary)
      anyBound = true;
  }
  CHECK(anyDep);
  CHECK_FALSE(anyBound);
}

TEST_CASE("Water-drop on a plane is a straight downhill line", "[surface][req133][watershed]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  PlaneSouth(&v, &i);
  const WaterDropResult d = ComputeWaterDrop(v, i, 5.0, 8.0);
  REQUIRE(d.ok);
  CHECK_FALSE(d.outside);
  CHECK(d.terminal == DrainKind::Boundary);
  REQUIRE(d.pathXyz.size() >= 6);
  CHECK(d.pathXyz[0] == Approx(5.0f).margin(0.02f));
  CHECK(d.pathXyz[1] == Approx(8.0f).margin(0.02f));
  const size_t n = d.pathXyz.size();
  CHECK(d.pathXyz[n - 3] == Approx(5.0f).margin(0.15f));
  CHECK(d.pathXyz[n - 2] == Approx(0.0f).margin(0.15f));
}

TEST_CASE("Water-drop in a pit terminates at the pit", "[surface][req133][watershed]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  Pit(&v, &i);
  const WaterDropResult d = ComputeWaterDrop(v, i, 4.2, 3.8);
  REQUIRE(d.ok);
  CHECK_FALSE(d.outside);
  CHECK(d.terminal == DrainKind::Depression);
}

TEST_CASE("Water-drop outside reports outside and draws nothing", "[surface][req133][watershed]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  PlaneSouth(&v, &i);
  const WaterDropResult d = ComputeWaterDrop(v, i, 50.0, 50.0);
  REQUIRE(d.ok);
  CHECK(d.outside);
  CHECK(d.pathXyz.empty());
}

TEST_CASE("Catchment at a pour-point matches the basin area", "[surface][req134][watershed]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  Ridge(&v, &i);
  const WatershedResult w = ComputeWatershed(v, i);
  REQUIRE(w.ok);
  REQUIRE(w.basins.size() == 2);
  const CatchmentResult c = ComputeCatchment(v, i, 0.5, 5.0);
  REQUIRE(c.ok);
  CHECK_FALSE(c.outside);
  const double west = w.basins[static_cast<size_t>(w.triangleBasinId[0])].area2d;
  CHECK(c.area2d == Approx(west).margin(0.01));
}

TEST_CASE("A planar catchment reports the plane's mean elevation", "[surface][req152][watershed]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  PlaneSouth(&v, &i);
  const CatchmentResult c = ComputeCatchment(v, i, 5.0, 5.0);
  REQUIRE(c.ok);
  CHECK_FALSE(c.outside);
  CHECK(c.meanZ == Approx(2.5).margin(0.05));
}

TEST_CASE("Catchment on a ridge unions both sides", "[surface][req134][watershed]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  Ridge(&v, &i);
  const CatchmentResult c = ComputeCatchment(v, i, 10.0, 5.0);
  REQUIRE(c.ok);
  CHECK_FALSE(c.outside);
  CHECK(c.area2d == Approx(200.0).margin(0.05));
}

TEST_CASE("Catchment miss is a named outside", "[surface][req134][watershed]") {
  std::vector<float> v;
  std::vector<std::uint32_t> i;
  PlaneSouth(&v, &i);
  const CatchmentResult c = ComputeCatchment(v, i, -10.0, -10.0);
  REQUIRE(c.ok);
  CHECK(c.outside);
  CHECK(c.area2d == Approx(0.0));
  CHECK(c.meanZ == Approx(0.0));
}

TEST_CASE("Flow arrows are chevrons along the path", "[surface][req133][watershed]") {
  const std::vector<float> path{0.f, 20.f, 5.f, 0.f, 10.f, 2.5f, 0.f, 0.f, 0.f};
  std::vector<float> arrows;
  AppendPathFlowArrows(path, &arrows);
  REQUIRE(arrows.size() >= 12);
  CHECK(arrows.size() % 6 == 0);
}
