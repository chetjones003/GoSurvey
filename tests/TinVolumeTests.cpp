#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "tinvolume.hpp"

#include <cstdint>
#include <vector>

namespace {

// Two 10×10 squares, same plan, comparison 5 ft above base.
void TwoPlanes(std::vector<float>* baseV, std::vector<std::uint32_t>* baseI, std::vector<float>* compV,
               std::vector<std::uint32_t>* compI, float dz) {
  *baseV = {0, 0, 0, 10, 0, 0, 10, 10, 0, 0, 10, 0};
  *compV = {0, 0, dz, 10, 0, dz, 10, 10, dz, 0, 10, dz};
  *baseI = {0, 1, 2, 0, 2, 3};
  *compI = {0, 1, 2, 0, 2, 3};
}

}  // namespace

TEST_CASE("TIN volume surface Z is comparison minus base", "[tinvolume][req136]") {
  std::vector<float> bv, cv;
  std::vector<std::uint32_t> bi, ci;
  TwoPlanes(&bv, &bi, &cv, &ci, 5.f);
  const TinBuildResult r = BuildTinVolumeSurface(bv, bi, cv, ci, 0.0, 0.0);
  REQUIRE(r.ok());
  REQUIRE(r.triangleCount() >= 1);
  for (int i = 0; i < r.vertexCount(); ++i) {
    REQUIRE_THAT(static_cast<double>(r.vertsXyz[static_cast<size_t>(i) * 3 + 2]),
                 Catch::Matchers::WithinAbs(5.0, 0.01));
  }
}

TEST_CASE("TIN volume surface with no plan overlap is empty", "[tinvolume][req136]") {
  std::vector<float> bv, cv;
  std::vector<std::uint32_t> bi, ci;
  TwoPlanes(&bv, &bi, &cv, &ci, 5.f);
  for (size_t i = 0; i + 2 < cv.size(); i += 3) {
    cv[i] += 100.f;
    cv[i + 1] += 100.f;
  }
  const TinBuildResult r = BuildTinVolumeSurface(bv, bi, cv, ci, 0.0, 0.0);
  REQUIRE_FALSE(r.ok());
  REQUIRE(r.status == TinBuildStatus::TooFewPoints);
}

TEST_CASE("TIN volume surface refuses a null triangulation", "[tinvolume][req136]") {
  std::vector<float> bv = {0, 0, 0, 10, 0, 0, 10, 10, 0};
  std::vector<std::uint32_t> bi = {0, 1, 2};
  std::vector<float> emptyV;
  std::vector<std::uint32_t> emptyI;
  const TinBuildResult r = BuildTinVolumeSurface(bv, bi, emptyV, emptyI, 0.0, 0.0);
  REQUIRE_FALSE(r.ok());
}
