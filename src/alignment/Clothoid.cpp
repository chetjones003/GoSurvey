#include "alignment/Clothoid.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace alignment {
namespace {

constexpr int kMaxSteps = 512;  // fixed upper bound for clothoid integration (REQ-481.3 accuracy).

double SignRight(bool right) { return right ? 1.0 : -1.0; }

void IntegrateLocal(double k0, double k1, double length, double s, double* dx, double* dy, double* dHeading) {
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

}  // namespace

double SpiralKappaStart(const Entity& e) {
  assert(e.kind == EntityKind::Spiral);
  if (IsInfiniteSpiralRadius(e.startRadius)) return 0.0;
  return SignRight(e.right) / e.startRadius;
}

double SpiralKappaEnd(const Entity& e) {
  assert(e.kind == EntityKind::Spiral);
  if (IsInfiniteSpiralRadius(e.radius)) return 0.0;
  return SignRight(e.right) / e.radius;
}

double SpiralKappaAt(const Entity& e, double s) {
  assert(e.kind == EntityKind::Spiral);
  assert(s >= 0.0);
  const double t = std::clamp(s, 0.0, e.length);
  if (!(e.length > 0.0)) return 0.0;
  const double k0 = SpiralKappaStart(e);
  const double k1 = SpiralKappaEnd(e);
  return k0 + (k1 - k0) * (t / e.length);
}

double SpiralHeadingDelta(const Entity& e, double s) {
  assert(e.kind == EntityKind::Spiral);
  const double k0 = SpiralKappaStart(e);
  const double k1 = SpiralKappaEnd(e);
  double dh = 0.0;
  double dx = 0.0;
  double dy = 0.0;
  IntegrateLocal(k0, k1, e.length, s, &dx, &dy, &dh);
  return dh;
}

Point2 SpiralDisplacementLocal(const Entity& e, double s) {
  assert(e.kind == EntityKind::Spiral);
  const double k0 = SpiralKappaStart(e);
  const double k1 = SpiralKappaEnd(e);
  double dx = 0.0;
  double dy = 0.0;
  double dh = 0.0;
  IntegrateLocal(k0, k1, e.length, s, &dx, &dy, &dh);
  (void)dh;
  const double sn = std::sin(e.startAzimuth);
  const double c = std::cos(e.startAzimuth);
  return {dx * c + dy * sn, -dx * sn + dy * c};
}

double EntityStartCurvature(const Entity& e) {
  assert(std::isfinite(e.length));
  if (e.kind == EntityKind::Line) return 0.0;
  if (e.kind == EntityKind::Arc) return (e.right ? 1.0 : -1.0) / e.radius;
  assert(e.kind == EntityKind::Spiral);
  return SpiralKappaStart(e);
}

double EntityEndCurvature(const Entity& e) {
  assert(std::isfinite(e.length));
  if (e.kind == EntityKind::Line) return 0.0;
  if (e.kind == EntityKind::Arc) return (e.right ? 1.0 : -1.0) / e.radius;
  assert(e.kind == EntityKind::Spiral);
  return SpiralKappaEnd(e);
}

}  // namespace alignment
