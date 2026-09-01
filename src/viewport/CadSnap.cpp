#include "CadSnap.hpp"

#include "SurveyPoints.hpp"
#include "geom2d.hpp"
#include "util/cadblock.hpp"
#include "util/curveintersect.hpp"

#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdint>
#include <vector>

namespace CadSnap {

float WorldToleranceFromPixels(float viewportHeightPx, float orthoHalfHeightWorld, float pixels) {
  const float vph = std::max(viewportHeightPx, 1.f);
  const float worldPerPixel = (2.f * orthoHalfHeightWorld) / vph;
  return pixels * worldPerPixel;
}

namespace {

constexpr float kHugePickDistSq = 1.e30f;

[[nodiscard]] float DistSqPointToSegment(float px, float py, float ax, float ay, float bx, float by) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1.e-18f) {
    const float dx = px - ax;
    const float dy = py - ay;
    return dx * dx + dy * dy;
  }
  float t = ((px - ax) * vx + (py - ay) * vy) / len2;
  t = std::clamp(t, 0.f, 1.f);
  const float qx = ax + t * vx;
  const float qy = ay + t * vy;
  const float dx = px - qx;
  const float dy = py - qy;
  return dx * dx + dy * dy;
}

[[nodiscard]] float MinDistSqToSurveyMarker(float wx, float wy, float e, float n, float halfArmWorld) {
  const float s = std::max(halfArmWorld, 1.e-8f);
  const float d1 = DistSqPointToSegment(wx, wy, e - s, n - s, e + s, n + s);
  const float d2 = DistSqPointToSegment(wx, wy, e - s, n + s, e + s, n - s);
  return std::min(d1, d2);
}

/// An arc's plane, and whether it is the flat one (REQ-312).
///
/// True means the arc lies in a plane parallel to world XY -- every arc that predates the normal --
/// and \p outPlane is left alone so the caller keeps its existing two-dimensional arithmetic, bit
/// for bit. Otherwise the arc's own frame is built here, once per arc rather than once per sample.
[[nodiscard]] bool ArcSnapPlane(const CadArc& a, ucs::Ucs* outPlane) {
  if (IsFlatNormal(a.nx, a.ny, a.nz))
    return true;
  if (outPlane)
    *outPlane = CurvePlane(a);
  return false;
}

/// The point at angle \p t on an arc, in the plane the arc actually lies in.
///
/// A snap has to offer points that are ON the drawn curve. A tilted arc walked in the XY projection
/// puts every candidate somewhere the curve does not go, so the glyph marks empty space and a click
/// commits to a point that is not on the object it claims to have snapped to (REQ-062, REQ-201).
void ArcSnapPoint(const CadArc& a, const ucs::Ucs& plane, bool flat, double t, float* ox, float* oy, float* oz) {
  if (flat) {
    double x = 0.;
    double y = 0.;
    CirclePointWorld(static_cast<double>(a.cx), static_cast<double>(a.cy), static_cast<double>(a.r), t, &x, &y);
    *ox = static_cast<float>(x);
    *oy = static_cast<float>(y);
    *oz = a.z;
    return;
  }
  const ray3d::Vec3 p = CurvePointAt(plane, static_cast<double>(a.r), t);
  *ox = static_cast<float>(p.x);
  *oy = static_cast<float>(p.y);
  *oz = static_cast<float>(p.z);
}

[[nodiscard]] float CircleCenterPickDistSq(float wx, float wy, float cx, float cy, float r, float tolWorld) {
  if (r <= 1.e-6f)
    return kHugePickDistSq;
  const float d = std::hypot(wx - cx, wy - cy);
  if (d <= r)
    return 0.f;
  const float out = d - r;
  if (out <= tolWorld)
    return out * out;
  return kHugePickDistSq;
}

[[nodiscard]] bool EllipseContainsPoint(float wx, float wy, const CadEllipse& el) {
  const float ma = std::hypot(el.majVx, el.majVy);
  if (ma < 1.e-8f)
    return false;
  const float ux = el.majVx / ma;
  const float uy = el.majVy / ma;
  const float px = -uy;
  const float py = ux;
  const float mb = ma * el.ratio;
  const float dx = wx - el.cx;
  const float dy = wy - el.cy;
  const float u = dx * ux + dy * uy;
  const float v = dx * px + dy * py;
  const float uu = u / std::max(ma, 1.e-8f);
  const float vv = v / std::max(mb, 1.e-8f);
  return (uu * uu + vv * vv) <= 1.f + 1.e-5f;
}

[[nodiscard]] float EllipseCenterPickDistSq(float wx, float wy, const CadEllipse& el, float tolWorld) {
  if (EllipseContainsPoint(wx, wy, el))
    return 0.f;
  constexpr int kSeg = 48;
  constexpr float kTwoPi = 6.28318530718f;
  const float ma = std::hypot(el.majVx, el.majVy);
  if (ma < 1.e-8f)
    return kHugePickDistSq;
  const float ux = el.majVx / ma;
  const float uy = el.majVy / ma;
  const float px = -uy;
  const float py = ux;
  const float mb = ma * el.ratio;
  float minD2 = kHugePickDistSq;
  for (int i = 0; i < kSeg; ++i) {
    const float ang0 = kTwoPi * static_cast<float>(i) / static_cast<float>(kSeg);
    const float ang1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(kSeg);
    const float c0 = std::cos(ang0);
    const float s0 = std::sin(ang0);
    const float c1 = std::cos(ang1);
    const float s1 = std::sin(ang1);
    const float x0 = el.cx + ux * (ma * c0) + px * (mb * s0);
    const float y0 = el.cy + uy * (ma * c0) + py * (mb * s0);
    const float x1 = el.cx + ux * (ma * c1) + px * (mb * s1);
    const float y1 = el.cy + uy * (ma * c1) + py * (mb * s1);
    minD2 = std::min(minD2, DistSqPointToSegment(wx, wy, x0, y0, x1, y1));
  }
  const float md = std::sqrt(std::min(std::max(minD2, 0.f), 1.e20f));
  if (md <= tolWorld)
    return minD2;
  return kHugePickDistSq;
}

[[nodiscard]] bool PointInClosedPoly(float wx, float wy, const std::vector<float>& V, int v0, int v1) {
  const int n = v1 - v0;
  if (n < 3)
    return false;
  bool c = false;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    const float yi = V[static_cast<size_t>((v0 + i) * 3 + 1)];
    const float yj = V[static_cast<size_t>((v0 + j) * 3 + 1)];
    if ((yi > wy) == (yj > wy))
      continue;
    const float xi = V[static_cast<size_t>((v0 + i) * 3)];
    const float xj = V[static_cast<size_t>((v0 + j) * 3)];
    const float t = (yj - yi);
    const float xInt = (t > 1.e-12f || t < -1.e-12f) ? (xj - xi) * (wy - yi) / t + xi : xi;
    if (wx < xInt)
      c = !c;
  }
  return c;
}

[[nodiscard]] float ClosedPolyGeometricPickDistSq(float wx, float wy, const std::vector<float>& V, int v0,
                                                  int v1) {
  const int n = v1 - v0;
  if (n < 3)
    return kHugePickDistSq;
  float minD2 = kHugePickDistSq;
  for (int i = 0; i < n; ++i) {
    const int ia = v0 + i;
    const int ib = v0 + ((i + 1) % n);
    const float ax = V[static_cast<size_t>(ia * 3)];
    const float ay = V[static_cast<size_t>(ia * 3 + 1)];
    const float bx = V[static_cast<size_t>(ib * 3)];
    const float by = V[static_cast<size_t>(ib * 3 + 1)];
    minD2 = std::min(minD2, DistSqPointToSegment(wx, wy, ax, ay, bx, by));
  }
  if (PointInClosedPoly(wx, wy, V, v0, v1))
    return 0.f;
  return minD2;
}

[[nodiscard]] bool ClosedPolylineCentroid(const std::vector<float>& V, int v0, int v1, float* outCx,
                                          float* outCy) {
  const int n = v1 - v0;
  if (n < 3 || !outCx || !outCy)
    return false;
  double a2 = 0.0;
  double cxa = 0.0;
  double cya = 0.0;
  for (int i = 0; i < n; ++i) {
    const int ia = v0 + i;
    const int ib = v0 + ((i + 1) % n);
    const double xi = static_cast<double>(V[static_cast<size_t>(ia * 3)]);
    const double yi = static_cast<double>(V[static_cast<size_t>(ia * 3 + 1)]);
    const double xj = static_cast<double>(V[static_cast<size_t>(ib * 3)]);
    const double yj = static_cast<double>(V[static_cast<size_t>(ib * 3 + 1)]);
    const double cross = xi * yj - xj * yi;
    a2 += cross;
    cxa += (xi + xj) * cross;
    cya += (yi + yj) * cross;
  }
  if (std::fabs(a2) < 1.e-12) {
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < n; ++i) {
      sx += static_cast<double>(V[static_cast<size_t>((v0 + i) * 3)]);
      sy += static_cast<double>(V[static_cast<size_t>((v0 + i) * 3 + 1)]);
    }
    *outCx = static_cast<float>(sx / static_cast<double>(n));
    *outCy = static_cast<float>(sy / static_cast<double>(n));
    return true;
  }
  const double inv = 1.0 / (3.0 * a2);
  *outCx = static_cast<float>(cxa * inv);
  *outCy = static_cast<float>(cya * inv);
  return std::isfinite(static_cast<double>(*outCx)) && std::isfinite(static_cast<double>(*outCy));
}

// Does polyline \p pi bound an area, so a geometric center is meaningful? Two ways to be closed: the
// explicit closed flag (RECT and CLOSE set it), or vertices drawn back onto the start point — a "joined"
// polyline, which AutoCAD also gives a geometric center. The coincidence test is relative to the polyline's
// own extent so it holds at survey-scale and at millimetre-scale alike.
[[nodiscard]] bool PolylineBoundsArea(const AppCommandState& cmd, int pi, int v0, int v1) {
  if (v1 - v0 < 3)
    return false;
  if (static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)])
    return true;
  if (v1 - v0 < 4)
    return false;  // first == last leaves only two distinct corners — no area
  const std::vector<float>& V = cmd.userPolylineVerts;
  float mnX = V[static_cast<size_t>(v0 * 3)], mxX = mnX;
  float mnY = V[static_cast<size_t>(v0 * 3 + 1)], mxY = mnY;
  for (int i = v0; i < v1; ++i) {
    mnX = std::min(mnX, V[static_cast<size_t>(i * 3)]);
    mxX = std::max(mxX, V[static_cast<size_t>(i * 3)]);
    mnY = std::min(mnY, V[static_cast<size_t>(i * 3 + 1)]);
    mxY = std::max(mxY, V[static_cast<size_t>(i * 3 + 1)]);
  }
  const float tol = std::max(1.e-6f * std::hypot(mxX - mnX, mxY - mnY), 1.e-9f);
  const float dx = V[static_cast<size_t>((v1 - 1) * 3)] - V[static_cast<size_t>(v0 * 3)];
  const float dy = V[static_cast<size_t>((v1 - 1) * 3 + 1)] - V[static_cast<size_t>(v0 * 3 + 1)];
  return std::hypot(dx, dy) <= tol;
}

struct SnapPickAccum {
  /// When set, candidates are measured against this world ray instead of the plan-view XY
  /// distance (REQ-058). The cursor ray crosses the work plane at one XY and elevated geometry at
  /// another, so a plan test measures to the wrong place and nothing within tolerance is found —
  /// which is why snapping appeared to stop working entirely once the view was orbited.
  const ray3d::Ray* ray = nullptr;
  Hit best{};
  /// True cursor-to-candidate-point distance (issue #103): the metric every kind is ranked by, so
  /// a candidate whose ACCEPTANCE test is a heuristic (e.g. a circle's distance-to-rim, so hovering
  /// anywhere on a large circle still offers its center) never wins purely because that heuristic
  /// reads small — only because its actual point is genuinely the nearest valid candidate.
  float bestRankDistSq = 0.f;
  int bestPri = -1;
};

/// \param snapZ elevation of the candidate point. Only consulted when \c acc->ray is set; the
///        plan-view path ignores Z exactly as it always has.
/// \param pickDistSq the ACCEPTANCE metric (tolerance test) — may be a heuristic distance (e.g. a
///        circle's distance-to-rim) that differs from the true distance to the returned point.
///        Ranking against other candidates always uses the true point distance, never this value,
///        so a kind with a generous heuristic acceptance radius cannot out-rank a kind that is
///        genuinely closer to the cursor (issue #103).
void ConsiderSnap(SnapPickAccum* acc, float wx, float wy, float snapX, float snapY, Kind kind, float pickDistSq,
                  float tolWorld, float snapZ = 0.f) {
  const float tol2 = tolWorld * tolWorld;
  float rankDistSq = (snapX - wx) * (snapX - wx) + (snapY - wy) * (snapY - wy);
  // Orbited: re-measure the candidate against the cursor ray in 3D. Doing it here — at the one
  // place every candidate funnels through — means each generator keeps its own 2D construction
  // logic and only the comparison changes. The ray distance to the actual point serves as both the
  // acceptance and ranking metric here — there is no separate heuristic once measured against the ray.
  if (acc->ray) {
    const double d = ray3d::RayPointDistance(
        *acc->ray, ray3d::Vec3{static_cast<double>(snapX), static_cast<double>(snapY), static_cast<double>(snapZ)});
    pickDistSq = static_cast<float>(d * d);
    rankDistSq = pickDistSq;
  }
  if (!(pickDistSq <= tol2) || pickDistSq > 1.e28f)
    return;
  const int pri = Priority(kind);
  const float eps = 1.e-9f * std::max(tol2, 1.f);
  if (!acc->best.valid) {
    acc->best.valid = true;
    acc->best.kind = kind;
    acc->best.x = snapX;
    acc->best.y = snapY;
    acc->best.z = snapZ;
    acc->bestRankDistSq = rankDistSq;
    acc->bestPri = pri;
    return;
  }
  if (rankDistSq < acc->bestRankDistSq - eps) {
    acc->best.kind = kind;
    acc->best.x = snapX;
    acc->best.y = snapY;
    acc->best.z = snapZ;
    acc->bestRankDistSq = rankDistSq;
    acc->bestPri = pri;
    return;
  }
  if (rankDistSq > acc->bestRankDistSq + eps)
    return;
  // True distance tie: fall back to the kind priority table.
  if (pri > acc->bestPri) {
    acc->best.kind = kind;
    acc->best.x = snapX;
    acc->best.y = snapY;
    acc->best.z = snapZ;
    acc->bestPri = pri;
  }
}

void Consider(SnapPickAccum* acc, float wx, float wy, float px, float py, Kind kind, float tolWorld,
              float pz = 0.f) {
  const float dx = px - wx;
  const float dy = py - wy;
  ConsiderSnap(acc, wx, wy, px, py, kind, dx * dx + dy * dy, tolWorld, pz);
}

/// Mean vertex elevation of polyline loop [\p v0,\p v1) — the elevation of its geometric centre.
///
/// A centroid has no single Z once the loop is non-planar, so it gets the same averaging the
/// centroid already is in X and Y (REQ-058). Exact for the planar case, which is every rectangle
/// and every pre-3D polyline.
[[nodiscard]] float PolylineLoopMeanZ(const std::vector<float>& verts, int v0, int v1) {
  if (v1 <= v0)
    return 0.f;
  double zSum = 0.;
  for (int vi = v0; vi < v1; ++vi)
    zSum += static_cast<double>(verts[static_cast<size_t>(vi * 3 + 2)]);
  return static_cast<float>(zSum / static_cast<double>(v1 - v0));
}

/// Foot of perpendicular from \p ref onto segment AB (clamped). Cursor \p wx,\p wy only gates distance.
///
/// \param az,bz elevations of A and B. The foot lies at parameter \p t along AB, so its elevation
///        is the same interpolation (REQ-058). Without it the candidate defaulted to Z 0 and was
///        measured against the cursor ray at the wrong depth — the snap was either missed entirely
///        or accepted and then committed on the datum instead of on the segment.
void AppendPerpendicularFromRef(float refX, float refY, float wx, float wy, float ax, float ay, float bx, float by,
                                float tolWorld, SnapPickAccum* acc, float az = 0.f, float bz = 0.f) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1.e-12f)
    return;
  float t = ((refX - ax) * vx + (refY - ay) * vy) / len2;
  t = std::clamp(t, 0.f, 1.f);
  const float qx = ax + t * vx;
  const float qy = ay + t * vy;
  Consider(acc, wx, wy, qx, qy, Kind::Perpendicular, tolWorld, az + t * (bz - az));
}

// --- Intersection snaps (REQ-062) ---------------------------------------------------------------
//
// Both INT and APPINT are pairwise, which makes them the only snaps whose cost is not linear in the
// drawing. They stay affordable because an intersection point lies ON both objects, so an object
// can only contribute if it passes within the snap aperture of the cursor — see GatherNearCursor,
// which reduces the pairwise work to the handful of objects actually under the pointer.

/// A line segment or polyline edge, with the elevation of each end (REQ-057).
struct IsectSeg {
  double x0 = 0.0, y0 = 0.0, z0 = 0.0;
  double x1 = 0.0, y1 = 0.0, z1 = 0.0;
  [[nodiscard]] curveisect::Seg xy() const { return curveisect::Seg{{x0, y0}, {x1, y1}}; }
  [[nodiscard]] double zAt(double t) const { return z0 + (z1 - z0) * t; }
};

/// A circle, arc or ellipse in a plane parallel to XY, so one elevation describes it.
///
/// REQ-312 lets an arc or circle lie in an arbitrary plane, and such a curve is NOT representable
/// here: a tilted circle projects to an ellipse whose intersection with another curve is a
/// different problem from the planar one this type solves. Tilted curves are therefore left out of
/// the candidate set by the collector below rather than flattened into it -- flattening would offer
/// an intersection point that is not on either curve, which is a wrong answer presented as a
/// correct one (REQ-201). True 3D curve-curve intersection belongs with the modelling kernel
/// (issue #146).
struct IsectConic {
  curveisect::Conic k;
  double z = 0.0;
};

/// Distance from the cursor to a point, measured the way ConsiderSnap will measure it: against the
/// 3D pick ray when the view is orbited, in plan XY otherwise.
[[nodiscard]] double CursorDistanceTo(const ray3d::Ray* ray, double wx, double wy, double px, double py, double pz) {
  if (ray)
    return ray3d::RayPointDistance(*ray, ray3d::Vec3{px, py, pz});
  return std::hypot(px - wx, py - wy);
}

/// Conservative near-cursor test for a whole object, from its bounding sphere.
///
/// An intersection lies on the object, so if the object never comes within \p tol of the cursor it
/// cannot produce an accepted candidate. By the triangle inequality
/// `dist(cursor, centre) ≤ dist(cursor, P) + radius`, so this rejects only objects that could not
/// have contributed — no true intersection is lost to the cull.
[[nodiscard]] bool NearCursor(const ray3d::Ray* ray, double wx, double wy, double cx, double cy, double cz,
                              double radius, double tol) {
  return CursorDistanceTo(ray, wx, wy, cx, cy, cz) <= tol + radius;
}

/// Collect the segments and conics close enough to the cursor to take part in an intersection.
void GatherNearCursor(const AppCommandState& cmd, double wx, double wy, double tol, const ray3d::Ray* ray,
                      std::vector<IsectSeg>* segs, std::vector<IsectConic>* conics) {
  const auto& L = cmd.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      IsectSeg s{L[i], L[i + 1], L[i + 2], L[i + 3], L[i + 4], L[i + 5]};
      const double mx = 0.5 * (s.x0 + s.x1);
      const double my = 0.5 * (s.y0 + s.y1);
      const double mz = 0.5 * (s.z0 + s.z1);
      const double r = 0.5 * std::sqrt((s.x1 - s.x0) * (s.x1 - s.x0) + (s.y1 - s.y0) * (s.y1 - s.y0) +
                                       (s.z1 - s.z0) * (s.z1 - s.z0));
      if (NearCursor(ray, wx, wy, mx, my, mz, r, tol))
        segs->push_back(s);
    }
  }

  const int polyCount =
      static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < polyCount; ++pi) {
    const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
    auto addEdge = [&](int ia, int ib) {
      const auto& V = cmd.userPolylineVerts;
      IsectSeg s{V[static_cast<size_t>(ia * 3)],     V[static_cast<size_t>(ia * 3 + 1)],
                 V[static_cast<size_t>(ia * 3 + 2)], V[static_cast<size_t>(ib * 3)],
                 V[static_cast<size_t>(ib * 3 + 1)], V[static_cast<size_t>(ib * 3 + 2)]};
      const double mx = 0.5 * (s.x0 + s.x1);
      const double my = 0.5 * (s.y0 + s.y1);
      const double mz = 0.5 * (s.z0 + s.z1);
      const double r = 0.5 * std::sqrt((s.x1 - s.x0) * (s.x1 - s.x0) + (s.y1 - s.y0) * (s.y1 - s.y0) +
                                       (s.z1 - s.z0) * (s.z1 - s.z0));
      if (NearCursor(ray, wx, wy, mx, my, mz, r, tol))
        segs->push_back(s);
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      addEdge(vi, vi + 1);
    if (closed && v1 - v0 >= 2)
      addEdge(v1 - 1, v0);
  }

  const auto& C = cmd.userCirclesCxCyZR;
  if (C.size() % 4 == 0) {
    for (size_t i = 0; i + 3 < C.size(); i += 4) {
      if (C[i + 3] <= 1.e-6f)
        continue;
      // A tilted circle (REQ-312) is not a planar-XY conic; see IsectConic. Skipped, so INTERSECTION
      // offers nothing on it rather than offering a point that lies on neither curve.
      if (!CircleIsFlat(cmd.userCircleNormals, i / 4))
        continue;
      if (NearCursor(ray, wx, wy, C[i], C[i + 1], C[i + 2], C[i + 3], tol))
        conics->push_back(IsectConic{curveisect::MakeCircle(C[i], C[i + 1], C[i + 3]), C[i + 2]});
    }
  }

  for (const CadArc& a : cmd.userArcs) {
    if (a.r <= 1.e-6f)
      continue;
    if (!IsFlatNormal(a.nx, a.ny, a.nz))  // same exclusion as tilted circles above
      continue;
    if (NearCursor(ray, wx, wy, a.cx, a.cy, a.z, a.r, tol))
      conics->push_back(IsectConic{curveisect::MakeArc(a.cx, a.cy, a.r, a.startRad, a.sweepRad), a.z});
  }

  for (const CadEllipse& el : cmd.userEllipses) {
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1.e-8)
      continue;
    if (NearCursor(ray, wx, wy, el.cx, el.cy, el.z, ma, tol))
      conics->push_back(IsectConic{curveisect::MakeEllipse(el.cx, el.cy, el.majVx, el.majVy, el.ratio), el.z});
  }
}

/// One resolved intersection point in world space.
struct IsectCandidate {
  double x = 0.0, y = 0.0, z = 0.0;
};

/// True 3-D intersections: the XY paths cross AND the elevations agree there within REQ-101.
///
/// The elevation test is what separates INT from APPINT. Two segments crossing in plan at different
/// elevations do not touch, and reporting a snap there would place geometry on nothing.
void ComputeTrueIntersections(const std::vector<IsectSeg>& segs, const std::vector<IsectConic>& conics,
                              std::vector<IsectCandidate>* out) {
  constexpr double kReq101 = 0.01;  ///< ±0.01 ft — the project coordinate tolerance.
  std::vector<curveisect::Hit2> hits;

  for (size_t i = 0; i < segs.size(); ++i) {
    for (size_t j = i + 1; j < segs.size(); ++j) {
      hits.clear();
      curveisect::IntersectSegSeg(segs[i].xy(), segs[j].xy(), &hits);
      for (const auto& h : hits) {
        const double za = segs[i].zAt(h.tA);
        const double zb = segs[j].zAt(h.tB);
        if (std::fabs(za - zb) > kReq101)
          continue;  // they cross in plan but miss in space — that is APPINT's business, not INT's
        out->push_back(IsectCandidate{h.p.x, h.p.y, 0.5 * (za + zb)});
      }
    }
  }

  for (const IsectSeg& s : segs) {
    for (const IsectConic& c : conics) {
      hits.clear();
      curveisect::IntersectSegConic(s.xy(), c.k, &hits);
      for (const auto& h : hits) {
        const double za = s.zAt(h.tA);
        if (std::fabs(za - c.z) > kReq101)
          continue;
        out->push_back(IsectCandidate{h.p.x, h.p.y, 0.5 * (za + c.z)});
      }
    }
  }

  for (size_t i = 0; i < conics.size(); ++i) {
    for (size_t j = i + 1; j < conics.size(); ++j) {
      if (std::fabs(conics[i].z - conics[j].z) > kReq101)
        continue;  // parallel planes at different heights never meet
      hits.clear();
      curveisect::IntersectConicConic(conics[i].k, conics[j].k, &hits);
      for (const auto& h : hits)
        out->push_back(IsectCandidate{h.p.x, h.p.y, 0.5 * (conics[i].z + conics[j].z)});
    }
  }
}

/// Apparent intersections: the objects cross **as projected into the view**, whether or not they
/// meet in space.
///
/// Everything is projected into the camera's right/up plane and intersected there. Two properties
/// make this work with no second implementation of the math:
///   - projection is linear, so a segment stays a segment and a conic stays a conic — an orbited
///     circle really does project to an ellipse, and `curveisect::Conic` represents that exactly;
///   - the projection preserves each shape's parametrization, so a parameter solved on screen reads
///     straight back as the world point via the *unprojected* shape.
/// Of the two candidate world points, the one NEARER THE CAMERA is returned — the object the user
/// is visually pointing at (REQ-062; AutoCAD uses "the first object picked", which we have no
/// equivalent of).
void ComputeApparentIntersections(const Camera& cam, const std::vector<IsectSeg>& segs,
                                  const std::vector<IsectConic>& conics, std::vector<IsectCandidate>* out) {
  const ray3d::Vec3 rW = cam.RightWorld();
  const ray3d::Vec3 uW = cam.UpWorld();
  const ray3d::Vec3 fW = cam.ForwardWorld();
  const double right[3] = {rW.x, rW.y, rW.z};
  const double up[3] = {uW.x, uW.y, uW.z};

  // Depth along the view direction; smaller is nearer the eye.
  const auto depth = [&](double x, double y, double z) { return x * fW.x + y * fW.y + z * fW.z; };
  const auto emitNearer = [&](double xa, double ya, double za, double xb, double yb, double zb) {
    const bool aNearer = depth(xa, ya, za) <= depth(xb, yb, zb);
    out->push_back(IsectCandidate{aNearer ? xa : xb, aNearer ? ya : yb, aNearer ? za : zb});
  };

  std::vector<curveisect::Hit2> hits;

  for (size_t i = 0; i < segs.size(); ++i) {
    const curveisect::Seg pi = curveisect::ProjectSeg(segs[i].x0, segs[i].y0, segs[i].z0, segs[i].x1, segs[i].y1,
                                                      segs[i].z1, right, up);
    for (size_t j = i + 1; j < segs.size(); ++j) {
      const curveisect::Seg pj = curveisect::ProjectSeg(segs[j].x0, segs[j].y0, segs[j].z0, segs[j].x1, segs[j].y1,
                                                        segs[j].z1, right, up);
      hits.clear();
      curveisect::IntersectSegSeg(pi, pj, &hits);
      for (const auto& h : hits) {
        const IsectSeg& A = segs[i];
        const IsectSeg& B = segs[j];
        emitNearer(A.x0 + (A.x1 - A.x0) * h.tA, A.y0 + (A.y1 - A.y0) * h.tA, A.zAt(h.tA),
                   B.x0 + (B.x1 - B.x0) * h.tB, B.y0 + (B.y1 - B.y0) * h.tB, B.zAt(h.tB));
      }
    }
  }

  for (const IsectSeg& s : segs) {
    const curveisect::Seg ps = curveisect::ProjectSeg(s.x0, s.y0, s.z0, s.x1, s.y1, s.z1, right, up);
    for (const IsectConic& c : conics) {
      const curveisect::Conic pc = curveisect::ProjectConic(c.k, c.z, right, up);
      hits.clear();
      curveisect::IntersectSegConic(ps, pc, &hits);
      for (const auto& h : hits) {
        const curveisect::Vec2 onCurve = c.k.point(h.tB);  // the UNPROJECTED conic, same parameter
        emitNearer(s.x0 + (s.x1 - s.x0) * h.tA, s.y0 + (s.y1 - s.y0) * h.tA, s.zAt(h.tA), onCurve.x, onCurve.y, c.z);
      }
    }
  }

  for (size_t i = 0; i < conics.size(); ++i) {
    const curveisect::Conic pi = curveisect::ProjectConic(conics[i].k, conics[i].z, right, up);
    for (size_t j = i + 1; j < conics.size(); ++j) {
      const curveisect::Conic pj = curveisect::ProjectConic(conics[j].k, conics[j].z, right, up);
      hits.clear();
      curveisect::IntersectConicConic(pi, pj, &hits);
      for (const auto& h : hits) {
        const curveisect::Vec2 pa = conics[i].k.point(h.tA);
        const curveisect::Vec2 pb = conics[j].k.point(h.tB);
        emitNearer(pa.x, pa.y, conics[i].z, pb.x, pb.y, conics[j].z);
      }
    }
  }
}

[[nodiscard]] bool PerpendicularReference(const AppCommandState& cmd, float* refX, float* refY) {
  using K = AppCommandState::Kind;
  using LP = AppCommandState::LinePhase;
  using CP = AppCommandState::CirclePhase;

  if (cmd.active == K::Line && cmd.linePhase == LP::NeedNextPoint) {
    *refX = cmd.anchorX;
    *refY = cmd.anchorY;
    return true;
  }
  if (cmd.active == K::Polyline && cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint) {
    *refX = cmd.anchorX;
    *refY = cmd.anchorY;
    return true;
  }
  if (cmd.active == K::Arc) {
    switch (cmd.arcPhase) {
    case AppCommandState::ArcPhase::WaitMid:
      *refX = cmd.arcAx;
      *refY = cmd.arcAy;
      return true;
    case AppCommandState::ArcPhase::WaitEnd:
      *refX = cmd.arcBx;
      *refY = cmd.arcBy;
      return true;
    default:
      break;
    }
  }
  if (cmd.active == K::Ellipse && cmd.ellPhase == AppCommandState::EllipsePhase::WaitMajorEnd) {
    *refX = cmd.ellCx;
    *refY = cmd.ellCy;
    return true;
  }
  if (cmd.active == K::Circle) {
    switch (cmd.circlePhase) {
    case CP::WaitRadius:
      *refX = cmd.circleCx;
      *refY = cmd.circleCy;
      return true;
    case CP::ThreeP_WaitP2:
      *refX = cmd.c3p1x;
      *refY = cmd.c3p1y;
      return true;
    case CP::ThreeP_WaitP3:
      *refX = cmd.c3p2x;
      *refY = cmd.c3p2y;
      return true;
    default:
      break;
    }
  }
  return false;
}


/// The point ON THE RAY nearest to \p e — a starting point for the projection back onto the edge.
///
/// A coarse scan and then one projection, rather than a closed form. The closed form exists for a
/// line but not for a general arc-against-a-ray, and a two-code-path version of this would be a
/// place for the two to disagree about which point is "nearest" — which shows up as a snap that
/// jumps between a straight edge and a curved one under the same cursor. 32 samples puts the coarse
/// answer within a hundredth of a turn, and \ref brep::ClosestPointOnEdge does the rest exactly.
ray3d::Vec3 ClosestRayPointToEdge(const ray3d::Ray& ray, const brep::Solid& s, const brep::Edge& e) {
  const int n = e.kind == brep::CurveKind::Line ? 1 : 32;
  double bestD = std::numeric_limits<double>::max();
  ray3d::Vec3 best = brep::EdgePointAt(s, e, 0.0);
  for (int i = 0; i <= n; ++i) {
    const ray3d::Vec3 p = brep::EdgePointAt(s, e, static_cast<double>(i) / static_cast<double>(n));
    const double d = ray3d::RayPointDistance(ray, p);
    if (d < bestD) {
      bestD = d;
      best = p;
    }
  }
  const double t = ray3d::Dot(ray3d::Sub(best, ray.origin), ray.dir);
  return ray.at(t > 0.0 ? t : 0.0);
}

/// Does \p ray come near \p b at all? A slab test, used to reject a whole solid before its triangles
/// are walked.
///
/// This is not a micro-optimisation. Object snapping runs on HOVER, every frame, and the triangle
/// walk below is O(triangles) per solid — a few hundred solids at a couple of thousand triangles
/// each is most of a million ray-triangle tests per frame, which is REQ-100's budget gone on a
/// cursor that is not near any of them. Four compares that discard a solid first is the difference.
///
/// The box is padded by \p pad so a ray passing just outside a solid still reaches the triangles: a
/// snap that silently misses is worse than a slow one, which is the same call the surface pick makes
/// about its own plan-AABB reject.
[[nodiscard]] bool RayNearBounds(const ray3d::Ray& ray, const brep::Bounds& b, double pad) {
  if (!b.valid)
    return false;
  const double mn[3] = {b.mn.x - pad, b.mn.y - pad, b.mn.z - pad};
  const double mx[3] = {b.mx.x + pad, b.mx.y + pad, b.mx.z + pad};
  const double o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
  const double d[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
  double tNear = -std::numeric_limits<double>::max();
  double tFar = std::numeric_limits<double>::max();
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(d[i]) < 1e-12) {
      if (o[i] < mn[i] || o[i] > mx[i])
        return false;  // parallel to this slab and outside it
      continue;
    }
    double t0 = (mn[i] - o[i]) / d[i];
    double t1 = (mx[i] - o[i]) / d[i];
    if (t0 > t1)
      std::swap(t0, t1);
    tNear = std::max(tNear, t0);
    tFar = std::min(tFar, t1);
    if (tNear > tFar)
      return false;
  }
  return tFar >= 0.0;
}

/// Nearest front-facing triangle hit along \p ray, over the flat 9-floats-per-triangle buffer the
/// solid display cache holds. Writes the hit point and the FACE the triangle belongs to.
///
/// Möller–Trumbore, two-sided: a solid can be viewed from inside (an orbit that puts the camera in
/// the middle of a box is ordinary), and a one-sided test would report nothing there rather than the
/// far wall. Nearest hit wins, which is what makes the snap land on the surface facing the user.
bool RayHitSolidFace(const ray3d::Ray& ray, const std::vector<float>& triVerts,
                     const std::vector<int>& triFaceIds, ray3d::Vec3* outHit, int* outFace) {
  const size_t triCount = triVerts.size() / 9;
  double bestT = std::numeric_limits<double>::max();
  bool found = false;
  for (size_t t = 0; t < triCount; ++t) {
    const size_t b = t * 9;
    const ray3d::Vec3 v0{triVerts[b], triVerts[b + 1], triVerts[b + 2]};
    const ray3d::Vec3 v1{triVerts[b + 3], triVerts[b + 4], triVerts[b + 5]};
    const ray3d::Vec3 v2{triVerts[b + 6], triVerts[b + 7], triVerts[b + 8]};
    const ray3d::Vec3 e1 = ray3d::Sub(v1, v0);
    const ray3d::Vec3 e2 = ray3d::Sub(v2, v0);
    const ray3d::Vec3 pv = ray3d::Cross(ray.dir, e2);
    const double det = ray3d::Dot(e1, pv);
    if (std::fabs(det) < 1e-12)
      continue;  // ray parallel to the triangle's plane
    const double invDet = 1.0 / det;
    const ray3d::Vec3 tv = ray3d::Sub(ray.origin, v0);
    const double u = ray3d::Dot(tv, pv) * invDet;
    if (u < 0.0 || u > 1.0)
      continue;
    const ray3d::Vec3 qv = ray3d::Cross(tv, e1);
    const double v = ray3d::Dot(ray.dir, qv) * invDet;
    if (v < 0.0 || u + v > 1.0)
      continue;
    const double hitT = ray3d::Dot(e2, qv) * invDet;
    if (hitT <= 0.0 || hitT >= bestT)
      continue;
    bestT = hitT;
    found = true;
    if (outHit)
      *outHit = ray.at(hitT);
    if (outFace)
      *outFace = t < triFaceIds.size() ? triFaceIds[t] : -1;
  }
  return found;
}
} // namespace

Hit FindBest(double wx, double wy, const AppCommandState& cmd, bool commandActive, float tolWorld,
             SnapExclude exclude, const ray3d::Ray* pickRay, const Kind* onlyKind) {
  SnapPickAccum acc{};
  // Null (plan view, paper space) leaves every candidate measured exactly as before.
  acc.ray = (pickRay && pickRay->valid()) ? pickRay : nullptr;

  // issue #103: with an override active, a kind is wanted purely because it IS the override — the
  // persistent per-type toggle is irrelevant (that toggle is exactly what the override exists to
  // bypass). With no override, behavior is unchanged: wanted iff the toggle says so.
  const auto want = [&](Kind k, bool toggle) { return onlyKind ? (*onlyKind == k) : toggle; };
  const bool wantEndpoint = want(Kind::Endpoint, cmd.objectSnapEndpoint);
  const bool wantMidpoint = want(Kind::Midpoint, cmd.objectSnapMidpoint);
  const bool wantCenter = want(Kind::Center, cmd.objectSnapCenter);
  const bool wantGeometricCenter = want(Kind::GeometricCenter, cmd.objectSnapGeometricCenter);
  const bool wantIntersection = want(Kind::Intersection, cmd.objectSnapIntersection);
  const bool wantApparentIntersection = want(Kind::ApparentIntersection, cmd.objectSnapApparentIntersection);
  const bool wantSurveyPoint = want(Kind::SurveyCenter, cmd.objectSnapSurveyPoint);
  const bool wantPerpendicular = want(Kind::Perpendicular, cmd.objectSnapPerpendicular);
  const bool wantSurface = want(Kind::Surface, cmd.objectSnapSurface);

  float refPx = 0.f;
  float refPy = 0.f;
  const bool havePerpRef = commandActive && wantPerpendicular && PerpendicularReference(cmd, &refPx, &refPy);

  // REQ-118: while a POLYLINE/3DPOLY draft is open, its STARTING vertex is an Endpoint candidate,
  // so the cursor can land on it exactly and the ordinary snap marker shows it. This is the only
  // candidate here that comes from uncommitted geometry — the draft is in no store yet.
  //
  // Offered only from three vertices on. REQ-118 keeps the existing minimum (three to close) and
  // expresses it by WITHHOLDING the affordance rather than refusing the close afterwards: a snap
  // the user is never offered cannot be mis-clicked, so "invalid attempts handled gracefully" needs
  // no message. Below that, the start point is still an ordinary place to put a vertex.
  //
  // This does NOT decide the close. SubmitPolylineVertex compares the committed point against the
  // stored first vertex; the snap only makes that point reachable (D-2026-08-25-j).
  if (cmd.active == AppCommandState::Kind::Polyline && wantEndpoint &&
      cmd.polylineDraftVerts.size() >= 9) {
    Consider(&acc, wx, wy, cmd.polylineDraftVerts[0], cmd.polylineDraftVerts[1], Kind::Endpoint,
             tolWorld, cmd.polylineDraftVerts[2]);
  }

  const auto& L = cmd.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      if (exclude.valid && exclude.type == SelectedEntity::Type::LineSeg &&
          exclude.index == static_cast<int>(i / 6)) continue;
      const float x0 = L[i];
      const float y0 = L[i + 1];
      const float x1 = L[i + 3];
      const float y1 = L[i + 4];
      const float z0 = L[i + 2];
      const float z1 = L[i + 5];
      if (wantEndpoint) {
        Consider(&acc, wx, wy, x0, y0, Kind::Endpoint, tolWorld, z0);
        Consider(&acc, wx, wy, x1, y1, Kind::Endpoint, tolWorld, z1);
      }
      if (wantMidpoint)
        Consider(&acc, wx, wy, 0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, tolWorld, 0.5f * (z0 + z1));
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, x0, y0, x1, y1, tolWorld, &acc, z0, z1);
    }
  }

  const auto& C = cmd.userCirclesCxCyZR;
  if (C.size() % 4 == 0 && wantCenter) {  // cx,cy,z,r
    for (size_t i = 0; i + 3 < C.size(); i += 4) {
      if (exclude.valid && exclude.type == SelectedEntity::Type::Circle &&
          exclude.index == static_cast<int>(i / 4)) continue;
      const float cx = C[i];
      const float cy = C[i + 1];
      const float r = C[i + 3];
      const float p2 = CircleCenterPickDistSq(wx, wy, cx, cy, r, tolWorld);
      ConsiderSnap(&acc, wx, wy, cx, cy, Kind::Center, p2, tolWorld, C[i + 2]);
    }
  }

  const int polyCount =
      static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < polyCount; ++pi) {
    if (exclude.valid && exclude.type == SelectedEntity::Type::Polyline && exclude.index == pi) continue;
    const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
    auto considerEdge = [&](int ia, int ib) {
      const float ax = cmd.userPolylineVerts[static_cast<size_t>(ia * 3)];
      const float ay = cmd.userPolylineVerts[static_cast<size_t>(ia * 3 + 1)];
      const float bx = cmd.userPolylineVerts[static_cast<size_t>(ib * 3)];
      const float by = cmd.userPolylineVerts[static_cast<size_t>(ib * 3 + 1)];
      const float az = cmd.userPolylineVerts[static_cast<size_t>(ia * 3 + 2)];
      const float bz = cmd.userPolylineVerts[static_cast<size_t>(ib * 3 + 2)];
      if (wantEndpoint) {
        Consider(&acc, wx, wy, ax, ay, Kind::Endpoint, tolWorld, az);
        Consider(&acc, wx, wy, bx, by, Kind::Endpoint, tolWorld, bz);
      }
      if (wantMidpoint)
        Consider(&acc, wx, wy, 0.5f * (ax + bx), 0.5f * (ay + by), Kind::Midpoint, tolWorld, 0.5f * (az + bz));
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, ax, ay, bx, by, tolWorld, &acc, az, bz);
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      considerEdge(vi, vi + 1);
    if (closed && v1 - v0 >= 2)
      considerEdge(v1 - 1, v0);

    if (wantGeometricCenter && PolylineBoundsArea(cmd, pi, v0, v1)) {
      float gcx = 0.f;
      float gcy = 0.f;
      if (ClosedPolylineCentroid(cmd.userPolylineVerts, v0, v1, &gcx, &gcy)) {
        const float p2 = ClosedPolyGeometricPickDistSq(wx, wy, cmd.userPolylineVerts, v0, v1);
        ConsiderSnap(&acc, wx, wy, gcx, gcy, Kind::GeometricCenter, p2, tolWorld,
                     PolylineLoopMeanZ(cmd.userPolylineVerts, v0, v1));
      }
    }
  }

  // REQ-303 (issue #80): while POLYLINE/3DPOLY is actively drawing (they share one state machine —
  // polylineDraft3d only changes the label and Z handling), its own start point is offered as an
  // ordinary Endpoint snap, so the "click to close" cue is the same glyph/priority/tolerance as any
  // other endpoint rather than a separate mechanism. SubmitViewportPickImpl reads this back by exact
  // equality against polyFirstX/Y, which is safe because Consider() below copies px/py through with
  // no arithmetic.
  if (commandActive && wantEndpoint && cmd.active == AppCommandState::Kind::Polyline &&
      cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint &&
      cmd.polylineDraftVerts.size() >= 3) {
    Consider(&acc, wx, wy, cmd.polyFirstX, cmd.polyFirstY, Kind::Endpoint, tolWorld,
             cmd.polylineDraftVerts[2]);
  }

  constexpr int kArcSnapSeg = 24;
  for (size_t arcIdx = 0; arcIdx < cmd.userArcs.size(); ++arcIdx) {
    if (exclude.valid && exclude.type == SelectedEntity::Type::Arc &&
        exclude.index == static_cast<int>(arcIdx)) continue;
    const CadArc& a = cmd.userArcs[arcIdx];
    if (a.r <= 1e-6f || kArcSnapSeg < 1)
      continue;
    ucs::Ucs arcPlane;
    const bool arcFlat = ArcSnapPlane(a, &arcPlane);
    const double tEnd = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad);
    if (wantEndpoint) {
      float ex = 0.f;
      float ey = 0.f;
      float ez = 0.f;
      ArcSnapPoint(a, arcPlane, arcFlat, static_cast<double>(a.startRad), &ex, &ey, &ez);
      Consider(&acc, wx, wy, ex, ey, Kind::Endpoint, tolWorld, ez);
      ArcSnapPoint(a, arcPlane, arcFlat, tEnd, &ex, &ey, &ez);
      Consider(&acc, wx, wy, ex, ey, Kind::Endpoint, tolWorld, ez);
    }
    for (int i = 0; i < kArcSnapSeg; ++i) {
      const double t0 = CurveSampleAngle(static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), i,
                                         kArcSnapSeg);
      const double t1 = CurveSampleAngle(static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), i + 1,
                                         kArcSnapSeg);
      float x0 = 0.f;
      float y0 = 0.f;
      float z0 = 0.f;
      float x1 = 0.f;
      float y1 = 0.f;
      float z1 = 0.f;
      ArcSnapPoint(a, arcPlane, arcFlat, t0, &x0, &y0, &z0);
      ArcSnapPoint(a, arcPlane, arcFlat, t1, &x1, &y1, &z1);
      if (wantMidpoint)
        // The chord's own midpoint, elevation included. On a flat arc both ends share a.z and this
        // is the previous value exactly; on a tilted one the two ends genuinely differ in Z.
        Consider(&acc, wx, wy, 0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, tolWorld,
                 0.5f * (z0 + z1));
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, x0, y0, x1, y1, tolWorld, &acc, z0, z1);
    }
  }

  constexpr int kEllSnapSeg = 36;
  constexpr float kTwoPi = 6.28318530718f;
  for (size_t ellIdx = 0; ellIdx < cmd.userEllipses.size(); ++ellIdx) {
    if (exclude.valid && exclude.type == SelectedEntity::Type::Ellipse &&
        exclude.index == static_cast<int>(ellIdx)) continue;
    const CadEllipse& el = cmd.userEllipses[ellIdx];
    const float ma = std::hypot(el.majVx, el.majVy);
    if (ma < 1e-8f || kEllSnapSeg < 3)
      continue;
    if (wantCenter) {
      const float p2 = EllipseCenterPickDistSq(wx, wy, el, tolWorld);
      ConsiderSnap(&acc, wx, wy, el.cx, el.cy, Kind::Center, p2, tolWorld, el.z);
    }
    const float ux = el.majVx / ma;
    const float uy = el.majVy / ma;
    const float px = -uy;
    const float py = ux;
    const float mb = ma * el.ratio;
    for (int i = 0; i < kEllSnapSeg; ++i) {
      const float ang0 = kTwoPi * static_cast<float>(i) / static_cast<float>(kEllSnapSeg);
      const float ang1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(kEllSnapSeg);
      const float c0 = std::cos(ang0);
      const float s0 = std::sin(ang0);
      const float c1 = std::cos(ang1);
      const float s1 = std::sin(ang1);
      const float x0 = el.cx + ux * (ma * c0) + px * (mb * s0);
      const float y0 = el.cy + uy * (ma * c0) + py * (mb * s0);
      const float x1 = el.cx + ux * (ma * c1) + px * (mb * s1);
      const float y1 = el.cy + uy * (ma * c1) + py * (mb * s1);
      if (wantMidpoint)
        Consider(&acc, wx, wy, 0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, tolWorld,
                 el.z);  // the ellipse's plane, same as its centre candidate above
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, x0, y0, x1, y1, tolWorld, &acc, el.z, el.z);
    }
  }

  // Intersection / apparent intersection (REQ-062). Gathered once and shared: both walk the same
  // near-cursor object list, and the cull is the only thing that keeps a pairwise snap affordable.
  if (wantIntersection || wantApparentIntersection) {
    std::vector<IsectSeg> isectSegs;
    std::vector<IsectConic> isectConics;
    GatherNearCursor(cmd, wx, wy, static_cast<double>(tolWorld), acc.ray, &isectSegs, &isectConics);
    if (isectSegs.size() + isectConics.size() >= 2) {
      std::vector<IsectCandidate> cand;
      if (wantIntersection) {
        ComputeTrueIntersections(isectSegs, isectConics, &cand);
        for (const IsectCandidate& c : cand)
          Consider(&acc, static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(c.x),
                   static_cast<float>(c.y), Kind::Intersection, tolWorld, static_cast<float>(c.z));
      }
      if (wantApparentIntersection) {
        cand.clear();
        ComputeApparentIntersections(CadViewCamera(cmd), isectSegs, isectConics, &cand);
        for (const IsectCandidate& c : cand)
          Consider(&acc, static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(c.x),
                   static_cast<float>(c.y), Kind::ApparentIntersection, tolWorld, static_cast<float>(c.z));
      }
    }
  }

  if (wantSurveyPoint) {
    const float arm =
        SurveyPointCrossHalfWorldFromPaper(cmd.surveyPointCrossSpanPlottedInches, cmd.modelUnitsPerPlottedInch);
    for (const SurveyPoint& sp : cmd.surveyPoints) {
      const float p2 = MinDistSqToSurveyMarker(wx, wy, sp.easting, sp.northing, arm);
      ConsiderSnap(&acc, wx, wy, sp.easting, sp.northing, Kind::SurveyCenter, p2, tolWorld, sp.elevation);  // elevation IS the point's Z (REQ-057)
    }
  }

  // --- Placed block instances (REQ-107, D-2026-08-29-i) ---
  // Expand each INSERT to world geometry and offer Endpoint on its segment ends and its insertion
  // point, Midpoint on its segment midpoints, and Center on its circles/arcs/ellipses — same
  // Consider/ConsiderSnap path, tolerance and ranking as native entities. The ghost being inserted
  // lives only in the rubber preview, never in cadBlockRefs, so it cannot snap to itself.
  if (wantEndpoint || wantMidpoint || wantCenter) {
    std::vector<CadBlockWorldSeg> bsegs;
    std::vector<CadBlockWorldPoint> bctr;
    for (const CadBlockRef& br : cmd.cadBlockRefs) {
      if (wantEndpoint)
        Consider(&acc, wx, wy, br.xf.x, br.xf.y, Kind::Endpoint, tolWorld, br.xf.z);
      if (wantEndpoint || wantMidpoint) {
        bsegs.clear();
        CadBlockCollectWorldLines(cmd.blockDefs, br, EntityAttributes{}, &bsegs);
        for (const CadBlockWorldSeg& s : bsegs) {
          if (wantEndpoint) {
            Consider(&acc, wx, wy, s.x0, s.y0, Kind::Endpoint, tolWorld, s.z0);
            Consider(&acc, wx, wy, s.x1, s.y1, Kind::Endpoint, tolWorld, s.z1);
          }
          if (wantMidpoint)
            Consider(&acc, wx, wy, 0.5f * (s.x0 + s.x1), 0.5f * (s.y0 + s.y1), Kind::Midpoint, tolWorld,
                     0.5f * (s.z0 + s.z1));
        }
      }
      if (wantCenter) {
        bctr.clear();
        CadBlockCollectWorldCenters(cmd.blockDefs, br, &bctr);
        for (const CadBlockWorldPoint& p : bctr)
          Consider(&acc, wx, wy, p.x, p.y, Kind::Center, tolWorld, p.z);
      }
    }
  }

  // --- B-rep solid snaps (REQ-313 / ADR-045) ------------------------------------------------------
  //
  // Four kinds come off a solid, and which toggle governs which is a deliberate split:
  //
  //   Endpoint  its VERTICES     — the ordinary Endpoint toggle. A box corner IS an endpoint, and a
  //                                user with Endpoint on expects a corner to snap.
  //   Midpoint  its EDGE MIDDLES — the ordinary Midpoint toggle, same argument.
  //   Edge      any point ALONG an edge  ) both behind `objectSnapSolid`, the one preference that
  //   Face      any point ON a face      ) means "snap to solids" (REQ-301: not two options).
  //
  // The FACE answer is what needs care. The ray is tested against the cached triangles to decide
  // which face is under the cursor, and the hit is then projected onto that face's ANALYTIC surface
  // — so on a cylinder the point comes back on the cylinder rather than a sagitta short of it, on
  // the chord the tessellator happened to draw (#120: "the resulting point should lie exactly on the
  // selected face"). Without that projection a face snap would be quietly wrong by an amount that
  // shrinks as you zoom in, which is the least reportable kind of wrong there is.
  //
  // Face snapping needs a pick ray and is skipped without one: in a plan view with no ray there is
  // no "under the cursor" to resolve, and answering with the work-plane point would be an invention.
  if (!cmd.cadSolids.empty()) {
    const bool wantSolidEdge = want(Kind::Edge, cmd.objectSnapSolid);
    const bool wantSolidFace = want(Kind::Face, cmd.objectSnapSolid);
    const ray3d::Vec3 cursor{wx, wy, acc.ray ? acc.ray->origin.z : 0.0};

    for (size_t si = 0; si < cmd.cadSolids.size(); ++si) {
      if (!SolidVisible(cmd, si))
        continue;  // layer off/frozen or isolated out — invisible and unclickable must not disagree
      if (exclude.valid && exclude.type == SelectedEntity::Type::Solid &&
          exclude.index == static_cast<int>(si))
        continue;
      const CadSolidPtr& sp = cmd.cadSolids[si];
      if (!sp)
        continue;

      if (wantEndpoint || wantMidpoint || wantSolidEdge) {
        if (wantEndpoint) {
          for (const brep::Vertex& v : sp->vertices)
            Consider(&acc, static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(v.p.x),
                     static_cast<float>(v.p.y), Kind::Endpoint, tolWorld, static_cast<float>(v.p.z));
        }
        for (const brep::Edge& e : sp->edges) {
          if (wantMidpoint) {
            const ray3d::Vec3 mid = brep::EdgePointAt(*sp, e, 0.5);
            Consider(&acc, static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(mid.x),
                     static_cast<float>(mid.y), Kind::Midpoint, tolWorld, static_cast<float>(mid.z));
          }
          if (wantSolidEdge) {
            // Measured from the cursor RAY where there is one, so an orbited view snaps to the edge
            // the user is pointing at rather than to whatever passes under the same plan XY.
            //
            // With no ray — a plan view — the probe is the cursor at the datum, and the answer is
            // still a point ON the edge; `ConsiderSnap` then ranks it by plan XY distance, which is
            // what plan view does for every other kind. For the horizontal and vertical edges every
            // primitive is mostly made of, that lands on the same point a proper 2D projection
            // would; on a slanted edge it can favour the lower end, which is a bias in WHICH point
            // of the edge is offered, never in whether the point is on it.
            const ray3d::Vec3 probe = acc.ray ? ClosestRayPointToEdge(*acc.ray, *sp, e) : cursor;
            const ray3d::Vec3 on = brep::ClosestPointOnEdge(*sp, e, probe);
            Consider(&acc, static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(on.x),
                     static_cast<float>(on.y), Kind::Edge, tolWorld, static_cast<float>(on.z));
          }
        }
      }

      if (wantSolidFace && acc.ray) {
        // Reject the whole solid before touching its triangles — see RayNearBounds. Hover runs this
        // every frame, and the walk below is O(triangles).
        if (!RayNearBounds(*acc.ray, brep::ComputeBounds(*sp), static_cast<double>(tolWorld)))
          continue;
        const auto it = std::find_if(cmd.solidDisplayCache.begin(), cmd.solidDisplayCache.end(),
                                     [&](const CadSolidTessellation& e) { return e.key.lock() == sp; });
        if (it == cmd.solidDisplayCache.end() || it->triVerts.empty())
          continue;  // not tessellated yet; it becomes snappable on the frame after it is drawn
        ray3d::Vec3 hit{};
        int faceIndex = -1;
        if (RayHitSolidFace(*acc.ray, it->triVerts, it->triFaceIds, &hit, &faceIndex) && faceIndex >= 0 &&
            static_cast<size_t>(faceIndex) < sp->faces.size()) {
          // The triangle told us WHICH face; the surface tells us WHERE on it.
          const ray3d::Vec3 exact = brep::ClosestPointOnSurface(sp->faces[static_cast<size_t>(faceIndex)].surface, hit);
          Consider(&acc, static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(exact.x),
                   static_cast<float>(exact.y), Kind::Face, tolWorld, static_cast<float>(exact.z));
        }
      }
    }
  }

  if (wantSurface) {
    float z = 0.f;
    if (SurfaceSnapElevation(cmd, wx, wy, &z))
      Consider(&acc, static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wx),
               static_cast<float>(wy), Kind::Surface, tolWorld, z);
  }

  // --- PDF underlay snap points ---
  if (cmd.objectSnapEnabled) {
    for (const PdfAttachment& att : cmd.pdfAttachments) {
      const float cosR = std::cos(att.rotationDeg * 3.14159265f / 180.f);
      const float sinR = std::sin(att.rotationDeg * 3.14159265f / 180.f);
      const float sc   = att.scale;

      // Transform a PDF-space point to local (drawing / world) space.
      auto pdfToLocal = [&](float px, float py, float* lx, float* ly) {
        *lx = att.insertX + (px * sc * cosR - py * sc * sinR);
        *ly = att.insertY + (px * sc * sinR + py * sc * cosR);
      };

      // Inverse: transform world cursor to PDF-page space for spatial queries.
      // PDF rotation is counter-clockwise by att.rotationDeg.
      // Inverse transform: rotate by -rotationDeg, then divide by scale.
      const float dxW       = wx - att.insertX;
      const float dyW       = wy - att.insertY;
      const float pdfCurX   = ( dxW * cosR + dyW * sinR) / sc;
      const float pdfCurY   = (-dxW * sinR + dyW * cosR) / sc;
      const float pdfTol    = (sc > 1e-9f) ? tolWorld / sc : 0.f;

      // ---- Endpoint snap via spatial grid (O(cells-near-cursor)) ----------
      if (att.snapLines && wantEndpoint) {
        // Visibility-mask check: returns true if pdfX,pdfY is inside or
        // adjacent to a cell marked as having visible foreground content.
        // This filters out snap endpoints that land in visually blank areas
        // (construction lines extending past viewport, etc.).
        const bool hasMask = !att.snapVisMask.empty();
        constexpr int MW = PdfAttachment::kVisMaskW;
        constexpr int MH = PdfAttachment::kVisMaskH;
        auto visOk = [&](float pdfX, float pdfY) -> bool {
          if (!hasMask || att.pageWidthPts <= 0.f || att.pageHeightPts <= 0.f)
            return true;
          // Map PDF coords (y-up, origin BL) to mask coords (row 0 = top of page).
          // The endpoint coordinate and the line pixels that pass through it map to
          // the same cell by construction, so an exact single-cell check is correct.
          // A 3x3 neighbourhood is too permissive on dense drawings (13 pt cells → every
          // "empty" area on a busy engineering plan is within one cell of some drawn line).
          const int cx = std::clamp(static_cast<int>(pdfX / att.pageWidthPts  * MW), 0, MW - 1);
          const int cy = std::clamp(static_cast<int>((1.f - pdfY / att.pageHeightPts) * MH), 0, MH - 1);
          return att.snapVisMask[static_cast<size_t>(cy) * MW + cx] != 0;
        };

        const auto& grid = att.snapEndptGrid;
        if (grid.cols > 0 && !grid.pts.empty()) {
          // Enumerate all cells that overlap [cursor ± pdfTol].
          const int col0 = std::max(0, static_cast<int>(
              (pdfCurX - pdfTol - grid.originX) / grid.cellW));
          const int col1 = std::min(grid.cols - 1, static_cast<int>(
              (pdfCurX + pdfTol - grid.originX) / grid.cellW));
          const int row0 = std::max(0, static_cast<int>(
              (pdfCurY - pdfTol - grid.originY) / grid.cellH));
          const int row1 = std::min(grid.rows - 1, static_cast<int>(
              (pdfCurY + pdfTol - grid.originY) / grid.cellH));
          for (int gr = row0; gr <= row1; ++gr) {
            for (int gc = col0; gc <= col1; ++gc) {
              const size_t cellIdx = static_cast<size_t>(gr * grid.cols + gc);
              const uint32_t start = grid.offsets[cellIdx];
              const uint32_t end   = grid.offsets[cellIdx + 1];
              for (uint32_t k = start; k < end; ++k) {
                const float epdfX = grid.pts[static_cast<size_t>(k) * 2];
                const float epdfY = grid.pts[static_cast<size_t>(k) * 2 + 1];
                if (!visOk(epdfX, epdfY)) continue; // blank area → skip
                float lx = 0.f, ly = 0.f;
                pdfToLocal(epdfX, epdfY, &lx, &ly);
                Consider(&acc, wx, wy, lx, ly, Kind::Endpoint, tolWorld);
              }
            }
          }
        } else {
          // Fallback: linear scan for attachments built without the grid
          // (e.g. loaded from an older saved file or extremely small drawings).
          const auto& SL = att.snapLinesFlat;
          for (size_t i = 0; i + 3 < SL.size(); i += 4) {
            float lx0 = 0.f, ly0 = 0.f, lx1 = 0.f, ly1 = 0.f;
            pdfToLocal(SL[i],     SL[i + 1], &lx0, &ly0);
            pdfToLocal(SL[i + 2], SL[i + 3], &lx1, &ly1);
            Consider(&acc, wx, wy, lx0, ly0, Kind::Endpoint, tolWorld);
            Consider(&acc, wx, wy, lx1, ly1, Kind::Endpoint, tolWorld);
          }
        }
      }

      // ---- Midpoint snap (snapLinesFlat with PDF-space bbox pre-filter) ----
      if (att.snapLines && wantMidpoint) {
        const auto& SL = att.snapLinesFlat;
        for (size_t i = 0; i + 3 < SL.size(); i += 4) {
          // Cheap PDF-space bbox reject — avoids pdfToLocal for distant lines.
          const float bxMin = std::min(SL[i], SL[i + 2]);
          const float bxMax = std::max(SL[i], SL[i + 2]);
          const float byMin = std::min(SL[i + 1], SL[i + 3]);
          const float byMax = std::max(SL[i + 1], SL[i + 3]);
          if (pdfCurX < bxMin - pdfTol || pdfCurX > bxMax + pdfTol ||
              pdfCurY < byMin - pdfTol || pdfCurY > byMax + pdfTol)
            continue;
          float lx0 = 0.f, ly0 = 0.f, lx1 = 0.f, ly1 = 0.f;
          pdfToLocal(SL[i],     SL[i + 1], &lx0, &ly0);
          pdfToLocal(SL[i + 2], SL[i + 3], &lx1, &ly1);
          Consider(&acc, wx, wy, 0.5f * (lx0 + lx1), 0.5f * (ly0 + ly1),
                   Kind::Midpoint, tolWorld);
        }
      }

      // ---- Perpendicular snap (same pre-filter approach) -------------------
      if (att.snapLines && havePerpRef) {
        const auto& SL = att.snapLinesFlat;
        for (size_t i = 0; i + 3 < SL.size(); i += 4) {
          const float bxMin = std::min(SL[i], SL[i + 2]);
          const float bxMax = std::max(SL[i], SL[i + 2]);
          const float byMin = std::min(SL[i + 1], SL[i + 3]);
          const float byMax = std::max(SL[i + 1], SL[i + 3]);
          if (pdfCurX < bxMin - pdfTol || pdfCurX > bxMax + pdfTol ||
              pdfCurY < byMin - pdfTol || pdfCurY > byMax + pdfTol)
            continue;
          float lx0 = 0.f, ly0 = 0.f, lx1 = 0.f, ly1 = 0.f;
          pdfToLocal(SL[i],     SL[i + 1], &lx0, &ly0);
          pdfToLocal(SL[i + 2], SL[i + 3], &lx1, &ly1);
          AppendPerpendicularFromRef(refPx, refPy, wx, wy, lx0, ly0, lx1, ly1, tolWorld, &acc);
        }
      }

      // ---- Circle center snap ---------------------------------------------
      if (att.snapCircles && wantCenter) {
        const auto& SC = att.snapCirclesCxCyR;
        for (size_t i = 0; i + 2 < SC.size(); i += 3) {
          float lcx = 0.f, lcy = 0.f;
          pdfToLocal(SC[i], SC[i + 1], &lcx, &lcy);
          const float lr = SC[i + 2] * sc;
          const float p2 = CircleCenterPickDistSq(wx, wy, lcx, lcy, lr, tolWorld);
          ConsiderSnap(&acc, wx, wy, lcx, lcy, Kind::Center, p2, tolWorld);
        }
      }

      // ---- Text insertion-point snap --------------------------------------
      // NOTE: Text positions are intentionally NOT emitted here.
      // Firing them as Kind::Endpoint causes the snap to trigger at text-label
      // baselines scattered across the PDF (room numbers, dimensions, callouts),
      // which appear as "snap on nothing" to the user.  Text snap positions
      // are stored so the feature can be surfaced later via a dedicated
      // Insert/Node snap type without polluting general endpoint snap.
    }
  }

  return acc.best;
}

bool CommandHasPerpendicularSnapReference(const AppCommandState& cmd, bool commandActive, bool ignoreToggle) {
  if (!commandActive || (!ignoreToggle && !cmd.objectSnapPerpendicular))
    return false;
  float rx = 0.f;
  float ry = 0.f;
  return PerpendicularReference(cmd, &rx, &ry);
}

/// \param pz the candidate's elevation (REQ-058). The picker hands its chosen entry straight back
///        as the snap result, so an entry built without a Z commits on the datum however carefully
///        FindBest resolves elevation for the same point.
void PushSnapPickerEntry(float px, float py, Kind kind, float sortWx, float sortWy,
                         std::vector<SnapCandidateEntry>& out, float pz = 0.f) {
  SnapCandidateEntry e;
  e.hit.valid = true;
  e.hit.kind = kind;
  e.hit.x = px;
  e.hit.y = py;
  e.hit.z = pz;
  const float dx = px - sortWx;
  const float dy = py - sortWy;
  e.distSq = dx * dx + dy * dy;
  out.push_back(e);
}

void PushPerpFootEntry(float refX, float refY, float ax, float ay, float bx, float by, float sortWx, float sortWy,
                       std::vector<SnapCandidateEntry>& out, float az = 0.f, float bz = 0.f) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1.e-12f)
    return;
  float t = ((refX - ax) * vx + (refY - ay) * vy) / len2;
  t = std::clamp(t, 0.f, 1.f);
  const float qx = ax + t * vx;
  const float qy = ay + t * vy;
  PushSnapPickerEntry(qx, qy, Kind::Perpendicular, sortWx, sortWy, out, az + t * (bz - az));
}

void SortDedupeSnapPicker(std::vector<SnapCandidateEntry>& v) {
  std::sort(v.begin(), v.end(),
            [](const SnapCandidateEntry& a, const SnapCandidateEntry& b) { return a.distSq < b.distSq; });
  std::vector<SnapCandidateEntry> u;
  u.reserve(v.size());
  for (const auto& c : v) {
    bool dup = false;
    for (const auto& p : u) {
      if (p.hit.kind == c.hit.kind && std::fabs(p.hit.x - c.hit.x) < 1e-5f &&
          std::fabs(p.hit.y - c.hit.y) < 1e-5f) {
        dup = true;
        break;
      }
    }
    if (!dup)
      u.push_back(c);
  }
  v.swap(u);
}

void GatherAllSnapsOfKind(Kind kind, float sortWorldX, float sortWorldY, const AppCommandState& cmd,
                          bool commandActive, std::vector<SnapCandidateEntry>& out) {
  out.clear();
  float refPx = 0.f;
  float refPy = 0.f;
  const bool havePerpRef = commandActive && cmd.objectSnapPerpendicular && PerpendicularReference(cmd, &refPx, &refPy);

  switch (kind) {
  case Kind::Endpoint: {
    const auto& L = cmd.userLinesFlat;
    if (L.size() % 6 == 0) {
      for (size_t i = 0; i + 5 < L.size(); i += 6) {
        PushSnapPickerEntry(L[i], L[i + 1], Kind::Endpoint, sortWorldX, sortWorldY, out, L[i + 2]);
        PushSnapPickerEntry(L[i + 3], L[i + 4], Kind::Endpoint, sortWorldX, sortWorldY, out, L[i + 5]);
      }
    }
    for (const CadBlockRef& br : cmd.cadBlockRefs) {
      PushSnapPickerEntry(br.xf.x, br.xf.y, Kind::Endpoint, sortWorldX, sortWorldY, out, br.xf.z);
      std::vector<CadBlockWorldSeg> segs;
      CadBlockCollectWorldLines(cmd.blockDefs, br, EntityAttributes{}, &segs);
      for (const CadBlockWorldSeg& s : segs) {
        PushSnapPickerEntry(s.x0, s.y0, Kind::Endpoint, sortWorldX, sortWorldY, out, s.z0);
        PushSnapPickerEntry(s.x1, s.y1, Kind::Endpoint, sortWorldX, sortWorldY, out, s.z1);
      }
    }
    const int polyCount =
        static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
    for (int pi = 0; pi < polyCount; ++pi) {
      const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const bool closed =
          static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
      auto pushEdge = [&](int ia, int ib) {
        const float ax = cmd.userPolylineVerts[static_cast<size_t>(ia * 3)];
        const float ay = cmd.userPolylineVerts[static_cast<size_t>(ia * 3 + 1)];
        const float bx = cmd.userPolylineVerts[static_cast<size_t>(ib * 3)];
        const float by = cmd.userPolylineVerts[static_cast<size_t>(ib * 3 + 1)];
        PushSnapPickerEntry(ax, ay, Kind::Endpoint, sortWorldX, sortWorldY, out,
                            cmd.userPolylineVerts[static_cast<size_t>(ia * 3 + 2)]);
        PushSnapPickerEntry(bx, by, Kind::Endpoint, sortWorldX, sortWorldY, out,
                            cmd.userPolylineVerts[static_cast<size_t>(ib * 3 + 2)]);
      };
      for (int vi = v0; vi + 1 < v1; ++vi)
        pushEdge(vi, vi + 1);
      if (closed && v1 - v0 >= 2)
        pushEdge(v1 - 1, v0);
    }
    for (const CadArc& a : cmd.userArcs) {
      if (a.r <= 1e-6f)
        continue;
      // Through the arc's own plane (REQ-312), and through the same walk the live snap uses. This
      // list and the live accumulator previously computed the same two endpoints in different
      // precisions, which is the shape of defect the shared parametrisation exists to remove.
      ucs::Ucs arcPlane;
      const bool arcFlat = ArcSnapPlane(a, &arcPlane);
      float ex = 0.f;
      float ey = 0.f;
      float ez = 0.f;
      ArcSnapPoint(a, arcPlane, arcFlat, static_cast<double>(a.startRad), &ex, &ey, &ez);
      PushSnapPickerEntry(ex, ey, Kind::Endpoint, sortWorldX, sortWorldY, out, ez);
      ArcSnapPoint(a, arcPlane, arcFlat, static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad), &ex,
                   &ey, &ez);
      PushSnapPickerEntry(ex, ey, Kind::Endpoint, sortWorldX, sortWorldY, out, ez);
    }
    break;
  }
  case Kind::Midpoint: {
    const auto& L = cmd.userLinesFlat;
    if (L.size() % 6 == 0) {
      for (size_t i = 0; i + 5 < L.size(); i += 6) {
        const float x0 = L[i];
        const float y0 = L[i + 1];
        const float x1 = L[i + 3];
        const float y1 = L[i + 4];
        PushSnapPickerEntry(0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, sortWorldX, sortWorldY, out,
                            0.5f * (L[i + 2] + L[i + 5]));
      }
    }
    for (const CadBlockRef& br : cmd.cadBlockRefs) {
      std::vector<CadBlockWorldSeg> segs;
      CadBlockCollectWorldLines(cmd.blockDefs, br, EntityAttributes{}, &segs);
      for (const CadBlockWorldSeg& s : segs)
        PushSnapPickerEntry(0.5f * (s.x0 + s.x1), 0.5f * (s.y0 + s.y1), Kind::Midpoint, sortWorldX, sortWorldY,
                            out, 0.5f * (s.z0 + s.z1));
    }
    const int polyCount =
        static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
    for (int pi = 0; pi < polyCount; ++pi) {
      const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const bool closed =
          static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
      auto pushEdgeMid = [&](int ia, int ib) {
        const float ax = cmd.userPolylineVerts[static_cast<size_t>(ia * 3)];
        const float ay = cmd.userPolylineVerts[static_cast<size_t>(ia * 3 + 1)];
        const float bx = cmd.userPolylineVerts[static_cast<size_t>(ib * 3)];
        const float by = cmd.userPolylineVerts[static_cast<size_t>(ib * 3 + 1)];
        PushSnapPickerEntry(0.5f * (ax + bx), 0.5f * (ay + by), Kind::Midpoint, sortWorldX, sortWorldY, out,
                            0.5f * (cmd.userPolylineVerts[static_cast<size_t>(ia * 3 + 2)] +
                                    cmd.userPolylineVerts[static_cast<size_t>(ib * 3 + 2)]));
      };
      for (int vi = v0; vi + 1 < v1; ++vi)
        pushEdgeMid(vi, vi + 1);
      if (closed && v1 - v0 >= 2)
        pushEdgeMid(v1 - 1, v0);
    }
    constexpr int kArcSnapSeg = 24;
    for (const CadArc& a : cmd.userArcs) {
      if (a.r <= 1e-6f || kArcSnapSeg < 1)
        continue;
      ucs::Ucs arcPlane;
      const bool arcFlat = ArcSnapPlane(a, &arcPlane);
      for (int i = 0; i < kArcSnapSeg; ++i) {
        const double t0 = CurveSampleAngle(static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), i,
                                           kArcSnapSeg);
        const double t1 = CurveSampleAngle(static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), i + 1,
                                           kArcSnapSeg);
        float x0 = 0.f;
        float y0 = 0.f;
        float z0 = 0.f;
        float x1 = 0.f;
        float y1 = 0.f;
        float z1 = 0.f;
        ArcSnapPoint(a, arcPlane, arcFlat, t0, &x0, &y0, &z0);
        ArcSnapPoint(a, arcPlane, arcFlat, t1, &x1, &y1, &z1);
        PushSnapPickerEntry(0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, sortWorldX, sortWorldY, out,
                            0.5f * (z0 + z1));
      }
    }
    constexpr int kEllSnapSeg = 36;
    constexpr double kTwoPi = 6.283185307179586;
    for (const CadEllipse& el : cmd.userEllipses) {
      const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
      if (ma < 1e-12 || kEllSnapSeg < 3)
        continue;
      const double ux = static_cast<double>(el.majVx) / ma;
      const double uy = static_cast<double>(el.majVy) / ma;
      const double px = -uy;
      const double py = ux;
      const double mb = ma * static_cast<double>(el.ratio);
      const double ecx = static_cast<double>(el.cx);
      const double ecy = static_cast<double>(el.cy);
      for (int i = 0; i < kEllSnapSeg; ++i) {
        const double ang0 = kTwoPi * static_cast<double>(i) / static_cast<double>(kEllSnapSeg);
        const double ang1 = kTwoPi * static_cast<double>(i + 1) / static_cast<double>(kEllSnapSeg);
        const double c0 = std::cos(ang0);
        const double s0 = std::sin(ang0);
        const double c1 = std::cos(ang1);
        const double s1 = std::sin(ang1);
        const double x0 = ecx + ux * (ma * c0) + px * (mb * s0);
        const double y0 = ecy + uy * (ma * c0) + py * (mb * s0);
        const double x1 = ecx + ux * (ma * c1) + px * (mb * s1);
        const double y1 = ecy + uy * (ma * c1) + py * (mb * s1);
        PushSnapPickerEntry(static_cast<float>(0.5 * (x0 + x1)), static_cast<float>(0.5 * (y0 + y1)), Kind::Midpoint,
                            sortWorldX, sortWorldY, out, el.z);
      }
    }
    break;
  }
  case Kind::Center: {
    const auto& C = cmd.userCirclesCxCyZR;
    if (C.size() % 4 == 0) {  // cx,cy,z,r
      for (size_t i = 0; i + 3 < C.size(); i += 4)
        PushSnapPickerEntry(C[i], C[i + 1], Kind::Center, sortWorldX, sortWorldY, out, C[i + 2]);
    }
    for (const CadEllipse& el : cmd.userEllipses) {
      const float ma = std::hypot(el.majVx, el.majVy);
      if (ma < 1e-8f)
        continue;
      PushSnapPickerEntry(el.cx, el.cy, Kind::Center, sortWorldX, sortWorldY, out, el.z);
    }
    break;
  }
  case Kind::Perpendicular: {
    if (!havePerpRef)
      break;
    const auto& L = cmd.userLinesFlat;
    if (L.size() % 6 == 0) {
      for (size_t i = 0; i + 5 < L.size(); i += 6)
        PushPerpFootEntry(refPx, refPy, L[i], L[i + 1], L[i + 3], L[i + 4], sortWorldX, sortWorldY, out, L[i + 2],
                          L[i + 5]);
    }
    const int polyCount =
        static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
    for (int pi = 0; pi < polyCount; ++pi) {
      const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const bool closed =
          static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
      auto pushEdgePerp = [&](int ia, int ib) {
        const float ax = cmd.userPolylineVerts[static_cast<size_t>(ia * 3)];
        const float ay = cmd.userPolylineVerts[static_cast<size_t>(ia * 3 + 1)];
        const float bx = cmd.userPolylineVerts[static_cast<size_t>(ib * 3)];
        const float by = cmd.userPolylineVerts[static_cast<size_t>(ib * 3 + 1)];
        PushPerpFootEntry(refPx, refPy, ax, ay, bx, by, sortWorldX, sortWorldY, out,
                          cmd.userPolylineVerts[static_cast<size_t>(ia * 3 + 2)],
                          cmd.userPolylineVerts[static_cast<size_t>(ib * 3 + 2)]);
      };
      for (int vi = v0; vi + 1 < v1; ++vi)
        pushEdgePerp(vi, vi + 1);
      if (closed && v1 - v0 >= 2)
        pushEdgePerp(v1 - 1, v0);
    }
    constexpr int kArcSnapSeg = 24;
    for (const CadArc& a : cmd.userArcs) {
      if (a.r <= 1e-6f || kArcSnapSeg < 1)
        continue;
      ucs::Ucs arcPlane;
      const bool arcFlat = ArcSnapPlane(a, &arcPlane);
      for (int i = 0; i < kArcSnapSeg; ++i) {
        const double t0 = CurveSampleAngle(static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), i,
                                           kArcSnapSeg);
        const double t1 = CurveSampleAngle(static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), i + 1,
                                           kArcSnapSeg);
        float x0 = 0.f;
        float y0 = 0.f;
        float z0 = 0.f;
        float x1 = 0.f;
        float y1 = 0.f;
        float z1 = 0.f;
        ArcSnapPoint(a, arcPlane, arcFlat, t0, &x0, &y0, &z0);
        ArcSnapPoint(a, arcPlane, arcFlat, t1, &x1, &y1, &z1);
        PushPerpFootEntry(refPx, refPy, x0, y0, x1, y1, sortWorldX, sortWorldY, out, z0, z1);
      }
    }
    constexpr int kEllSnapSeg = 36;
    constexpr float kTwoPi = 6.28318530718f;
    for (const CadEllipse& el : cmd.userEllipses) {
      const float ma = std::hypot(el.majVx, el.majVy);
      if (ma < 1e-8f || kEllSnapSeg < 3)
        continue;
      const float ux = el.majVx / ma;
      const float uy = el.majVy / ma;
      const float px = -uy;
      const float py = ux;
      const float mb = ma * el.ratio;
      for (int i = 0; i < kEllSnapSeg; ++i) {
        const float ang0 = kTwoPi * static_cast<float>(i) / static_cast<float>(kEllSnapSeg);
        const float ang1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(kEllSnapSeg);
        const float c0 = std::cos(ang0);
        const float s0 = std::sin(ang0);
        const float c1 = std::cos(ang1);
        const float s1 = std::sin(ang1);
        const float x0 = el.cx + ux * (ma * c0) + px * (mb * s0);
        const float y0 = el.cy + uy * (ma * c0) + py * (mb * s0);
        const float x1 = el.cx + ux * (ma * c1) + px * (mb * s1);
        const float y1 = el.cy + uy * (ma * c1) + py * (mb * s1);
        PushPerpFootEntry(refPx, refPy, x0, y0, x1, y1, sortWorldX, sortWorldY, out, el.z, el.z);
      }
    }
    break;
  }
  case Kind::GeometricCenter: {
    const int polyCount =
        static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
    for (int pi = 0; pi < polyCount; ++pi) {
      const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      if (!PolylineBoundsArea(cmd, pi, v0, v1))
        continue;
      float gcx = 0.f;
      float gcy = 0.f;
      if (ClosedPolylineCentroid(cmd.userPolylineVerts, v0, v1, &gcx, &gcy))
        PushSnapPickerEntry(gcx, gcy, Kind::GeometricCenter, sortWorldX, sortWorldY, out,
                            PolylineLoopMeanZ(cmd.userPolylineVerts, v0, v1));
    }
    break;
  }
  case Kind::SurveyCenter:
    for (const SurveyPoint& sp : cmd.surveyPoints)
      PushSnapPickerEntry(sp.easting, sp.northing, Kind::SurveyCenter, sortWorldX, sortWorldY, out,
                          sp.elevation);  // elevation IS the point's Z (REQ-057)
    break;
  case Kind::Intersection:
  case Kind::ApparentIntersection: {
    // Unlike every other kind, these are PAIRWISE — "all in the model" would be O(n²) over the
    // whole drawing, which on a real topo is unbounded work for a menu. So the list is the
    // intersections within roughly one screen height of the click, which is the neighbourhood the
    // user is choosing from anyway. The menu title says "all in model"; for these two it is not,
    // and that is a deliberate trade rather than an oversight.
    const double searchR = static_cast<double>(1.f / std::max(cmd.viewportZoom, 1.e-9f)) * 50.0;
    std::vector<IsectSeg> segs;
    std::vector<IsectConic> conics;
    GatherNearCursor(cmd, sortWorldX, sortWorldY, searchR, nullptr, &segs, &conics);
    if (segs.size() + conics.size() < 2)
      break;
    std::vector<IsectCandidate> cand;
    if (kind == Kind::Intersection)
      ComputeTrueIntersections(segs, conics, &cand);
    else
      ComputeApparentIntersections(CadViewCamera(cmd), segs, conics, &cand);
    for (const IsectCandidate& c : cand)
      PushSnapPickerEntry(static_cast<float>(c.x), static_cast<float>(c.y), kind, sortWorldX, sortWorldY, out,
                          static_cast<float>(c.z));
    break;
  }
  case Kind::Grip:
    break; // grip snap points are per-selection, not gathered globally
  // A solid's edge and face have no global list to gather: both are resolved from the cursor RAY
  // against the solid under it, so there is no aperture-free "every one of these in the drawing"
  // for the snap picker to enumerate. Listed explicitly rather than left to a default, so adding
  // a Kind later is a compile error here rather than a silently missing entry.
  case Kind::Edge:
  case Kind::Face:
    break;
  case Kind::Surface: {
    float z = 0.f;
    if (SurfaceSnapElevation(cmd, static_cast<double>(sortWorldX), static_cast<double>(sortWorldY), &z))
      PushSnapPickerEntry(sortWorldX, sortWorldY, Kind::Surface, sortWorldX, sortWorldY, out, z);
    break;
  }
  }
  SortDedupeSnapPicker(out);
}

Hit FindGripSnap(double wx, double wy, const AppCommandState& cmd, float tolWorld) {
  SnapPickAccum acc{};
  const float tol2 = tolWorld * tolWorld;

  /// \param gz the grip's own elevation (REQ-058). Grip snap outranks every geometry snap
  ///        (Priority(Grip) == 4), so a grip reported at Z 0 does not merely miss — it WINS over
  ///        a correct endpoint candidate and drags the commit down to the datum.
  auto gripCandidate = [&](float gx, float gy, float gz) {
    const float dx = gx - static_cast<float>(wx);
    const float dy = gy - static_cast<float>(wy);
    if (dx * dx + dy * dy <= tol2)
      ConsiderSnap(&acc, wx, wy, gx, gy, Kind::Grip, 0.f, tolWorld, gz);
  };

  // CAD entity grips
  for (const SelectedEntity& sel : cmd.selection) {
    if (sel.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(sel.index) * 6;
      if (k + 5 < cmd.userLinesFlat.size()) {
        gripCandidate(cmd.userLinesFlat[k],     cmd.userLinesFlat[k + 1], cmd.userLinesFlat[k + 2]);
        gripCandidate(cmd.userLinesFlat[k + 3], cmd.userLinesFlat[k + 4], cmd.userLinesFlat[k + 5]);
      }
    } else if (sel.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(sel.index) * 4;
      if (k + 3 < cmd.userCirclesCxCyZR.size()) {
        const float cx = cmd.userCirclesCxCyZR[k];
        const float cy = cmd.userCirclesCxCyZR[k + 1];
        const float cz = cmd.userCirclesCxCyZR[k + 2];
        gripCandidate(cx, cy, cz);
        gripCandidate(cx + cmd.userCirclesCxCyZR[k + 3], cy, cz);
      }
    } else if (sel.type == SelectedEntity::Type::Polyline) {
      const int np = static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
      if (sel.index >= 0 && sel.index < np) {
        const int startV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index)];
        const int endV   = cmd.userPolylineOffsets[static_cast<size_t>(sel.index + 1)];
        for (int vi = 0; vi < endV - startV; ++vi) {
          const size_t xIdx = static_cast<size_t>(startV + vi) * 3;
          if (xIdx + 2 >= cmd.userPolylineVerts.size()) break;
          gripCandidate(cmd.userPolylineVerts[xIdx], cmd.userPolylineVerts[xIdx + 1],
                        cmd.userPolylineVerts[xIdx + 2]);
        }
      }
    } else if (sel.type == SelectedEntity::Type::Arc) {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userArcs.size()) {
        const CadArc& a = cmd.userArcs[static_cast<size_t>(sel.index)];
        const float endRad = a.startRad + a.sweepRad;
        gripCandidate(a.cx, a.cy, a.z);
        gripCandidate(a.cx + a.r * std::cos(a.startRad), a.cy + a.r * std::sin(a.startRad), a.z);
        gripCandidate(a.cx + a.r * std::cos(endRad),     a.cy + a.r * std::sin(endRad),     a.z);
      }
    } else if (sel.type == SelectedEntity::Type::Ellipse) {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userEllipses.size()) {
        const CadEllipse& el = cmd.userEllipses[static_cast<size_t>(sel.index)];
        const float perpX = -el.majVy, perpY = el.majVx;
        gripCandidate(el.cx, el.cy, el.z);
        gripCandidate(el.cx + el.majVx,            el.cy + el.majVy,            el.z);
        gripCandidate(el.cx + perpX * el.ratio,     el.cy + perpY * el.ratio,    el.z);
      }
    } else if (sel.type == SelectedEntity::Type::Annotation) {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.cadAnnotations.size()) {
        const CadAnnotation& a = cmd.cadAnnotations[static_cast<size_t>(sel.index)];
        if (a.kind == CadAnnotation::Kind::Mtext) {
          if (a.surveyPointLabelForId >= 0) {
            gripCandidate(0.5f * (a.boxMinX + a.boxMaxX), 0.5f * (a.boxMinY + a.boxMaxY), a.insZ);
          } else {
            gripCandidate(a.boxMinX, a.boxMinY, a.insZ);
            gripCandidate(a.boxMaxX, a.boxMinY, a.insZ);
            gripCandidate(a.boxMaxX, a.boxMaxY, a.insZ);
            gripCandidate(a.boxMinX, a.boxMaxY, a.insZ);
          }
        }
      }
    }
  }

  // Survey point grips (selected survey points)
  for (const int idx : cmd.selectedSurveyPointIndices) {
    if (idx >= 0 && static_cast<size_t>(idx) < cmd.surveyPoints.size())
      gripCandidate(cmd.surveyPoints[static_cast<size_t>(idx)].easting,
                    cmd.surveyPoints[static_cast<size_t>(idx)].northing,
                    cmd.surveyPoints[static_cast<size_t>(idx)].elevation);  // elevation IS Z (REQ-057)
  }

  return acc.best;
}

} // namespace CadSnap
