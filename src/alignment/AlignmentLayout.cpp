// AlignmentLayout.cpp - create an alignment by layout from picked PIs (REQ-485.1, issue #121).
//
// Civil 3D's Tangent-Tangent tools: tangents run PI to PI; "With curves" puts a curve of the Curve and Spiral
// Settings radius at every interior PI, optionally between two clothoids. Tangent lengths follow from the
// tangent distances, so the result is C0 and C1 by construction.

#include "alignment/Alignment.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace alignment {
namespace {

constexpr double kGeomEps = 1e-9;

struct Corner {
  bool fitted = false;
  bool right = true;
  double delta = 0.0;   ///< Total deflection at the PI, radians.
  double tBack = 0.0;   ///< PI to the start of the curve group, along the back tangent.
  double tAhead = 0.0;  ///< PI to the end of the curve group, along the ahead tangent.
};

std::string Num(double v) {
  std::array<char, 48> b{};
  std::snprintf(b.data(), b.size(), "%g", v);
  return b.data();
}

double Azimuth(Point2 a, Point2 b) { return std::atan2(b.x - a.x, b.y - a.y); }

std::vector<Point2> DistinctPoints(const std::vector<Point2>& pis) {
  std::vector<Point2> out;
  out.reserve(pis.size());
  for (const Point2& p : pis) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
    if (!out.empty() && std::hypot(p.x - out.back().x, p.y - out.back().y) <= kGeomEps) continue;
    out.push_back(p);
  }
  assert(out.size() <= pis.size());
  return out;
}

/// The clothoid from a tangent to `radius` over `length` shifts the circle off the tangent by `p` and starts `k` before
/// the circle's foot on the tangent. Measured with the alignment's own spiral, so the curve group ends exactly on the
/// ahead tangent.
bool SpiralShift(double length, double radius, double* p, double* k) {
  assert(p != nullptr && k != nullptr);
  assert(radius > 0.0);
  *p = 0.0;
  *k = 0.0;
  if (!(length > 0.0)) return true;
  Alignment probe;
  if (!probe.AppendTangent({0.0, 0.0}, 0.0, 1.0) || !probe.AppendSpiral(length, 0.0, radius, true)) return false;
  const Point2 end = probe.EndPoint();
  const double theta = length / (2.0 * radius);
  *p = end.x - radius * (1.0 - std::cos(theta));
  *k = (end.y - 1.0) - radius * std::sin(theta);
  return true;
}

/// Tangent distances of the curve group at a PI of deflection `delta`; false when the spirals leave no curve.
bool CornerTangents(double delta, const CurveSettings& s, double* tBack, double* tAhead) {
  assert(tBack != nullptr && tAhead != nullptr);
  assert(s.radius > 0.0);
  const double r = s.radius;
  if (!(delta > kGeomEps) || delta >= kPi - kGeomEps) return false;
  if (delta - (s.spiralInLength + s.spiralOutLength) / (2.0 * r) <= kGeomEps) return false;
  double p1 = 0.0, k1 = 0.0, p2 = 0.0, k2 = 0.0;
  if (!SpiralShift(s.spiralInLength, r, &p1, &k1) || !SpiralShift(s.spiralOutLength, r, &p2, &k2)) return false;
  const double d1 = r + p1, d2 = r + p2;
  *tBack = (d2 - d1 * std::cos(delta)) / std::sin(delta) + k1;
  *tAhead = (d1 - d2 * std::cos(delta)) / std::sin(delta) + k2;
  return std::isfinite(*tBack) && std::isfinite(*tAhead);
}

/// Decides, left to right, which interior PIs get a curve. A corner that does not fit what is left of its tangents is
/// reported and stays a PI.
std::vector<Corner> FitCorners(const std::vector<Point2>& pts, const CurveSettings& s,
                               std::vector<std::string>* warnings) {
  assert(pts.size() >= 2);
  std::vector<Corner> corners(pts.size());
  if (!s.addCurves) return corners;
  double usedAtStart = 0.0;  // of the tangent leaving the previous PI
  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    Corner& c = corners[i];
    double d = std::remainder(Azimuth(pts[i], pts[i + 1]) - Azimuth(pts[i - 1], pts[i]), kTwoPi);
    c.right = d > 0.0;
    c.delta = std::fabs(d);
    const double backLeft = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y) - usedAtStart;
    const double ahead = std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
    usedAtStart = 0.0;
    if (c.delta <= kGeomEps) continue;  // collinear: nothing to curve
    if (!CornerTangents(c.delta, s, &c.tBack, &c.tAhead) || c.tBack >= backLeft - kGeomEps ||
        c.tAhead >= ahead - kGeomEps) {
      if (warnings != nullptr)
        warnings->push_back("the corner at PI " + std::to_string(i + 1) + " is too tight for radius " + Num(s.radius) +
                            "; left as a PI");
      continue;
    }
    c.fitted = true;
    usedAtStart = c.tAhead;
  }
  assert(corners.size() == pts.size());
  return corners;
}

/// Appends the curve group at a fitted PI: a Free curve between the tangents, or spiral-curve-spiral.
bool AppendCorner(const Corner& c, const CurveSettings& s, Alignment* out) {
  assert(out != nullptr && !out->Empty());
  assert(c.fitted);
  const double r = s.radius;
  const double curveDelta = c.delta - (s.spiralInLength + s.spiralOutLength) / (2.0 * r);
  if (s.spiralInLength <= 0.0 && s.spiralOutLength <= 0.0) {
    Entity e;
    e.kind = EntityKind::Arc;
    e.start = out->EndPoint();
    e.startAzimuth = out->EndAzimuth();
    e.radius = r;
    e.length = r * curveDelta;
    e.right = c.right;
    e.constraint = Entity::Constraint::Free;
    return out->AppendRaw(e);
  }
  if (s.spiralInLength > 0.0 && !out->AppendSpiral(s.spiralInLength, 0.0, r, c.right)) return false;
  if (!out->AppendCurveByDelta(r, curveDelta, c.right)) return false;
  return s.spiralOutLength <= 0.0 || out->AppendSpiral(s.spiralOutLength, r, 0.0, c.right);
}

}  // namespace

bool BuildFromPis(const std::vector<Point2>& pis, const CurveSettings& s, Alignment* out, std::string* error,
                  std::vector<std::string>* warnings) {
  assert(out != nullptr);
  assert(s.spiralInLength >= 0.0 && s.spiralOutLength >= 0.0);
  if (out == nullptr) return false;
  out->Clear();
  auto fail = [&](const std::string& m) {
    if (error != nullptr) *error = m;
    out->Clear();
    return false;
  };
  if (s.addCurves && !(s.radius > 0.0 && std::isfinite(s.radius))) return fail("the curve radius must be positive");
  const std::vector<Point2> pts = DistinctPoints(pis);
  if (pts.size() < 2) return fail("an alignment needs at least two distinct points");
  const std::vector<Corner> corners = FitCorners(pts, s, warnings);
  for (size_t j = 0; j + 1 < pts.size(); ++j) {
    const double az = Azimuth(pts[j], pts[j + 1]);
    const Point2 dir{std::sin(az), std::cos(az)};
    // A tangent after a curve starts where the curve ended, so a spiral's rounding never opens a gap.
    const Point2 from = (j > 0 && corners[j].fitted) ? out->EndPoint() : pts[j];
    const double trimEnd = corners[j + 1].fitted ? corners[j + 1].tBack : 0.0;
    const Point2 to{pts[j + 1].x - dir.x * trimEnd, pts[j + 1].y - dir.y * trimEnd};
    const double length = (to.x - from.x) * dir.x + (to.y - from.y) * dir.y;
    if (!(length > kGeomEps) || !out->AppendTangent(from, az, length))
      return fail("tangent " + std::to_string(j + 1) + " has no length left");
    if (corners[j + 1].fitted && !AppendCorner(corners[j + 1], s, out))
      return fail("the curve at PI " + std::to_string(j + 2) + " could not be built");
  }
  return true;
}

}  // namespace alignment
