#include <GoSurvey/Geometry/ClothoidIntegration.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace geometry {
namespace {

constexpr int kMaxSteps = 512;  // fixed upper bound for clothoid integration (REQ-481.3 accuracy).

}  // namespace

void IntegrateClothoidLocal(double k0, double k1, double length, double s, double* dx, double* dy, double* dHeading) {
  assert(dx != nullptr);
  assert(dy != nullptr);
  assert(dHeading != nullptr);
  assert(length > 0.0);
  assert(s >= 0.0);
  const double travel = std::min(s, length);
  if (!(travel > 0.0)) {
    *dx = 0.0;
    *dy = 0.0;
    *dHeading = 0.0;
    return;
  }
  const double ds = travel / static_cast<double>(kMaxSteps);
  double x = 0.0;
  double y = 0.0;
  double heading = 0.0;
  for (int i = 0; i < kMaxSteps; ++i) {
    const double uMid = (static_cast<double>(i) + 0.5) * ds;
    const double kMid = k0 + (k1 - k0) * (uMid / length);
    const double dh = kMid * ds;
    const double hMid = heading + dh * 0.5;
    x += ds * std::sin(hMid);
    y += ds * std::cos(hMid);
    heading += dh;
  }
  *dx = x;
  *dy = y;
  *dHeading = heading;
}

}  // namespace geometry
