#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "surfacequery.hpp"
#include "tinbuild.hpp"

#include <cmath>
#include <vector>

TEST_CASE("Quick profile midpoint on plane Z=X is 5", "[req145][profile]") {
  const std::vector<float> v{0.f, 0.f, 0.f, 10.f, 0.f, 10.f, 0.f, 10.f, 0.f};
  const std::vector<std::uint32_t> idx{0, 1, 2};
  TinSurfaceQuery q(v, idx);
  std::vector<SurfaceProfileSample> s;
  REQUIRE(SampleSurfaceProfileLine(q, 0.0, 0.0, 10.0, 0.0, 1.0, 4096, &s));
  REQUIRE(s.size() >= 2);
  bool found = false;
  double midZ = 0.0;
  for (const SurfaceProfileSample& p : s) {
    if (p.onSurface && std::fabs(p.station - 5.0) < 0.01) {
      found = true;
      midZ = p.z;
      break;
    }
  }
  REQUIRE(found);
  REQUIRE_THAT(midZ, Catch::Matchers::WithinAbs(5.0, 0.01));
}

TEST_CASE("Quick profile miss does not invent elevations", "[req145][profile]") {
  const std::vector<float> v{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  const std::vector<std::uint32_t> idx{0, 1, 2};
  TinSurfaceQuery q(v, idx);
  std::vector<SurfaceProfileSample> s;
  REQUIRE(SampleSurfaceProfileLine(q, 50.0, 50.0, 60.0, 50.0, 1.0, 4096, &s));
  REQUIRE_FALSE(s.empty());
  for (const SurfaceProfileSample& p : s)
    REQUIRE_FALSE(p.onSurface);
}

TEST_CASE("Quick profile zero length is refused", "[req145][profile]") {
  const std::vector<float> v{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  const std::vector<std::uint32_t> idx{0, 1, 2};
  TinSurfaceQuery q(v, idx);
  std::vector<SurfaceProfileSample> s;
  REQUIRE_FALSE(SampleSurfaceProfileLine(q, 1.0, 1.0, 1.0, 1.0, 1.0, 4096, &s));
  REQUIRE(s.empty());
}
