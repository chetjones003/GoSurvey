// REQ-125 — surface statistics from a triangulation.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdint>
#include <vector>

#include "util/surfacestats.hpp"

using Catch::Approx;

TEST_CASE("Empty or malformed input reports not built", "[surface][req125][stats]") {
  SurfaceStats a = ComputeSurfaceStats({}, {});
  CHECK_FALSE(a.built);
  CHECK(a.points == 0);
  CHECK(a.triangles == 0);

  const std::vector<float> verts{0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  SurfaceStats b = ComputeSurfaceStats(verts, {0, 1, 0});
  CHECK_FALSE(b.built);
}

TEST_CASE("A right triangle reports plan area, 3D area, and slope", "[surface][req125][stats]") {
  // (0,0,0) (10,0,0) (0,10,0) — plan area 50, grade 0, 3D area 50.
  const std::vector<float> verts{0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 10.f, 0.f};
  const std::vector<std::uint32_t> idx{0, 1, 2};
  const SurfaceStats s = ComputeSurfaceStats(verts, idx);
  REQUIRE(s.built);
  CHECK(s.points == 3);
  CHECK(s.triangles == 1);
  CHECK(s.area2d == Approx(50.0));
  CHECK(s.area3d == Approx(50.0));
  CHECK(s.minSlopePct == Approx(0.0));
  CHECK(s.maxSlopePct == Approx(0.0));
  CHECK(s.meanSlopePct == Approx(0.0));
  CHECK(s.minZ == Approx(0.0));
  CHECK(s.maxZ == Approx(0.0));
}

TEST_CASE("A 10% grade triangle reports that grade", "[surface][req125][stats]") {
  const std::vector<float> verts{0.f, 0.f, 0.f, 10.f, 0.f, 1.f, 0.f, 10.f, 0.f};
  const std::vector<std::uint32_t> idx{0, 1, 2};
  const SurfaceStats s = ComputeSurfaceStats(verts, idx);
  REQUIRE(s.built);
  CHECK(s.minSlopePct == Approx(10.0));
  CHECK(s.maxSlopePct == Approx(10.0));
  CHECK(s.meanSlopePct == Approx(10.0));
}

TEST_CASE("A mixed-sign volume triangle reports both cut and fill", "[surface][req140][stats]") {
  const std::vector<float> verts{0.f, 0.f, -1.f, 10.f, 0.f, 1.f, 0.f, 10.f, 1.f};
  const std::vector<std::uint32_t> idx{0, 1, 2};
  const SurfaceStats s = ComputeSurfaceStats(verts, idx, 0, true);
  REQUIRE(s.built);
  CHECK(s.volumeCutFt3 > 0.0);
  CHECK(s.volumeFillFt3 > 0.0);
}

TEST_CASE("An all-positive volume triangle is fill only", "[surface][req140][stats]") {
  const std::vector<float> verts{0.f, 0.f, 1.f, 10.f, 0.f, 1.f, 0.f, 10.f, 1.f};
  const std::vector<std::uint32_t> idx{0, 1, 2};
  const SurfaceStats s = ComputeSurfaceStats(verts, idx, 0, true);
  REQUIRE(s.built);
  CHECK(s.volumeCutFt3 == Approx(0.0));
  CHECK(s.volumeFillFt3 == Approx(50.0));
}
