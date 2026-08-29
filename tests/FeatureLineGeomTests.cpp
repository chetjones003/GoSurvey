#include "util/featurelinegeom.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE("Bulge of a 90-degree CCW sweep is tan(pi/8)", "[req155]") {
  const float b = flgeom::BulgeFromSweepRad(1.5707963267948966);
  REQUIRE_THAT(static_cast<double>(b), WithinAbs(std::tan(3.14159265358979323846 / 8.0), 1.0e-6));
  REQUIRE_THAT(flgeom::SweepFromBulge(b), WithinAbs(1.5707963267948966, 1.0e-5));
}

TEST_CASE("Zero bulge tessellates as the chord only", "[req155]") {
  std::vector<flgeom::Vec3> v;
  flgeom::TessellateBulgeSegment(0, 0, 10, 10, 0, 20, 0.f, &v, true);
  REQUIRE(v.size() == 2);
  REQUIRE_THAT(v.back().z, WithinAbs(20.0, 1.0e-9));
}

TEST_CASE("A 90-degree bulge tessellates more than two vertices", "[req155]") {
  const float b = flgeom::BulgeFromSweepRad(1.5707963267948966);
  std::vector<flgeom::Vec3> v;
  flgeom::TessellateBulgeSegment(1, 0, 0, 0, 1, 0, b, &v, true);
  REQUIRE(v.size() > 2);
}

TEST_CASE("Offset of a 2-vertex segment is 10 ft left of the chord", "[req157]") {
  std::vector<float> xyz = {0.f, 0.f, 5.f, 100.f, 0.f, 15.f};
  std::vector<flgeom::Vec3> out;
  REQUIRE(flgeom::OffsetChainXy(xyz, 0, 2, false, 10.f, &out));
  REQUIRE(out.size() == 2);
  REQUIRE_THAT(out[0].y, WithinAbs(10.0, 0.01));
  REQUIRE_THAT(out[1].y, WithinAbs(10.0, 0.01));
  REQUIRE_THAT(out[0].z, WithinAbs(5.0, 0.01));
}

TEST_CASE("High/low finds a peak", "[req160]") {
  std::vector<float> xyz = {0.f, 0.f, 0.f, 10.f, 0.f, 10.f, 20.f, 0.f, 0.f};
  std::vector<flgeom::HighLowHit> hits;
  flgeom::FindHighLow(xyz, 0, 3, &hits);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].isHigh);
  REQUIRE(hits[0].afterVertex == 1);
}

TEST_CASE("Grade percent is rise over run times 100", "[req154]") {
  REQUIRE_THAT(flgeom::GradePercent(100.0, 10.0), WithinAbs(10.0, 1.0e-9));
}

TEST_CASE("From-objects line is two XYZ vertices", "[req154]") {
  REQUIRE_THAT(flgeom::PlanDist(0, 0, 3, 4), WithinAbs(5.0, 1.0e-9));
}

TEST_CASE("ReverseOpenBulges negates and reorders", "[req155]") {
  std::vector<float> b = {0.4f, 0.2f, 0.f};
  flgeom::ReverseOpenBulges(&b, 3);
  REQUIRE_THAT(static_cast<double>(b[0]), WithinAbs(-0.2, 1.0e-6));
  REQUIRE_THAT(static_cast<double>(b[1]), WithinAbs(-0.4, 1.0e-6));
}

TEST_CASE("BulgeThrough of a quarter-circle through (1,0),(0.707,0.707),(0,1)", "[req155]") {
  const float b = flgeom::BulgeThrough(1.0, 0.0, 0.70710678118, 0.70710678118, 0.0, 1.0);
  REQUIRE_THAT(static_cast<double>(b), WithinAbs(std::tan(3.14159265358979323846 / 8.0), 1.0e-3));
}

TEST_CASE("TinEdgeCrossings finds a triangle-edge hit", "[req159]") {
  std::vector<float> xyz = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 10.f, 0.f};
  std::vector<std::uint32_t> idx = {0, 1, 2};
  std::vector<flgeom::TinCrossing> hits;
  flgeom::TinEdgeCrossings(xyz, idx, -1.0, 1.0, 11.0, 1.0, &hits);
  REQUIRE_FALSE(hits.empty());
}
