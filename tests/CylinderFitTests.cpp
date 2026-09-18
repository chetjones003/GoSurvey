// Least-squares cylinder-axis fit (REQ-347).
//
// Pinned without a window because the fit is pure Domain code with no file IO and no GL: given a
// local point-cloud neighborhood, does it (1) recover the axis direction/center/radius of a known
// synthetic cylinder within tolerance, (2) tolerate reasonable point-position noise, (3) refuse a
// flat/planar neighborhood and a too-sparse one rather than reporting a misleading axis (REQ-201 /
// REQ-347's "no misleading line" requirement).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <random>
#include <vector>

#include "util/cylinderfit.hpp"

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}  // namespace

using Catch::Approx;
using cylinderfit::FitCylinderAxisLeastSquares;
using cylinderfit::FitParams;
using cylinderfit::FitResult;

namespace {

/// Samples points on the surface of a cylinder of `radius`, centered at (`cx`,`cy`,`cz`), with axis
/// direction (`ax`,`ay`,`az`) (must be unit length), spanning `halfLength` on either side of the
/// center, `ringCount` rings around `pointsPerRing` each. `radialNoise` perturbs each point's radial
/// distance (0 = exact cylinder).
std::vector<double> SampleCylinder(double cx, double cy, double cz, double ax, double ay, double az,
                                    double radius, double halfLength, int ringCount,
                                    int pointsPerRing, double radialNoise = 0.0) {
  // Any vector not parallel to the axis, made perpendicular to it via Gram-Schmidt, gives one basis
  // vector of the cross-sectional plane; the axis cross that basis vector gives the other.
  double ux = 1, uy = 0, uz = 0;
  if (std::fabs(ax) > 0.9) {
    ux = 0;
    uy = 1;
    uz = 0;
  }
  const double dot = ux * ax + uy * ay + uz * az;
  ux -= dot * ax;
  uy -= dot * ay;
  uz -= dot * az;
  const double ulen = std::sqrt(ux * ux + uy * uy + uz * uz);
  ux /= ulen;
  uy /= ulen;
  uz /= ulen;
  const double vx = ay * uz - az * uy;
  const double vy = az * ux - ax * uz;
  const double vz = ax * uy - ay * ux;

  std::mt19937 rng(12345);
  std::uniform_real_distribution<double> noiseDist(-radialNoise, radialNoise);

  std::vector<double> pts;
  pts.reserve(static_cast<std::size_t>(ringCount) * pointsPerRing * 3);
  for (int i = 0; i < ringCount; ++i) {
    const double t = -halfLength + (2.0 * halfLength) * i / (ringCount - 1);
    for (int j = 0; j < pointsPerRing; ++j) {
      const double theta = kTwoPi * j / pointsPerRing;
      const double r = radius + noiseDist(rng);
      const double cosT = std::cos(theta), sinT = std::sin(theta);
      pts.push_back(cx + t * ax + r * (cosT * ux + sinT * vx));
      pts.push_back(cy + t * ay + r * (cosT * uy + sinT * vy));
      pts.push_back(cz + t * az + r * (cosT * uz + sinT * vz));
    }
  }
  return pts;
}

}  // namespace

TEST_CASE("FitCylinderAxisLeastSquares: recovers an axis-aligned cylinder exactly",
          "[cylinderfit]") {
  const auto pts = SampleCylinder(5.0, -2.0, 1.0, 0, 0, 1, 0.5, 3.0, 20, 24);
  const FitResult r = FitCylinderAxisLeastSquares(pts);
  REQUIRE(r.ok);
  CHECK(std::fabs(r.axisZ) == Approx(1.0).margin(1e-6));
  CHECK(r.centerX == Approx(5.0).margin(0.002));
  CHECK(r.centerY == Approx(-2.0).margin(0.002));
  CHECK(r.radius == Approx(0.5).margin(0.002));
  CHECK(r.rmsResidual < 1e-6);
  // inlier extent spans the sampled length
  const double spanZ = std::fabs(r.p1Z - r.p0Z);
  CHECK(spanZ == Approx(6.0).margin(0.002));
}

TEST_CASE("FitCylinderAxisLeastSquares: recovers a tilted, off-origin cylinder", "[cylinderfit]") {
  double ax = 1, ay = 1, az = 1;
  const double len = std::sqrt(ax * ax + ay * ay + az * az);
  ax /= len;
  ay /= len;
  az /= len;
  const auto pts = SampleCylinder(100.0, 200.0, 10.0, ax, ay, az, 0.75, 4.0, 25, 30);
  const FitResult r = FitCylinderAxisLeastSquares(pts);
  REQUIRE(r.ok);
  // axis direction, up to sign, matches within a small angle (dot product near +-1)
  const double dot = r.axisX * ax + r.axisY * ay + r.axisZ * az;
  CHECK(std::fabs(dot) == Approx(1.0).margin(1e-4));
  CHECK(r.radius == Approx(0.75).margin(0.002));
}

TEST_CASE("FitCylinderAxisLeastSquares: tolerates modest radial noise", "[cylinderfit]") {
  const auto pts = SampleCylinder(0, 0, 0, 0, 0, 1, 1.0, 5.0, 30, 24, /*radialNoise=*/0.02);
  const FitResult r = FitCylinderAxisLeastSquares(pts);
  REQUIRE(r.ok);
  CHECK(r.radius == Approx(1.0).margin(0.02));
}

TEST_CASE("FitCylinderAxisLeastSquares: rejects a flat/planar cluster", "[cylinderfit]") {
  std::vector<double> pts;
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> d(-1.0, 1.0);
  for (int i = 0; i < 200; ++i) {
    pts.push_back(d(rng));
    pts.push_back(d(rng));
    pts.push_back(0.0);  // z == 0 for every point: a plane, not a cylindrical shell
  }
  const FitResult r = FitCylinderAxisLeastSquares(pts);
  CHECK_FALSE(r.ok);
}

TEST_CASE("FitCylinderAxisLeastSquares: rejects a noisy, non-cylindrical scatter",
          "[cylinderfit]") {
  std::vector<double> pts;
  std::mt19937 rng(9);
  std::uniform_real_distribution<double> d(-2.0, 2.0);
  for (int i = 0; i < 100; ++i) {
    pts.push_back(d(rng));
    pts.push_back(d(rng));
    pts.push_back(d(rng));
  }
  const FitResult r = FitCylinderAxisLeastSquares(pts);
  CHECK_FALSE(r.ok);
}

TEST_CASE("FitCylinderAxisLeastSquares: rejects too few points, no crash", "[cylinderfit]") {
  const std::vector<double> pts = {0, 0, 0, 1, 0, 0, 0, 1, 0};  // 3 points, well under minPoints
  const FitResult r = FitCylinderAxisLeastSquares(pts);
  CHECK_FALSE(r.ok);
}

TEST_CASE("FitCylinderAxisLeastSquares: empty input does not crash", "[cylinderfit]") {
  const FitResult r = FitCylinderAxisLeastSquares({});
  CHECK_FALSE(r.ok);
}

TEST_CASE("FitCylinderAxisLeastSquares: custom params can tighten the residual gate",
          "[cylinderfit]") {
  const auto pts = SampleCylinder(0, 0, 0, 0, 0, 1, 1.0, 5.0, 30, 24, /*radialNoise=*/0.05);
  const FitResult loose = FitCylinderAxisLeastSquares(pts);
  REQUIRE(loose.ok);  // accepted at the default 15% residual/radius gate

  FitParams tight;
  tight.maxResidualToRadiusRatio = 0.001;
  const FitResult strict = FitCylinderAxisLeastSquares(pts, tight);
  CHECK_FALSE(strict.ok);  // the same neighborhood, rejected once the gate is tightened past its residual
}
