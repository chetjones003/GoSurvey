#pragma once

/// Pure feature-line geometry (REQ-154…160). No document types.
///
/// Bulge is AutoCAD LWPOLYLINE group 42: tan(includedAngle/4), sign = CCW from the chord.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flgeom {

constexpr double kPlanEps = 1.0e-4;
constexpr int kMaxBulgeChord = 64;

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

[[nodiscard]] inline double PlanDist(double x0, double y0, double x1, double y1) {
  return std::hypot(x1 - x0, y1 - y0);
}

[[nodiscard]] inline float BulgeFromSweepRad(double sweepRad) {
  const double half = sweepRad / 4.0;
  return static_cast<float>(std::tan(half));
}

[[nodiscard]] inline double SweepFromBulge(float bulge) {
  return 4.0 * std::atan(static_cast<double>(bulge));
}

/// Included-angle bulge from a circular arc's start, end, centre, and signed sweep.
[[nodiscard]] inline float BulgeFromArc(double startX, double startY, double endX, double endY, double cx,
                                        double cy, double sweepRad) {
  (void)startX;
  (void)startY;
  (void)endX;
  (void)endY;
  (void)cx;
  (void)cy;
  return BulgeFromSweepRad(sweepRad);
}

inline void ArcCenterFromBulge(double x0, double y0, double x1, double y1, float bulge, double* cx, double* cy,
                               double* radius, double* startAng, double* sweep) {
  const double b = static_cast<double>(bulge);
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double chord = std::hypot(dx, dy);
  *sweep = SweepFromBulge(bulge);
  if (!(chord > kPlanEps) || std::fabs(b) < 1.0e-12) {
    *cx = 0.5 * (x0 + x1);
    *cy = 0.5 * (y0 + y1);
    *radius = 0.0;
    *startAng = 0.0;
    *sweep = 0.0;
    return;
  }
  const double s = *sweep;
  const double gamma = (3.14159265358979323846 - std::fabs(s)) / 2.0;
  const double alpha = std::atan2(dy, dx);
  const double phi = alpha + (b >= 0.0 ? gamma : -gamma);
  *radius = (chord / 2.0) / std::sin(std::fabs(s) / 2.0);
  *cx = x0 + *radius * std::cos(phi);
  *cy = y0 + *radius * std::sin(phi);
  *startAng = std::atan2(y0 - *cy, x0 - *cx);
}

inline void PointOnBulge(double x0, double y0, double z0, double x1, double y1, double z1, float bulge, double t,
                         double* ox, double* oy, double* oz) {
  t = std::clamp(t, 0.0, 1.0);
  *oz = z0 + t * (z1 - z0);
  if (std::fabs(static_cast<double>(bulge)) < 1.0e-12) {
    *ox = x0 + t * (x1 - x0);
    *oy = y0 + t * (y1 - y0);
    return;
  }
  double cx = 0.0, cy = 0.0, r = 0.0, a0 = 0.0, sw = 0.0;
  ArcCenterFromBulge(x0, y0, x1, y1, bulge, &cx, &cy, &r, &a0, &sw);
  const double ang = a0 + t * sw;
  *ox = cx + r * std::cos(ang);
  *oy = cy + r * std::sin(ang);
}

inline void TessellateBulgeSegment(double x0, double y0, double z0, double x1, double y1, double z1, float bulge,
                                   std::vector<Vec3>* out, bool includeStart) {
  if (!out)
    return;
  if (includeStart)
    out->push_back({x0, y0, z0});
  if (std::fabs(static_cast<double>(bulge)) < 1.0e-12) {
    out->push_back({x1, y1, z1});
    return;
  }
  const double sw = std::fabs(SweepFromBulge(bulge));
  int n = static_cast<int>(std::ceil(sw / (5.0 * 3.14159265358979323846 / 180.0)));
  n = std::clamp(n, 2, kMaxBulgeChord);
  for (int i = 1; i <= n; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(n);
    double x = 0.0, y = 0.0, z = 0.0;
    PointOnBulge(x0, y0, z0, x1, y1, z1, bulge, t, &x, &y, &z);
    out->push_back({x, y, z});
  }
}

[[nodiscard]] inline double PlanLength(const std::vector<float>& xyz, int v0, int v1, const std::vector<float>& bulge,
                                       bool closed) {
  double total = 0.0;
  const int n = v1 - v0;
  if (n < 2)
    return 0.0;
  const int edges = closed ? n : n - 1;
  for (int e = 0; e < edges; ++e) {
    const int ia = v0 + e;
    const int ib = v0 + ((e + 1) % n);
    const size_t a = static_cast<size_t>(ia) * 3;
    const size_t b = static_cast<size_t>(ib) * 3;
    const float bu = (static_cast<size_t>(ia) < bulge.size()) ? bulge[static_cast<size_t>(ia)] : 0.f;
    if (std::fabs(static_cast<double>(bu)) < 1.0e-12) {
      total += PlanDist(xyz[a], xyz[a + 1], xyz[b], xyz[b + 1]);
    } else {
      double cx = 0.0, cy = 0.0, r = 0.0, a0 = 0.0, sw = 0.0;
      ArcCenterFromBulge(xyz[a], xyz[a + 1], xyz[b], xyz[b + 1], bu, &cx, &cy, &r, &a0, &sw);
      total += std::fabs(r * sw);
    }
  }
  return total;
}

[[nodiscard]] inline double GradePercent(double run, double rise) {
  if (!(std::fabs(run) > kPlanEps))
    return 0.0;
  return (rise / run) * 100.0;
}

/// Offset an open or closed plan chain the same way OFFSET offsets a polyline (edge parallels +
/// infinite-line intersections). Elevations are copied 1:1 when counts match.
inline bool OffsetChainXy(const std::vector<float>& xyz, int v0, int v1, bool closed, float signedD,
                          std::vector<Vec3>* out) {
  if (!out)
    return false;
  out->clear();
  const int nv = v1 - v0;
  if (nv < 2)
    return false;
  std::vector<std::pair<double, double>> v;
  v.reserve(static_cast<size_t>(nv));
  for (int i = v0; i < v1; ++i) {
    const size_t b = static_cast<size_t>(i) * 3;
    v.push_back({xyz[b], xyz[b + 1]});
  }
  const int n = static_cast<int>(v.size());
  const int nEdges = closed ? n : n - 1;
  auto unitLeft = [](double ax, double ay, double bx, double by, double* nx, double* ny) {
    const double vx = bx - ax, vy = by - ay;
    const double len = std::hypot(vx, vy);
    if (!(len > kPlanEps)) {
      *nx = 0.0;
      *ny = 1.0;
      return;
    }
    *nx = -vy / len;
    *ny = vx / len;
  };
  auto intersectInf = [](double a0x, double a0y, double b0x, double b0y, double a1x, double a1y, double b1x,
                         double b1y, double* ix, double* iy) -> bool {
    const double d0x = b0x - a0x, d0y = b0y - a0y;
    const double d1x = b1x - a1x, d1y = b1y - a1y;
    const double den = d0x * d1y - d0y * d1x;
    if (std::fabs(den) < 1.0e-12)
      return false;
    const double t = ((a1x - a0x) * d1y - (a1y - a0y) * d1x) / den;
    *ix = a0x + t * d0x;
    *iy = a0y + t * d0y;
    return true;
  };
  std::vector<std::pair<double, double>> pa(static_cast<size_t>(nEdges)), pb(static_cast<size_t>(nEdges));
  for (int ei = 0; ei < nEdges; ++ei) {
    const int ia = ei;
    const int ib = closed ? (ei + 1) % n : ei + 1;
    double nx = 0.0, ny = 0.0;
    unitLeft(v[static_cast<size_t>(ia)].first, v[static_cast<size_t>(ia)].second, v[static_cast<size_t>(ib)].first,
             v[static_cast<size_t>(ib)].second, &nx, &ny);
    pa[static_cast<size_t>(ei)] = {v[static_cast<size_t>(ia)].first + nx * signedD,
                                   v[static_cast<size_t>(ia)].second + ny * signedD};
    pb[static_cast<size_t>(ei)] = {v[static_cast<size_t>(ib)].first + nx * signedD,
                                   v[static_cast<size_t>(ib)].second + ny * signedD};
  }
  std::vector<std::pair<double, double>> xy;
  if (!closed) {
    if (nEdges == 1) {
      xy.push_back(pa[0]);
      xy.push_back(pb[0]);
    } else {
      xy.push_back(pa[0]);
      for (int ei = 0; ei < nEdges - 1; ++ei) {
        double ix = 0.0, iy = 0.0;
        if (intersectInf(pa[static_cast<size_t>(ei)].first, pa[static_cast<size_t>(ei)].second,
                         pb[static_cast<size_t>(ei)].first, pb[static_cast<size_t>(ei)].second,
                         pa[static_cast<size_t>(ei + 1)].first, pa[static_cast<size_t>(ei + 1)].second,
                         pb[static_cast<size_t>(ei + 1)].first, pb[static_cast<size_t>(ei + 1)].second, &ix, &iy))
          xy.push_back({ix, iy});
        else
          xy.push_back({0.5 * (pb[static_cast<size_t>(ei)].first + pa[static_cast<size_t>(ei + 1)].first),
                        0.5 * (pb[static_cast<size_t>(ei)].second + pa[static_cast<size_t>(ei + 1)].second)});
      }
      xy.push_back(pb[static_cast<size_t>(nEdges - 1)]);
    }
  } else {
    xy.resize(static_cast<size_t>(nEdges));
    for (int ei = 0; ei < nEdges; ++ei) {
      const int en = (ei + 1) % nEdges;
      double ix = 0.0, iy = 0.0;
      if (intersectInf(pa[static_cast<size_t>(ei)].first, pa[static_cast<size_t>(ei)].second,
                       pb[static_cast<size_t>(ei)].first, pb[static_cast<size_t>(ei)].second,
                       pa[static_cast<size_t>(en)].first, pa[static_cast<size_t>(en)].second,
                       pb[static_cast<size_t>(en)].first, pb[static_cast<size_t>(en)].second, &ix, &iy))
        xy[static_cast<size_t>(ei)] = {ix, iy};
      else
        xy[static_cast<size_t>(ei)] = {0.5 * (pb[static_cast<size_t>(ei)].first + pa[static_cast<size_t>(en)].first),
                                       0.5 * (pb[static_cast<size_t>(ei)].second + pa[static_cast<size_t>(en)].second)};
    }
  }
  const bool z1to1 = static_cast<int>(xy.size()) == nv;
  for (size_t i = 0; i < xy.size(); ++i) {
    const int src = z1to1 ? (v0 + static_cast<int>(i)) : v0;
    const size_t zb = static_cast<size_t>(src) * 3 + 2;
    const double z = zb < xyz.size() ? xyz[zb] : 0.0;
    out->push_back({xy[i].first, xy[i].second, z});
  }
  return out->size() >= 2;
}

struct HighLowHit {
  int afterVertex = 0;  ///< insert after this 0-based index in the line
  double station = 0.0;
  bool isHigh = false;
};

/// Reverse an open chain's bulge array after the vertices have been reversed in place.
/// Edge i→i+1 becomes the reversed edge at the other end, with opposite sign.
inline void ReverseOpenBulges(std::vector<float>* bulge, int nVert) {
  if (!bulge || nVert < 2)
    return;
  bulge->resize(static_cast<size_t>(nVert), 0.f);
  std::vector<float> nb(static_cast<size_t>(nVert), 0.f);
  for (int i = 0; i < nVert - 1; ++i)
    nb[static_cast<size_t>(i)] = -(*bulge)[static_cast<size_t>(nVert - 2 - i)];
  *bulge = std::move(nb);
}

/// Bulge of the circular arc from (x0,y0) to (x2,y2) that passes through (x1,y1). Zero if collinear.
[[nodiscard]] inline float BulgeThrough(double x0, double y0, double x1, double y1, double x2, double y2) {
  const double a = PlanDist(x0, y0, x1, y1);
  const double b = PlanDist(x1, y1, x2, y2);
  const double c = PlanDist(x0, y0, x2, y2);
  if (!(a > kPlanEps) || !(b > kPlanEps) || !(c > kPlanEps))
    return 0.f;
  const double cross = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
  if (std::fabs(cross) < 1.0e-12)
    return 0.f;
  double cx = 0.0, cy = 0.0;
  const double d = 2.0 * ((x0 * (y1 - y2)) + (x1 * (y2 - y0)) + (x2 * (y0 - y1)));
  if (std::fabs(d) < 1.0e-18)
    return 0.f;
  cx = ((x0 * x0 + y0 * y0) * (y1 - y2) + (x1 * x1 + y1 * y1) * (y2 - y0) + (x2 * x2 + y2 * y2) * (y0 - y1)) / d;
  cy = ((x0 * x0 + y0 * y0) * (x2 - x1) + (x1 * x1 + y1 * y1) * (x0 - x2) + (x2 * x2 + y2 * y2) * (x1 - x0)) / d;
  const double a0 = std::atan2(y0 - cy, x0 - cx);
  const double a1 = std::atan2(y1 - cy, x1 - cx);
  const double a2 = std::atan2(y2 - cy, x2 - cx);
  auto wrap = [](double ang) {
    while (ang > 3.14159265358979323846)
      ang -= 2.0 * 3.14159265358979323846;
    while (ang < -3.14159265358979323846)
      ang += 2.0 * 3.14159265358979323846;
    return ang;
  };
  const double s01 = wrap(a1 - a0);
  const double s12 = wrap(a2 - a1);
  const double sweep = s01 + s12;
  return BulgeFromSweepRad(sweep);
}

/// Plan-segment intersection; t is the parameter on AB in (0,1). Returns false if parallel or at an endpoint.
[[nodiscard]] inline bool PlanSegIntersectT(double ax, double ay, double bx, double by, double cx, double cy,
                                            double dx, double dy, double* tAB) {
  if (!tAB)
    return false;
  const double rx = bx - ax, ry = by - ay;
  const double sx = dx - cx, sy = dy - cy;
  const double den = rx * sy - ry * sx;
  if (std::fabs(den) < 1.0e-18)
    return false;
  const double t = ((cx - ax) * sy - (cy - ay) * sx) / den;
  const double u = ((cx - ax) * ry - (cy - ay) * rx) / den;
  if (t <= 1.0e-4 || t >= 1.0 - 1.0e-4 || u <= 1.0e-4 || u >= 1.0 - 1.0e-4)
    return false;
  *tAB = t;
  return true;
}

struct TinCrossing {
  double station = 0.0;
  double x = 0.0;
  double y = 0.0;
};

/// Intersections of plan segment (x0,y0)–(x1,y1) with TIN triangle edges, as stations from (x0,y0).
inline void TinEdgeCrossings(const std::vector<float>& tinXyz, const std::vector<std::uint32_t>& indices, double x0,
                             double y0, double x1, double y1, std::vector<TinCrossing>* out) {
  if (!out)
    return;
  const double len = PlanDist(x0, y0, x1, y1);
  if (!(len > kPlanEps) || tinXyz.size() < 9 || indices.size() < 3)
    return;
  const size_t nTri = indices.size() / 3;
  for (size_t t = 0; t < nTri; ++t) {
    const std::uint32_t ia = indices[t * 3], ib = indices[t * 3 + 1], ic = indices[t * 3 + 2];
    const std::uint32_t corners[3] = {ia, ib, ic};
    for (int e = 0; e < 3; ++e) {
      const std::uint32_t u = corners[e], v = corners[(e + 1) % 3];
      const size_t ua = static_cast<size_t>(u) * 3, va = static_cast<size_t>(v) * 3;
      if (va + 1 >= tinXyz.size() || ua + 1 >= tinXyz.size())
        continue;
      double tAB = 0.0;
      if (!PlanSegIntersectT(x0, y0, x1, y1, tinXyz[ua], tinXyz[ua + 1], tinXyz[va], tinXyz[va + 1], &tAB))
        continue;
      TinCrossing c;
      c.station = tAB * len;
      c.x = x0 + tAB * (x1 - x0);
      c.y = y0 + tAB * (y1 - y0);
      bool dup = false;
      for (const TinCrossing& old : *out) {
        if (std::fabs(old.station - c.station) < kPlanEps * 10.0) {
          dup = true;
          break;
        }
      }
      if (!dup)
        out->push_back(c);
    }
  }
}

inline void FindHighLow(const std::vector<float>& xyz, int v0, int v1, std::vector<HighLowHit>* out) {
  if (!out)
    return;
  out->clear();
  const int n = v1 - v0;
  if (n < 3)
    return;
  auto z = [&](int i) { return xyz[static_cast<size_t>(v0 + i) * 3 + 2]; };
  auto run = [&](int a, int b) {
    const size_t ia = static_cast<size_t>(v0 + a) * 3;
    const size_t ib = static_cast<size_t>(v0 + b) * 3;
    return PlanDist(xyz[ia], xyz[ia + 1], xyz[ib], xyz[ib + 1]);
  };
  double sta = 0.0;
  for (int i = 1; i + 1 < n; ++i) {
    sta += run(i - 1, i);
    const double gIn = GradePercent(run(i - 1, i), z(i) - z(i - 1));
    const double gOut = GradePercent(run(i, i + 1), z(i + 1) - z(i));
    if (gIn > 0.01 && gOut < -0.01)
      out->push_back({i, sta, true});
    else if (gIn < -0.01 && gOut > 0.01)
      out->push_back({i, sta, false});
  }
}

}  // namespace flgeom
