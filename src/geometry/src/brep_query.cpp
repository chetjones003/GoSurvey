#include <GoSurvey/Geometry/brep.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <unordered_map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>
#include "brep_internal.hpp"

namespace brep {

namespace {

/// The patch parameter `(u, v)` whose surface point is nearest \p p: a coarse grid search to pick a
/// basin, then Gauss–Newton on `|S(u,v) - p|^2` using the analytic first derivatives. Clamped to the
/// patch domain. This is what makes a face snap on a lofted surface land **on the patch** rather than
/// on the tessellator's chord (REQ-315 acceptance).
void NurbsInvertParam(const nurbs::Patch& patch, const Vec3& p, double* outU, double* outV) {
  const double u0 = nurbs::UMin(patch);
  const double u1 = nurbs::UMax(patch);
  const double v0 = nurbs::VMin(patch);
  const double v1 = nurbs::VMax(patch);
  double bu = 0.5 * (u0 + u1);
  double bv = 0.5 * (v0 + v1);
  double bd = std::numeric_limits<double>::max();
  constexpr int kGrid = 12;
  for (int i = 0; i <= kGrid; ++i)
    for (int j = 0; j <= kGrid; ++j) {
      const double u = u0 + (u1 - u0) * i / kGrid;
      const double v = v0 + (v1 - v0) * j / kGrid;
      const double d = ray3d::Length(ray3d::Sub(nurbs::Evaluate(patch, u, v), p));
      if (d < bd) {
        bd = d;
        bu = u;
        bv = v;
      }
    }
  for (int it = 0; it < 16; ++it) {
    const nurbs::SurfacePoint sp = nurbs::EvaluateWithDerivs(patch, bu, bv);
    const Vec3 r = ray3d::Sub(sp.p, p);
    const double a = ray3d::Dot(sp.du, sp.du);
    const double b = ray3d::Dot(sp.du, sp.dv);
    const double c = ray3d::Dot(sp.dv, sp.dv);
    const double det = a * c - b * b;
    if (!(std::fabs(det) > 1e-20))
      break;
    const double g1 = ray3d::Dot(r, sp.du);
    const double g2 = ray3d::Dot(r, sp.dv);
    const double du = -(c * g1 - b * g2) / det;
    const double dv = -(a * g2 - b * g1) / det;
    bu = std::clamp(bu + du, u0, u1);
    bv = std::clamp(bv + dv, v0, v1);
    if (std::fabs(du) + std::fabs(dv) < 1e-13 * (1.0 + std::fabs(bu) + std::fabs(bv)))
      break;
  }
  *outU = bu;
  *outV = bv;
}

}  // namespace

Vec3 ClosestPointOnSurface(const Surface& sf, const Vec3& p) {
  const Vec3 local = ucs::WorldToUcs(sf.frame, p);
  auto toWorld = [&sf](const Vec3& v) { return ucs::UcsToWorld(sf.frame, v); };
  // The radial direction in the frame's XY plane. Degenerate exactly on the axis, which is the one
  // input for which "nearest point" has no single answer.
  const double rho = std::sqrt(local.x * local.x + local.y * local.y);

  switch (sf.kind) {
  case SurfaceKind::Plane:
    return toWorld(Vec3{local.x, local.y, 0.0});
  case SurfaceKind::Cylinder: {
    const double a = sf.radius;
    const double b = sf.radius2;
    const bool ellipse = b > 1e-9 && std::fabs(a - b) > 1e-8 * std::max(a, 1e-9);
    if (!ellipse) {
      if (!(rho > 1e-12))
        return p;
      const double k = sf.radius / rho;
      return toWorld(Vec3{local.x * k, local.y * k, local.z});
    }
    double th = std::atan2(local.y / b, local.x / a);
    for (int it = 0; it < 8; ++it) {
      const double c = std::cos(th);
      const double s = std::sin(th);
      const double px = a * c;
      const double py = b * s;
      const double dx = -a * s;
      const double dy = b * c;
      const double f = (px - local.x) * dx + (py - local.y) * dy;
      const double df = dx * dx + (px - local.x) * (-a * c) + dy * dy + (py - local.y) * (-b * s);
      if (!(std::fabs(df) > 1e-18))
        break;
      th -= f / df;
    }
    return toWorld(Vec3{a * std::cos(th), b * std::sin(th), local.z});
  }
  case SurfaceKind::Cone: {
    if (!(rho > 1e-12))
      return p;
    // Work in the (rho, z) half-plane, where the cone is the straight segment from (r0, 0) to
    // (r1, h) — so this is a point-to-line projection, and the taper is handled by the same
    // arithmetic that handles a cylinder rather than by a special case.
    const Vec3 a{sf.radius, 0.0, 0.0};
    const Vec3 b{sf.radius2, 0.0, sf.height};
    const Vec3 ab = ray3d::Sub(b, a);
    const double denom = ray3d::Dot(ab, ab);
    if (!(denom > 1e-24))
      return p;
    const Vec3 ap{rho - a.x, 0.0, local.z - a.z};
    const double t = ray3d::Dot(ap, ab) / denom;
    const double rhoOn = a.x + ab.x * t;
    const double zOn = a.z + ab.z * t;
    const double k = rhoOn / rho;
    return toWorld(Vec3{local.x * k, local.y * k, zOn});
  }
  case SurfaceKind::Sphere: {
    const double len = ray3d::Length(local);
    if (!(len > 1e-12))
      return p;
    return toWorld(ray3d::Scale(local, sf.radius / len));
  }
  case SurfaceKind::Torus: {
    if (!(rho > 1e-12))
      return p;  // on the axis: every point of the ring is equidistant
    // Walk to the tube's centre circle first, then out along the tube.
    const Vec3 ring{local.x * (sf.radius / rho), local.y * (sf.radius / rho), 0.0};
    const Vec3 out = ray3d::Sub(local, ring);
    const double outLen = ray3d::Length(out);
    if (!(outLen > 1e-12))
      return p;  // exactly on the tube's centre circle
    return toWorld(ray3d::Add(ring, ray3d::Scale(out, sf.radius2 / outLen)));
  }
  case SurfaceKind::Nurbs: {
    double u = 0.0;
    double v = 0.0;
    NurbsInvertParam(sf.patch, p, &u, &v);
    return nurbs::Evaluate(sf.patch, u, v);
  }
  }
  return p;
}

Vec3 ClosestPointOnEdge(const Solid& s, const Edge& e, const Vec3& p) {
  if (e.kind == CurveKind::Intersection) {
    const std::vector<Vec3> poly = MarchIntersectionCurve(s, e, 256);
    Vec3 best = poly.empty() ? Vec3{} : poly.front();
    double bestD = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
      const Vec3 ab = ray3d::Sub(poly[i + 1], poly[i]);
      const double denom = ray3d::Dot(ab, ab);
      const double u = denom > 1e-24 ? std::clamp(ray3d::Dot(ray3d::Sub(p, poly[i]), ab) / denom, 0.0, 1.0) : 0.0;
      const Vec3 c = ray3d::Add(poly[i], ray3d::Scale(ab, u));
      const double d = ray3d::Length(ray3d::Sub(c, p));
      if (d < bestD) {
        bestD = d;
        best = c;
      }
    }
    return best;
  }
  if (e.kind == CurveKind::Line) {
    const Vec3& a = s.vertices[static_cast<std::size_t>(e.v0)].p;
    const Vec3& b = s.vertices[static_cast<std::size_t>(e.v1)].p;
    const Vec3 ab = ray3d::Sub(b, a);
    const double denom = ray3d::Dot(ab, ab);
    if (!(denom > 1e-24))
      return a;
    const double t = std::clamp(ray3d::Dot(ray3d::Sub(p, a), ab) / denom, 0.0, 1.0);
    return ray3d::Add(a, ray3d::Scale(ab, t));
  }
  if (e.kind == CurveKind::Ellipse) {
    // No closed form for the nearest point on an ellipse; sample the swept range and polish the best
    // sample with a few Newton steps on d/dt |E(t) - p|^2. Clamped to the edge's own extent.
    const double t0 = EllipseParamOf(e, s.vertices[static_cast<std::size_t>(e.v0)].p);
    auto at = [&](double th) {
      return ray3d::Add(e.frame.origin,
                        ray3d::Add(ray3d::Scale(e.frame.xAxis, e.radius * std::cos(th)),
                                   ray3d::Scale(e.frame.yAxis, e.radius2 * std::sin(th))));
    };
    double best = 0.0;
    double bestD = std::numeric_limits<double>::max();
    for (int i = 0; i <= 64; ++i) {
      const double u = static_cast<double>(i) / 64.0;
      const double d = ray3d::Length(ray3d::Sub(at(t0 + e.sweep * u), p));
      if (d < bestD) {
        bestD = d;
        best = u;
      }
    }
    double u = best;
    for (int it = 0; it < 8; ++it) {
      const double th = t0 + e.sweep * u;
      const Vec3 E = at(th);
      const Vec3 dE = ray3d::Add(ray3d::Scale(e.frame.xAxis, -e.radius * std::sin(th)),
                                 ray3d::Scale(e.frame.yAxis, e.radius2 * std::cos(th)));
      const Vec3 ddE = ray3d::Add(ray3d::Scale(e.frame.xAxis, -e.radius * std::cos(th)),
                                  ray3d::Scale(e.frame.yAxis, -e.radius2 * std::sin(th)));
      const Vec3 r = ray3d::Sub(E, p);
      const double g = ray3d::Dot(r, dE) * e.sweep;
      const double h = (ray3d::Dot(dE, dE) + ray3d::Dot(r, ddE)) * e.sweep * e.sweep;
      if (!(std::fabs(h) > 1e-18))
        break;
      u = std::clamp(u - g / h, 0.0, 1.0);
    }
    return at(t0 + e.sweep * u);
  }
  // An arc: drop onto its plane, take the angle there, and if that angle is outside the swept range,
  // answer with whichever END is nearer **round the circle**.
  //
  // A plain `clamp` on the raw `atan2` result is wrong and quietly so, which is worth spelling out
  // because it is what this function did until a review caught it. `atan2` returns (-pi, pi], so for
  // a half-arc spanning [0, pi] a probe at -2.0 rad is 2.0 rad from the start and only 1.14 rad from
  // the end — but it clamps to the start, because -2.0 is simply the smaller number. The answer is
  // still ON the arc, which is why nothing crashed and why a test that only checked "is it on the
  // arc" passed: it is just the wrong end of it.
  //
  // Measuring the angle FORWARD from the start, in the sweep's own direction, removes the branch cut
  // entirely — both senses then share one comparison, and a full-circle edge (sweep = 2*pi) falls out
  // as the case where nothing is ever outside.
  const ucs::Point2D flat = ucs::WorldToPlane(e.frame, p);
  if (!(std::fabs(flat.x) > 1e-12 || std::fabs(flat.y) > 1e-12))
    return EdgePointAt(s, e, 0.0);  // on the centre: no angle is defined
  const double angle = std::atan2(flat.y, flat.x);
  const double span = std::fabs(e.sweep);
  const bool forward = e.sweep >= 0.0;

  double t = std::fmod(forward ? angle : -angle, kTwoPi);
  if (t < 0.0)
    t += kTwoPi;  // now in [0, 2*pi): how far round from the start, the way the arc runs

  double param = t;
  if (t > span) {
    // Outside the sweep. Two gaps: past the end, and back round to the start. Nearer wins.
    param = (t - span <= kTwoPi - t) ? span : 0.0;
  }
  return ucs::PointOnPlaneCircle(e.frame, e.radius, forward ? param : -param);
}

namespace {

[[nodiscard]] Vec3 AverageOfRing(const std::vector<Vec3>& r) {
  Vec3 c{};
  for (const Vec3& p : r)
    c = ray3d::Add(c, p);
  return r.empty() ? c : ray3d::Scale(c, 1.0 / static_cast<double>(r.size()));
}
} // namespace

Vec3 PlanarFaceCentroid(const Solid& s, const Face& f) {
  if (f.loops.empty())
    return Vec3{};
  // Subdivide curved edges so a circular rim contributes many points, not just its two topology
  // vertices — averaging just the vertices of a full circle's rim is nowhere near its centre.
  std::vector<Vec3> ring;
  for (const EdgeUse& u : f.loops[0].uses) {
    const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
    const int steps = (e.kind == CurveKind::Line) ? 1 : 24;
    for (int i = 0; i < steps; ++i) {
      const double t = static_cast<double>(i) / static_cast<double>(steps);
      const double tt = u.reversed ? (1.0 - t) : t;
      ring.push_back(EdgePointAt(s, e, tt));
    }
  }
  if (ring.size() < 3)
    return AverageOfRing(ring);

  // Project onto the face's own plane basis and take the exact area-weighted (shoelace) centroid of
  // the resulting 2D polygon, then map back to 3D.
  const ucs::Ucs& fr = f.surface.frame;
  std::vector<double> u2(ring.size());
  std::vector<double> v2(ring.size());
  for (std::size_t i = 0; i < ring.size(); ++i) {
    const Vec3 d = ray3d::Sub(ring[i], fr.origin);
    u2[i] = ray3d::Dot(d, fr.xAxis);
    v2[i] = ray3d::Dot(d, fr.yAxis);
  }
  double area = 0.0, cu = 0.0, cv = 0.0;
  for (std::size_t i = 0; i < ring.size(); ++i) {
    const std::size_t j = (i + 1) % ring.size();
    const double cross = u2[i] * v2[j] - u2[j] * v2[i];
    area += cross;
    cu += (u2[i] + u2[j]) * cross;
    cv += (v2[i] + v2[j]) * cross;
  }
  area *= 0.5;
  if (!(std::fabs(area) > 1e-12))
    return AverageOfRing(ring);  // degenerate (zero-area) loop
  cu /= (6.0 * area);
  cv /= (6.0 * area);
  return ray3d::Add(fr.origin, ray3d::Add(ray3d::Scale(fr.xAxis, cu), ray3d::Scale(fr.yAxis, cv)));
}

Vec3 CurvedFaceMidpoint(const Face& f) {
  const double u = 0.5 * (f.uStart + f.uEnd);
  const double v = 0.5 * (f.vStart + f.vEnd);
  const Surface& sf = f.surface;
  switch (sf.kind) {
  case SurfaceKind::Plane:
    return sf.frame.origin;  // caller error (only meant for curved faces) — a harmless fallback
  case SurfaceKind::Cylinder:
    return CylinderPoint(sf, u, v);
  case SurfaceKind::Cone:
    return ConicalPoint(sf, sf.radius, sf.radius2, u, v);
  case SurfaceKind::Sphere:
    return SphericalPoint(sf, u, v);
  case SurfaceKind::Torus:
    return ToroidalPoint(sf, u, v);
  case SurfaceKind::Nurbs:
    return nurbs::Evaluate(sf.patch, u, v);
  }
  return sf.frame.origin;
}

} // namespace brep
