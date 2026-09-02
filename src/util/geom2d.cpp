#include "geom2d.hpp"

#include <algorithm>
#include <cmath>

bool SegIntersectsAABB(float x0, float y0, float x1, float y1, float mnX, float mxX, float mnY, float mxY) {
  auto ins = [&](float x, float y) { return x >= mnX && x <= mxX && y >= mnY && y <= mxY; };
  if (ins(x0, y0) || ins(x1, y1))
    return true;
  auto edgeHit = [&](float ex0, float ey0, float ex1, float ey1) {
    float dxs = x1 - x0;
    float dys = y1 - y0;
    float dxe = ex1 - ex0;
    float dye = ey1 - ey0;
    float denom = dxs * dye - dys * dxe;
    if (std::fabs(denom) < 1e-12f)
      return false;
    float t = ((ex0 - x0) * dye - (ey0 - y0) * dxe) / denom;
    float u = ((ex0 - x0) * dys - (ey0 - y0) * dxs) / denom;
    return t >= 0.f && t <= 1.f && u >= 0.f && u <= 1.f;
  };
  if (edgeHit(mnX, mnY, mxX, mnY))
    return true;
  if (edgeHit(mxX, mnY, mxX, mxY))
    return true;
  if (edgeHit(mxX, mxY, mnX, mxY))
    return true;
  if (edgeHit(mnX, mxY, mnX, mnY))
    return true;
  return false;
}

bool CircleIntersectsAABB(float cx, float cy, float r, float mnX, float mxX, float mnY, float mxY) {
  if (r <= 0.f)
    return false;
  // Crossing fence: use the circle *curve* (circumference), not the filled disk. The old disk test
  // selected circles whenever the box lay inside the disk (closest point in rect to center < r),
  // even when the box never touched the visible circle.
  const float qx = std::clamp(cx, mnX, mxX);
  const float qy = std::clamp(cy, mnY, mxY);
  const float dx0 = cx - qx;
  const float dy0 = cy - qy;
  const float dMinSq = dx0 * dx0 + dy0 * dy0;
  auto cornerDsq = [&](float x, float y) {
    const float dx = cx - x;
    const float dy = cy - y;
    return dx * dx + dy * dy;
  };
  float dMaxSq = cornerDsq(mnX, mnY);
  dMaxSq = std::max(dMaxSq, cornerDsq(mxX, mnY));
  dMaxSq = std::max(dMaxSq, cornerDsq(mxX, mxY));
  dMaxSq = std::max(dMaxSq, cornerDsq(mnX, mxY));
  const float r2 = r * r;
  const float tol = 1e-5f * (std::fabs(r2) + 1.f);
  return dMinSq <= r2 + tol && r2 <= dMaxSq + tol;
}

bool CircleFullyInsideRect(float cx, float cy, float r, float mnX, float mxX, float mnY, float mxY) {
  if (r <= 0.f)
    return false;
  return cx - r >= mnX && cx + r <= mxX && cy - r >= mnY && cy + r <= mxY;
}

bool PointInsideClosedRect(float x, float y, float mnX, float mxX, float mnY, float mxY) {
  return x >= mnX && x <= mxX && y >= mnY && y <= mxY;
}

void WorldToViewRelativeFloat(double worldX, double worldY, double anchorX, double anchorY, float* relX,
                              float* relY) {
  *relX = static_cast<float>(worldX - anchorX);
  *relY = static_cast<float>(worldY - anchorY);
}

void CirclePointViewRel(double localCx, double localCy, double anchorX, double anchorY, double r, double angleRad,
                        float* relX, float* relY) {
  const double rcx = localCx - anchorX;
  const double rcy = localCy - anchorY;
  const double c = std::cos(angleRad);
  const double s = std::sin(angleRad);
  *relX = static_cast<float>(rcx + r * c);
  *relY = static_cast<float>(rcy + r * s);
}

int CircleTessellationSegmentCount(double radiusWorld, double orthoHalfHeightWorld, int framebufferHeightPx,
                                   int maxSegmentCap) {
  if (radiusWorld <= 1e-12)
    return 0;
  const double halfH = std::max(orthoHalfHeightWorld, 1e-12);
  const double fbH = static_cast<double>(std::max(framebufferHeightPx, 1));
  const double pixPerWorld = fbH / (2.0 * halfH);
  const double rPix = radiusWorld * pixPerWorld;
  constexpr double kTwoPi = 6.283185307179586;
  constexpr double kTargetChordPx = 4.0;
  const int segs = static_cast<int>(std::ceil(kTwoPi * rPix / kTargetChordPx));
  const int upper = std::clamp(maxSegmentCap, 8, 20000);
  return std::clamp(segs, 24, upper);
}

void CirclePointWorld(double cx, double cy, double r, double angleRad, double* outX, double* outY) {
  const double c = std::cos(angleRad);
  const double s = std::sin(angleRad);
  *outX = cx + r * c;
  *outY = cy + r * s;
}

BulgeArcSpan BulgeArc(double x0, double y0, double x1, double y1, double bulge) {
  BulgeArcSpan r;
  constexpr double kPi = 3.14159265358979323846;
  if (std::fabs(bulge) < 1e-12)
    return r;
  const double thetaMag = 4.0 * std::atan(std::fabs(bulge));  // included angle, > 0
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double chord = std::hypot(dx, dy);
  if (chord < 1e-12 || thetaMag < 1e-12)
    return r;
  const double R = chord / (2.0 * std::sin(thetaMag * 0.5));
  const double alpha = std::atan2(dy, dx);
  const double gamma = (kPi - thetaMag) / 2.0;
  const double phi = alpha + (bulge >= 0.0 ? gamma : -gamma);
  const double cx = x0 + R * std::cos(phi);
  const double cy = y0 + R * std::sin(phi);
  const double a0 = std::atan2(y0 - cy, x0 - cx);
  const double a1 = std::atan2(y1 - cy, x1 - cx);
  double sweep = a1 - a0;
  if (bulge >= 0.0 && sweep < 0.0)
    sweep += 2.0 * kPi;
  if (bulge < 0.0 && sweep > 0.0)
    sweep -= 2.0 * kPi;
  if (std::fabs(sweep) < 1e-12)
    return r;
  r.valid = true;
  r.cx = cx;
  r.cy = cy;
  r.radius = R;
  r.startAngle = a0;
  r.sweep = sweep;
  return r;
}

double ArcBulgeThrough(double ax, double ay, double px, double py, double bx, double by) {
  constexpr double kPi = 3.14159265358979323846;
  const double d = 2.0 * (ax * (py - by) + px * (by - ay) + bx * (ay - py));
  if (std::fabs(d) < 1e-9)
    return 0.0;  // collinear
  const double a2 = ax * ax + ay * ay;
  const double p2 = px * px + py * py;
  const double b2 = bx * bx + by * by;
  const double ux = (a2 * (py - by) + p2 * (by - ay) + b2 * (ay - py)) / d;
  const double uy = (a2 * (bx - px) + p2 * (ax - bx) + b2 * (px - ax)) / d;
  const double a0 = std::atan2(ay - uy, ax - ux);
  const double a1 = std::atan2(by - uy, bx - ux);
  const double ap = std::atan2(py - uy, px - ux);
  auto wrap2pi = [](double t) {
    while (t < 0.0) t += 2.0 * kPi;
    while (t >= 2.0 * kPi) t -= 2.0 * kPi;
    return t;
  };
  const double ccwEnd = wrap2pi(a1 - a0);   // CCW sweep from A to B
  const double ccwMid = wrap2pi(ap - a0);   // where P sits along that CCW path
  const double sweep = (ccwMid <= ccwEnd) ? ccwEnd : ccwEnd - 2.0 * kPi;
  return std::tan(sweep / 4.0);
}

double PointArcDistanceSq(double px, double py, const BulgeArcSpan& s) {
  if (!s.valid || s.radius <= 0.0)
    return 1e300;
  constexpr double kPi = 3.14159265358979323846;
  const double dx = px - s.cx;
  const double dy = py - s.cy;
  const double dc = std::hypot(dx, dy);
  // Is the cursor's bearing from the centre inside the arc's angular span?
  double t = std::atan2(dy, dx) - s.startAngle;
  const double sweepMag = std::fabs(s.sweep);
  // Fold `t` into the sweep direction, then to [0, 2pi).
  if (s.sweep < 0.0)
    t = -t;
  while (t < 0.0) t += 2.0 * kPi;
  while (t >= 2.0 * kPi) t -= 2.0 * kPi;
  if (t <= sweepMag) {
    const double radial = dc - s.radius;
    return radial * radial;  // perpendicular foot lands on the arc
  }
  // Outside the span: nearest of the two endpoints.
  const double e0x = s.cx + s.radius * std::cos(s.startAngle);
  const double e0y = s.cy + s.radius * std::sin(s.startAngle);
  const double e1x = s.cx + s.radius * std::cos(s.startAngle + s.sweep);
  const double e1y = s.cy + s.radius * std::sin(s.startAngle + s.sweep);
  const double d0 = (px - e0x) * (px - e0x) + (py - e0y) * (py - e0y);
  const double d1 = (px - e1x) * (px - e1x) + (py - e1y) * (py - e1y);
  return std::min(d0, d1);
}

double BulgeSegmentLength(double x0, double y0, double x1, double y1, double bulge) {
  const BulgeArcSpan a = BulgeArc(x0, y0, x1, y1, bulge);
  if (!a.valid)
    return std::hypot(x1 - x0, y1 - y0);
  return std::fabs(a.sweep) * a.radius;
}

void AppendLineSeg3(std::vector<float>& out, double x0, double y0, double z, double x1, double y1) {
  out.push_back(static_cast<float>(x0));
  out.push_back(static_cast<float>(y0));
  out.push_back(static_cast<float>(z));
  out.push_back(static_cast<float>(x1));
  out.push_back(static_cast<float>(y1));
  out.push_back(static_cast<float>(z));
}

void AppendArcLineSegments(std::vector<float>& out, double cx, double cy, double r, double startRad, double sweepRad,
                           int segments, float z) {
  if (r <= 1e-12 || segments < 1)
    return;
  for (int i = 0; i < segments; ++i) {
    const double u0 = static_cast<double>(i) / static_cast<double>(segments);
    const double u1 = static_cast<double>(i + 1) / static_cast<double>(segments);
    const double t0 = startRad + sweepRad * u0;
    const double t1 = startRad + sweepRad * u1;
    double wx0 = 0.;
    double wy0 = 0.;
    double wx1 = 0.;
    double wy1 = 0.;
    CirclePointWorld(cx, cy, r, t0, &wx0, &wy0);
    CirclePointWorld(cx, cy, r, t1, &wx1, &wy1);
    AppendLineSeg3(out, wx0, wy0, z, wx1, wy1);
  }
}

void AppendEllipseLineSegments(std::vector<float>& out, double cx, double cy, double majVx, double majVy, double ratio,
                               int segments, float z) {
  const double ma = std::hypot(majVx, majVy);
  if (ma < 1e-12 || segments < 1)
    return;
  const double ux = majVx / ma;
  const double uy = majVy / ma;
  const double px = -uy;
  const double py = ux;
  const double mb = ma * ratio;
  constexpr double kTwoPi = 6.283185307179586;
  for (int i = 0; i < segments; ++i) {
    const double u0 = kTwoPi * static_cast<double>(i) / static_cast<double>(segments);
    const double u1 = kTwoPi * static_cast<double>(i + 1) / static_cast<double>(segments);
    const double c0 = std::cos(u0);
    const double s0 = std::sin(u0);
    const double c1 = std::cos(u1);
    const double s1 = std::sin(u1);
    const double x0 = cx + ux * (ma * c0) + px * (mb * s0);
    const double y0 = cy + uy * (ma * c0) + py * (mb * s0);
    const double x1 = cx + ux * (ma * c1) + px * (mb * s1);
    const double y1 = cy + uy * (ma * c1) + py * (mb * s1);
    AppendLineSeg3(out, x0, y0, z, x1, y1);
  }
}

void ExpandExtents(double x, double y, double* mnX, double* mxX, double* mnY, double* mxY, bool* any) {
  if (!any || !mnX || !mxX || !mnY || !mxY)
    return;
  if (!*any) {
    *mnX = *mxX = x;
    *mnY = *mxY = y;
    *any = true;
  } else {
    *mnX = std::min(*mnX, x);
    *mxX = std::max(*mxX, x);
    *mnY = std::min(*mnY, y);
    *mxY = std::max(*mxY, y);
  }
}
