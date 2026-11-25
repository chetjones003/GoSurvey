#include "CadCommandsInternal.hpp"
#include <GoSurvey/Geometry/curveintersect.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

static bool SegSegIntersectParamD(double ax, double ay, double bx, double by, double cx, double cy,
                                  double dx, double dy, double* tAB) {
  const double rx = bx - ax, ry = by - ay;
  const double sx = dx - cx, sy = dy - cy;
  const double denom = rx * sy - ry * sx;
  if (std::fabs(denom) < 1e-12) return false;
  const double wx = cx - ax, wy = cy - ay;
  const double t = (wx * sy - wy * sx) / denom;
  const double u = (wx * ry - wy * rx) / denom;
  if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0) { *tAB = t; return true; }
  return false;
}

struct TrimTargetEdge {
  enum Kind : uint8_t { Line = 0, Poly = 1 } kind = Line;
  int lineIx = -1;
  int polyIx = -1;
  int vLo = -1;
};

static double PickDistSqPointSegmentD(double px, double py, double ax, double ay, double bx, double by);

static bool SelectedEntityMatches(const SelectedEntity& a, const SelectedEntity& b) {
  return a.type == b.type && a.index == b.index;
}

static void CollectCutSegments(const AppCommandState& st, const SelectedEntity& cut,
                               std::vector<std::array<double, 4>>* out) {
  using ST = SelectedEntity::Type;
  if (cut.type == ST::LineSeg) {
    const size_t k = static_cast<size_t>(cut.index);
    if (k < st.drawing.lineCount())
      out->push_back({st.drawing.line(k).x0, st.drawing.line(k).y0, st.drawing.line(k).x1,
                      st.drawing.line(k).y1});
    return;
  }
  if (cut.type == ST::Circle) {
    const size_t ci = static_cast<size_t>(cut.index);
    if (ci >= st.drawing.circleCount())
      return;
    const CadCircleRecord cc = st.drawing.circle(ci);
    const float cx = static_cast<float>(cc.cx);
    const float cy = static_cast<float>(cc.cy);
    const float r = static_cast<float>(cc.r);
    constexpr int n = 48;
    const double dcx = static_cast<double>(cx);
    const double dcy = static_cast<double>(cy);
    const double dr = static_cast<double>(r);
    double px = 0.;
    double py = 0.;
    CirclePointWorld(dcx, dcy, dr, 0.0, &px, &py);
    for (int i = 1; i <= n; ++i) {
      constexpr double kTwoPi = 6.283185307179586;
      const double t = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      double x = 0.;
      double y = 0.;
      CirclePointWorld(dcx, dcy, dr, t, &x, &y);
      out->push_back({static_cast<float>(px), static_cast<float>(py), static_cast<float>(x), static_cast<float>(y)});
      px = x;
      py = y;
    }
    return;
  }
  if (cut.type == ST::Arc) {
    const size_t k = static_cast<size_t>(cut.index);
    if (k >= st.drawing.userArcs.size())
      return;
    const CadArc& a = st.drawing.userArcs[k];
    constexpr int n = 40;
    const double dcx = static_cast<double>(a.cx);
    const double dcy = static_cast<double>(a.cy);
    const double dr = static_cast<double>(a.r);
    double px = 0.;
    double py = 0.;
    CirclePointWorld(dcx, dcy, dr, static_cast<double>(a.startRad), &px, &py);
    for (int i = 1; i <= n; ++i) {
      const double u = static_cast<double>(i) / static_cast<double>(n);
      const double ang = static_cast<double>(a.startRad + a.sweepRad * u);
      double x = 0.;
      double y = 0.;
      CirclePointWorld(dcx, dcy, dr, ang, &x, &y);
      out->push_back({static_cast<float>(px), static_cast<float>(py), static_cast<float>(x), static_cast<float>(y)});
      px = x;
      py = y;
    }
    return;
  }
  if (cut.type == ST::Ellipse) {
    const size_t k = static_cast<size_t>(cut.index);
    if (k >= st.drawing.userEllipses.size())
      return;
    const CadEllipse& el = st.drawing.userEllipses[k];
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1e-12)
      return;
    const double ux = static_cast<double>(el.majVx) / ma;
    const double uy = static_cast<double>(el.majVy) / ma;
    const double pxv = -uy;
    const double pyv = ux;
    const double mb = ma * static_cast<double>(el.ratio);
    constexpr int n = 48;
    constexpr double kTwoPi = 6.283185307179586;
    const double ecx = static_cast<double>(el.cx);
    const double ecy = static_cast<double>(el.cy);
    double px = ecx + ux * ma;
    double py = ecy + uy * ma;
    for (int i = 1; i <= n; ++i) {
      const double ang = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      const double c = std::cos(ang);
      const double s = std::sin(ang);
      const double x = ecx + ux * (ma * c) + pxv * (mb * s);
      const double y = ecy + uy * (ma * c) + pyv * (mb * s);
      out->push_back({static_cast<float>(px), static_cast<float>(py), static_cast<float>(x), static_cast<float>(y)});
      px = x;
      py = y;
    }
    return;
  }
  if (cut.type == ST::Polyline) {
    const int pi = cut.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
      return;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const float ax = static_cast<float>(st.drawing.vertex(vi).x);
      const float ay = static_cast<float>(st.drawing.vertex(vi).y);
      const float bx = static_cast<float>(st.drawing.vertex(static_cast<size_t>(vi + 1)).x);
      const float by = static_cast<float>(st.drawing.vertex(static_cast<size_t>(vi + 1)).y);
      out->push_back({ax, ay, bx, by});
    }
    if (closed && v1 - v0 >= 2) {
      const float ax = static_cast<float>(st.drawing.vertex(static_cast<size_t>(v1 - 1)).x);
      const float ay = static_cast<float>(st.drawing.vertex(static_cast<size_t>(v1 - 1)).y);
      const float bx = static_cast<float>(st.drawing.vertex(v0).x);
      const float by = static_cast<float>(st.drawing.vertex(v0).y);
      out->push_back({ax, ay, bx, by});
    }
  }
}

static void AppendPolylineCutEdgesExcept(const AppCommandState& st, int pi, int skipEdgeVi,
                                         std::vector<std::array<double, 4>>* out) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
    return;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  const bool closed =
      static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
  auto pushEdge = [&](int vi) {
    if (vi == skipEdgeVi)
      return;
    const float ax = static_cast<float>(st.drawing.vertex(vi).x);
    const float ay = static_cast<float>(st.drawing.vertex(vi).y);
    const float bx = static_cast<float>(st.drawing.vertex(static_cast<size_t>(vi + 1)).x);
    const float by = static_cast<float>(st.drawing.vertex(static_cast<size_t>(vi + 1)).y);
    out->push_back({ax, ay, bx, by});
  };
  for (int vi = v0; vi + 1 < v1; ++vi)
    pushEdge(vi);
  if (closed && v1 - v0 >= 2) {
    const int closingVi = v1 - 1;
    if (closingVi != skipEdgeVi) {
      const float ax = static_cast<float>(st.drawing.vertex(closingVi).x);
      const float ay = static_cast<float>(st.drawing.vertex(closingVi).y);
      const float bx = static_cast<float>(st.drawing.vertex(v0).x);
      const float by = static_cast<float>(st.drawing.vertex(v0).y);
      out->push_back({ax, ay, bx, by});
    }
  }
}

static void CollectAllDrawingCutSegmentsExceptTarget(const AppCommandState& st, const TrimTargetEdge* excludeEdge,
                                                     std::vector<std::array<double, 4>>* out) {
  out->clear();
  if (st.drawing.lineStoreWellFormed()) {
    for (size_t li = 0; li < st.drawing.lineCount(); ++li) {
      const CadLineRecord ln = st.drawing.line(li);
      const int idx = static_cast<int>(li);
      if (excludeEdge && excludeEdge->kind == TrimTargetEdge::Line && excludeEdge->lineIx == idx)
        continue;
      SelectedEntity cut{};
      cut.type = SelectedEntity::Type::LineSeg;
      cut.index = idx;
      CollectCutSegments(st, cut, out);
    }
  }
  if (st.drawing.circleStoreWellFormed()) {
    for (size_t ci = 0; ci < st.drawing.circleCount(); ++ci) {
      SelectedEntity cut{};
      cut.type = SelectedEntity::Type::Circle;
      cut.index = static_cast<int>(ci);
      CollectCutSegments(st, cut, out);
    }
  }
  for (size_t ai = 0; ai < st.drawing.userArcs.size(); ++ai) {
    SelectedEntity cut{};
    cut.type = SelectedEntity::Type::Arc;
    cut.index = static_cast<int>(ai);
    CollectCutSegments(st, cut, out);
  }
  for (size_t ei = 0; ei < st.drawing.userEllipses.size(); ++ei) {
    SelectedEntity cut{};
    cut.type = SelectedEntity::Type::Ellipse;
    cut.index = static_cast<int>(ei);
    CollectCutSegments(st, cut, out);
  }
  const int nPoly =
      static_cast<int>(st.drawing.userPolylineOffsets.size() > 0 ? st.drawing.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    if (excludeEdge && excludeEdge->kind == TrimTargetEdge::Poly && excludeEdge->polyIx == pi)
      AppendPolylineCutEdgesExcept(st, pi, excludeEdge->vLo, out);
    else {
      SelectedEntity cut{};
      cut.type = SelectedEntity::Type::Polyline;
      cut.index = pi;
      CollectCutSegments(st, cut, out);
    }
  }
}

static void BuildTrimCutSegments(const AppCommandState& st, const std::vector<SelectedEntity>& cutters,
                                 const TrimTargetEdge* excludeEdge, std::vector<std::array<double, 4>>* out) {
  out->clear();
  for (const SelectedEntity& cut : cutters) {
    if (excludeEdge && cut.type == SelectedEntity::Type::LineSeg && excludeEdge->kind == TrimTargetEdge::Line &&
        cut.index == excludeEdge->lineIx)
      continue;
    if (excludeEdge && cut.type == SelectedEntity::Type::Polyline && excludeEdge->kind == TrimTargetEdge::Poly &&
        cut.index == excludeEdge->polyIx) {
      AppendPolylineCutEdgesExcept(st, cut.index, excludeEdge->vLo, out);
      continue;
    }
    CollectCutSegments(st, cut, out);
  }
}

static bool PickClosestTrimTarget(const AppCommandState& st, float wx, float wy, float tolWorld,
                                  TrimTargetEdge* outRef, double* ax, double* ay, double* bx, double* by,
                                  float* outDistSq) {
  if (!outRef || !ax || !ay || !bx || !by || !outDistSq)
    return false;
  const float tol2 = tolWorld * tolWorld;
  bool any = false;
  float best = 0.f;
  TrimTargetEdge bestR{};
  double bax = 0.0, bay = 0.0, bbx = 0.0, bby = 0.0;

  if (st.drawing.lineStoreWellFormed()) {
    for (size_t li = 0; li < st.drawing.lineCount(); ++li) {
      const CadLineRecord ln = st.drawing.line(li);
      const double x0 = ln.x0, y0 = ln.y0, x1 = ln.x1, y1 = ln.y1;
      const float d2 = static_cast<float>(PickDistSqPointSegmentD(wx, wy, x0, y0, x1, y1));
      if (d2 > tol2)
        continue;
      if (!any || d2 < best - 1e-12f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Line;
        bestR.lineIx = static_cast<int>(li);
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    }
  }

  const int nPoly =
      static_cast<int>(st.drawing.userPolylineOffsets.size() > 0 ? st.drawing.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
    auto tryEdge = [&](int vi) {
      const double x0 = st.drawing.vertex(vi).x;
      const double y0 = st.drawing.vertex(vi).y;
      const double x1 = st.drawing.vertex(static_cast<size_t>(vi + 1)).x;
      const double y1 = st.drawing.vertex(static_cast<size_t>(vi + 1)).y;
      const float d2 = static_cast<float>(PickDistSqPointSegmentD(wx, wy, x0, y0, x1, y1));
      if (d2 > tol2)
        return;
      if (!any || d2 < best - 1e-12f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Poly;
        bestR.polyIx = pi;
        bestR.vLo = vi;
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      tryEdge(vi);
    if (closed && v1 - v0 >= 2)
      tryEdge(v1 - 1);
  }

  if (!any)
    return false;
  *outRef = bestR;
  *ax = bax;
  *ay = bay;
  *bx = bbx;
  *by = bby;
  *outDistSq = best;
  return true;
}

/// Drawing edge (line or poly segment) whose geometry passes closest to the user-drawn segment \p u1–\p u2.
static bool PickTrimTargetClosestToDrawnSegment(const AppCommandState& st, float u1x, float u1y, float u2x, float u2y,
                                                 float tolWorld, TrimTargetEdge* outRef, double* ax, double* ay,
                                                 double* bx, double* by, float* outDistSq) {
  if (!outRef || !ax || !ay || !bx || !by || !outDistSq)
    return false;
  const float tol2 = tolWorld * tolWorld;
  bool any = false;
  float best = 0.f;
  TrimTargetEdge bestR{};
  double bax = 0.0, bay = 0.0, bbx = 0.0, bby = 0.0;

  if (st.drawing.lineStoreWellFormed()) {
    for (size_t li = 0; li < st.drawing.lineCount(); ++li) {
      const CadLineRecord ln = st.drawing.line(li);
      const double x0 = ln.x0, y0 = ln.y0, x1 = ln.x1, y1 = ln.y1;
      const float d2 = CadCmdGeom::MinDistSqSegSeg(u1x, u1y, u2x, u2y, x0, y0, x1, y1);
      if (d2 > tol2)
        continue;
      if (!any || d2 < best - 1e-18f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Line;
        bestR.lineIx = static_cast<int>(li);
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    }
  }

  const int nPoly =
      static_cast<int>(st.drawing.userPolylineOffsets.size() > 0 ? st.drawing.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
    auto tryEdge = [&](int vi) {
      const double x0 = st.drawing.vertex(vi).x;
      const double y0 = st.drawing.vertex(vi).y;
      const double x1 = st.drawing.vertex(static_cast<size_t>(vi + 1)).x;
      const double y1 = st.drawing.vertex(static_cast<size_t>(vi + 1)).y;
      const float d2 = CadCmdGeom::MinDistSqSegSeg(u1x, u1y, u2x, u2y, x0, y0, x1, y1);
      if (d2 > tol2)
        return;
      if (!any || d2 < best - 1e-18f) {
        any = true;
        best = d2;
        bestR.kind = TrimTargetEdge::Poly;
        bestR.polyIx = pi;
        bestR.vLo = vi;
        bax = x0;
        bay = y0;
        bbx = x1;
        bby = y1;
      }
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      tryEdge(vi);
    if (closed && v1 - v0 >= 2)
      tryEdge(v1 - 1);
  }

  if (!any)
    return false;
  *outRef = bestR;
  *ax = bax;
  *ay = bay;
  *bx = bbx;
  *by = bby;
  *outDistSq = best;
  return true;
}

static bool TrimSegmentIntersectPickSide(double ax, double ay, double bx, double by, double pickX, double pickY,
                                         const std::vector<std::array<double, 4>>& cuts, const AppCommandState& st,
                                         double fenceFx, double fenceFy, double fenceGx, double fenceGy,
                                         bool useFenceToPickIntersection,
                                         double* outIx, double* outIy, bool* trimFromA, std::vector<std::string>* log) {
  double epsGeom = 1e-5;
  double dmnX = 0.;
  double dmxX = 0.;
  double dmnY = 0.;
  double dmxY = 0.;
  if (ComputeWorldExtents(st, &dmnX, &dmxX, &dmnY, &dmxY))
    epsGeom = std::max(1e-8, 1e-6 * std::max(dmxX - dmnX, dmxY - dmnY));

  const double vx = bx - ax;
  const double vy = by - ay;
  const double len2 = vx * vx + vy * vy;
  if (len2 < 1e-24) {
    if (log)
      log->push_back("TRIM — degenerate segment.");
    return false;
  }
  const double segLen = std::sqrt(len2);
  const double epsT = std::clamp(segLen > 1e-12 ? epsGeom / segLen : 1e-9, 1e-12, 0.05);

  std::vector<double> ts;
  for (const auto& seg : cuts) {
    double t = 0.0;
    if (SegSegIntersectParamD(ax, ay, bx, by, seg[0], seg[1], seg[2], seg[3], &t)) {
      if (t > epsT && t < 1.0 - epsT)
        ts.push_back(t);
    }
  }
  if (ts.empty()) {
    if (log)
      log->push_back("TRIM — segment does not cross a cutting edge.");
    return false;
  }

  std::sort(ts.begin(), ts.end());
  ts.erase(std::unique(ts.begin(), ts.end(),
                       [&](double a, double b) { return std::fabs(a - b) < std::max(1e-12, epsGeom / segLen); }),
           ts.end());

  const double u = std::clamp(((pickX - ax) * vx + (pickY - ay) * vy) / len2, 0.0, 1.0);

  const double fdx = fenceGx - fenceFx;
  const double fdy = fenceGy - fenceFy;
  const double fenceLen2 = fdx * fdx + fdy * fdy;
  const bool fenceOk = useFenceToPickIntersection && fenceLen2 >= 1e-24;

  double tNear = ts.front();
  if (fenceOk) {
    double bestFenceD2 = 1e300;
    double bestPickAbs = 1e300;
    for (double t : ts) {
      const double ix = ax + t * vx;
      const double iy = ay + t * vy;
      const double df2 = PickDistSqPointSegmentD(ix, iy, fenceFx, fenceFy, fenceGx, fenceGy);
      const double dp = std::fabs(t - u);
      if (df2 < bestFenceD2 - 1e-24 ||
          (std::fabs(df2 - bestFenceD2) <= 1e-24 && dp < bestPickAbs - 1e-15)) {
        bestFenceD2 = df2;
        bestPickAbs = dp;
        tNear = t;
      }
    }
  } else {
    double bestAbs = std::fabs(ts.front() - u);
    for (double t : ts) {
      const double d = std::fabs(t - u);
      if (d < bestAbs - 1e-15) {
        bestAbs = d;
        tNear = t;
      }
    }
  }

  const double ix = ax + tNear * vx;
  const double iy = ay + tNear * vy;
  const double epsParam = std::max(1e-12, epsGeom / segLen);

  bool trimA = false;
  if (u < tNear - epsParam)
    trimA = true;
  else if (u > tNear + epsParam)
    trimA = false;
  else {
    const double dA = (pickX - ax) * (pickX - ax) + (pickY - ay) * (pickY - ay);
    const double dB = (pickX - bx) * (pickX - bx) + (pickY - by) * (pickY - by);
    trimA = dA <= dB;
  }

  *outIx = ix;
  *outIy = iy;
  *trimFromA = trimA;
  return true;
}

/// AutoCAD-style: shorten segment toward the nearest cutting intersection from the pick (portion containing pick is
/// removed).
static bool TrimSegmentToCuttingEdges(AppCommandState& st, const TrimTargetEdge& tgt, double ax, double ay, double bx,
                                      double by, double pickX, double pickY,
                                      const std::vector<std::array<double, 4>>& cuts, bool useFence,
                                      double fenceFx, double fenceFy, double fenceGx, double fenceGy,
                                      std::vector<std::string>& log) {
  double ix = 0.0, iy = 0.0;
  bool trimA = false;
  if (!TrimSegmentIntersectPickSide(ax, ay, bx, by, pickX, pickY, cuts, st, fenceFx, fenceFy, fenceGx, fenceGy,
                                    useFence, &ix, &iy, &trimA, &log))
    return false;

  const double rx = ix;
  const double ry = iy;

  if (tgt.kind == TrimTargetEdge::Line) {
    const size_t k = static_cast<size_t>(tgt.lineIx);
    if (k >= st.drawing.lineCount())
      return false;
    if (trimA) {
      st.drawing.lineComponent(k, CadLineComponent::X0) = rx;
      st.drawing.lineComponent(k, CadLineComponent::Y0) = ry;
    } else {
      st.drawing.lineComponent(k, CadLineComponent::X1) = rx;
      st.drawing.lineComponent(k, CadLineComponent::Y1) = ry;
    }
    const double exx = st.drawing.line(k).x1 - st.drawing.line(k).x0;
    const double eyy = st.drawing.line(k).y1 - st.drawing.line(k).y0;
    if (exx * exx + eyy * eyy < 1e-24) {
      st.drawing.eraseLine(k);
      if (static_cast<size_t>(tgt.lineIx) < st.drawing.userLineAttrs.size())
        st.drawing.userLineAttrs.erase(st.drawing.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(tgt.lineIx));
    }
  } else {
    const int vi = trimA ? tgt.vLo : tgt.vLo + 1;
    const size_t vk = static_cast<size_t>(vi);
    if (vk >= st.drawing.vertexCount())
      return false;
    st.drawing.vertexComponent(vk, CadVertexComponent::X) = rx;
    st.drawing.vertexComponent(vk, CadVertexComponent::Y) = ry;
  }

  log.push_back("TRIM — segment shortened.");
  return true;
}


static void ExecuteDrawnSegmentTrimOnce(AppCommandState& st, float p1x, float p1y, float p2x, float p2y,
                                        float tolWorld, std::vector<std::string>& log) {
  float matchTol = std::max(tolWorld * 4.f, 1e-6f);
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    matchTol = std::max(matchTol, static_cast<float>(2e-5 * std::max(mxX - mnX, mxY - mnY)));
  TrimTargetEdge tgt{};
  double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
  float dEdge = 0.f;
  if (!PickTrimTargetClosestToDrawnSegment(st, p1x, p1y, p2x, p2y, matchTol, &tgt, &ax, &ay, &bx, &by, &dEdge)) {
    log.push_back("TRIM — no segment close enough to your line (draw along the edge to shorten).");
    return;
  }
  std::vector<std::array<double, 4>> cuts;
  CollectAllDrawingCutSegmentsExceptTarget(st, &tgt, &cuts);
  if (cuts.empty()) {
    log.push_back("TRIM — nothing crosses that segment.");
    return;
  }
  const float pmx = (p1x + p2x) * 0.5f;
  const float pmy = (p1y + p2y) * 0.5f;
  PushUndoSnapshot(st, "Trim");
  if (!TrimSegmentToCuttingEdges(st, tgt, ax, ay, bx, by, pmx, pmy, cuts, true, p1x, p1y, p2x, p2y, log))
    return;
  BumpCadGpuCache(st);
}

/// issue #399 increment 1: closest approach of two finite 3D segments (Real-Time Collision
/// Detection's ClosestPtSegmentSegment), clamped to [0,1] on each. \p outS is the parameter along
/// \p p0-\p p1 (the TRIM target), \p outT along \p q0-\p q1 (a cutting edge). Returns the squared
/// 3D gap between the two closest points — zero for genuinely coplanar/crossing lines, positive for
/// skew ones.
static double SegSegClosest3D(const ray3d::Vec3& p0, const ray3d::Vec3& p1, const ray3d::Vec3& q0,
                              const ray3d::Vec3& q1, double* outS, double* outT) {
  using namespace ray3d;
  const Vec3 d1 = Sub(p1, p0);
  const Vec3 d2 = Sub(q1, q0);
  const Vec3 r = Sub(p0, q0);
  const double a = Dot(d1, d1);
  const double e = Dot(d2, d2);
  const double f = Dot(d2, r);
  double s = 0.0, t = 0.0;
  if (a < 1e-24 && e < 1e-24) {
    s = 0.0;
    t = 0.0;
  } else if (a < 1e-24) {
    s = 0.0;
    t = std::clamp(f / e, 0.0, 1.0);
  } else {
    const double c = Dot(d1, r);
    if (e < 1e-24) {
      t = 0.0;
      s = std::clamp(-c / a, 0.0, 1.0);
    } else {
      const double b = Dot(d1, d2);
      const double denom = a * e - b * b;
      s = std::fabs(denom) > 1e-18 ? std::clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
      t = (b * s + f) / e;
      if (t < 0.0) {
        t = 0.0;
        s = std::clamp(-c / a, 0.0, 1.0);
      } else if (t > 1.0) {
        t = 1.0;
        s = std::clamp((b - c) / a, 0.0, 1.0);
      }
    }
  }
  const Vec3 cp1 = Add(p0, Scale(d1, s));
  const Vec3 cp2 = Add(q0, Scale(d2, t));
  if (outS)
    *outS = s;
  if (outT)
    *outT = t;
  const Vec3 gap = Sub(cp1, cp2);
  return Dot(gap, gap);
}

/// issue #399 increment 2: builds the local-2D `curveisect::Conic` for a Circle/Arc/Ellipse cutting
/// edge, expressed in ITS OWN plane (\ref CadEntities.hpp's `CurvePlane`) — centred at that plane's
/// origin, so `WorldToPlane(plane, worldPoint)` of any point on the curve lands exactly on the
/// returned conic's own parametrisation (REQ-312's own convention; the flat +Z case reproduces the
/// existing world-XY numbers exactly, byte for bit). Returns false for a degenerate curve.
static bool CutterCurvePlaneAndConic(const AppCommandState& st, const SelectedEntity& c, ucs::Ucs* outPlane,
                                     curveisect::Conic* outConic) {
  using ST = SelectedEntity::Type;
  if (c.type == ST::Circle) {
    const size_t ci = static_cast<size_t>(c.index);
    if (ci >= st.drawing.circleCount())
      return false;
    const CadCircleRecord cc = st.drawing.circle(ci);
    const float r = static_cast<float>(cc.r);
    if (!(r > 1e-9f))
      return false;
    float nx = 0.f, ny = 0.f, nz = 1.f;
    CircleNormalAt(st.drawing.userCircleNormals, ci, &nx, &ny, &nz);
    *outPlane = CurvePlane(cc.cx, cc.cy, cc.cz, nx, ny, nz);
    *outConic = curveisect::MakeCircle(0.0, 0.0, r);
    return true;
  }
  if (c.type == ST::Arc) {
    if (c.index < 0 || static_cast<size_t>(c.index) >= st.drawing.userArcs.size())
      return false;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(c.index)];
    if (!(a.r > 1e-9f))
      return false;
    *outPlane = CurvePlane(a);
    *outConic = curveisect::MakeArc(0.0, 0.0, a.r, a.startRad, a.sweepRad);
    return true;
  }
  if (c.type == ST::Ellipse) {
    if (c.index < 0 || static_cast<size_t>(c.index) >= st.drawing.userEllipses.size())
      return false;
    const CadEllipse& el = st.drawing.userEllipses[static_cast<size_t>(c.index)];
    if (std::hypot(el.majVx, el.majVy) < 1e-9)
      return false;
    *outPlane = CurvePlane(el.cx, el.cy, el.z, 0.0, 0.0, 1.0);
    *outConic = curveisect::MakeEllipse(0.0, 0.0, el.majVx, el.majVy, el.ratio);
    return true;
  }
  return false;
}

/// issue #399 increment 3: nearest STRAIGHT-CHORD segment of polyline \p polyIx to the pick ray (or
/// the flat wx,wy when no ray applies). Matches the existing 2D TRIM's own polyline treatment
/// exactly — \ref PickClosestTrimTarget and \ref CollectAllDrawingCutSegmentsExceptTarget both walk
/// a polyline as a chain of straight vi→vi+1 chords, with no bulge/tilted-arc awareness at all — so
/// this is not a narrowing of scope, it is the SAME limitation the 2D path already has. A curved
/// (bulge != 0) polyline segment is a later, separate piece of work (REQ-325's tilted-arc machinery
/// would be the way in), not part of issue #399. Returns false if the polyline has no segment within
/// tolerance of the pick.
static bool FindNearestPolylineSegment3D(const AppCommandState& st, int polyIx, double wx, double wy,
                                         const ray3d::Ray* pickRay, int* outVi, ray3d::Vec3* outA,
                                         ray3d::Vec3* outB) {
  if (polyIx < 0 || static_cast<size_t>(polyIx) + 1 >= st.drawing.userPolylineOffsets.size())
    return false;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(polyIx)];
  const int v1 = st.drawing.userPolylineOffsets[static_cast<size_t>(polyIx) + 1];
  const bool closed =
      static_cast<size_t>(polyIx) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(polyIx)];
  const bool useRay = pickRay != nullptr && pickRay->valid();
  bool any = false;
  double best = 0.0;
  auto tryEdge = [&](int vi) {
    const size_t a3 = static_cast<size_t>(vi), b3 = (static_cast<size_t>(vi)+ 1);
    if (b3 >= st.drawing.vertexCount())
      return;
    const ray3d::Vec3 a{st.drawing.vertex(a3).x, st.drawing.vertex(a3).y, st.drawing.vertex(a3).z};
    const ray3d::Vec3 b{st.drawing.vertex(b3).x, st.drawing.vertex(b3).y, st.drawing.vertex(b3).z};
    double d2;
    if (useRay) {
      const double d = ray3d::RaySegmentDistance(*pickRay, a, b);
      d2 = d * d;
    } else {
      d2 = PickDistSqPointSegmentD(wx, wy, a.x, a.y, b.x, b.y);
    }
    if (any && d2 >= best - 1e-12)
      return;
    any = true;
    best = d2;
    *outVi = vi;
    *outA = a;
    *outB = b;
  };
  for (int vi = v0; vi + 1 < v1; ++vi)
    tryEdge(vi);
  if (closed && v1 - v0 >= 2)
    tryEdge(v1 - 1);
  return any;
}

/// issue #399: shared 3D crossing finder for both TRIM paths — the classic "click the piece to
/// remove" (\ref Try3DLineTrim) and the smart drawn-line (\ref Try3DDrawnLineTrim). Appends to \p ts
/// every parameter in (\p epsT, 1-\p epsT) along the target segment \p ta -> \p tb where the target
/// meets a supported cutting entity in true 3D: closest-approach-within-\p epsGeom for Line and
/// polyline-chord cutters (\ref SegSegClosest3D), analytic conic intersection for a Circle/Arc/
/// Ellipse cutter that shares a plane with the target, and nothing for a skew curved cutter
/// (deferred — never guessed). \p targetIndex / \p targetVi identify the target so a segment never
/// cuts itself.
static void Collect3DTrimCrossings(const AppCommandState& st, const std::vector<SelectedEntity>& cutters,
                                   const ray3d::Vec3& ta, const ray3d::Vec3& tb, bool targetIsPoly,
                                   int targetIndex, int targetVi, double epsGeom, double epsT,
                                   std::vector<double>* ts) {
  auto tryLineCutSeg = [&](const ray3d::Vec3& qa, const ray3d::Vec3& qb) {
    double s = 0., t = 0.;
    const double gap2 = SegSegClosest3D(ta, tb, qa, qb, &s, &t);
    if (gap2 > epsGeom * epsGeom)
      return;  // farther apart than tolerance at closest approach: not an intersection
    if (s <= epsT || s >= 1.0 - epsT)
      return;  // touches only at (or past) an existing endpoint of the target
    ts->push_back(s);
  };

  for (const SelectedEntity& c : cutters) {
    if (c.type == SelectedEntity::Type::LineSeg) {
      if (c.index == targetIndex && !targetIsPoly)
        continue;  // a line can't cut itself
      const size_t ck = static_cast<size_t>(c.index);
      if (ck >= st.drawing.lineCount())
        continue;
      const ray3d::Vec3 qa{st.drawing.line(ck).x0, st.drawing.line(ck).y0, st.drawing.line(ck).z0};
      const ray3d::Vec3 qb{st.drawing.line(ck).x1, st.drawing.line(ck).y1, st.drawing.line(ck).z1};
      tryLineCutSeg(qa, qb);
      continue;
    }
    if (c.type == SelectedEntity::Type::Polyline) {
      if (c.index < 0 || static_cast<size_t>(c.index) + 1 >= st.drawing.userPolylineOffsets.size())
        continue;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(c.index)];
      const int v1 = st.drawing.userPolylineOffsets[static_cast<size_t>(c.index) + 1];
      const bool closed = static_cast<size_t>(c.index) < st.drawing.userPolylineClosed.size() &&
                          st.drawing.userPolylineClosed[static_cast<size_t>(c.index)];
      auto tryEdge = [&](int vi) {
        if (targetIsPoly && c.index == targetIndex && vi == targetVi)
          return;  // a polyline segment can't cut itself, but its siblings can
        const size_t a3 = static_cast<size_t>(vi), b3 = (static_cast<size_t>(vi)+ 1);
        if (b3 >= st.drawing.vertexCount())
          return;
        const ray3d::Vec3 qa{st.drawing.vertex(a3).x, st.drawing.vertex(a3).y, st.drawing.vertex(a3).z};
        const ray3d::Vec3 qb{st.drawing.vertex(b3).x, st.drawing.vertex(b3).y, st.drawing.vertex(b3).z};
        tryLineCutSeg(qa, qb);
      };
      for (int vi = v0; vi + 1 < v1; ++vi)
        tryEdge(vi);
      if (closed && v1 - v0 >= 2)
        tryEdge(v1 - 1);
      continue;
    }
    // Circle/Arc/Ellipse cutter: coplanarity is judged against the target's two endpoints (the same
    // signed-distance-to-plane test FILLET / issue #373 uses), then solved exactly via
    // `curveisect::IntersectSegConic` (REQ-062) — never by tessellating the curve.
    ucs::Ucs plane{};
    curveisect::Conic conic{};
    if (!CutterCurvePlaneAndConic(st, c, &plane, &conic))
      continue;
    const double da = ucs::SignedDistanceToPlane(plane, ta);
    const double db = ucs::SignedDistanceToPlane(plane, tb);
    if (std::fabs(da) > epsGeom || std::fabs(db) > epsGeom)
      continue;  // skew: no closed-form line-vs-curve closest approach yet (deferred); skip, don't guess
    const ucs::Point2D pa = ucs::WorldToPlane(plane, ta);
    const ucs::Point2D pb = ucs::WorldToPlane(plane, tb);
    const curveisect::Seg seg{{pa.x, pa.y}, {pb.x, pb.y}};
    std::vector<curveisect::Hit2> hits;
    curveisect::IntersectSegConic(seg, conic, &hits);
    for (const curveisect::Hit2& h : hits) {
      const double s = h.tA;  // WorldToPlane is affine: the projected segment's t IS the 3D line's t
      if (s <= epsT || s >= 1.0 - epsT)
        continue;
      ts->push_back(s);
    }
  }
}

/// issue #399: shared apply step for both 3D TRIM paths. Moves whichever end of the target — a Line
/// endpoint, or the picked polyline chord's near vertex (\p targetVi / \p targetVi+1) — that sits on
/// the \p trimA side to the 3D cut point \p cut, in all three coordinates; deletes a Line collapsed
/// to zero length. Pushes exactly one undo snapshot.
static void Apply3DTrimCut(AppCommandState& st, const SelectedEntity& hit, bool targetIsPoly, size_t tk,
                           int targetVi, const ray3d::Vec3& cut, bool trimA, std::vector<std::string>& log) {
  PushUndoSnapshot(st, "Trim");
  if (targetIsPoly) {
    const int vi = trimA ? targetVi : targetVi + 1;
    const size_t vk = static_cast<size_t>(vi);
    if (vk >= st.drawing.vertexCount()) {
      log.push_back("TRIM — nothing to trim at pick.");
      return;
    }
    st.drawing.vertexComponent(vk, CadVertexComponent::X) = static_cast<float>(cut.x);
    st.drawing.vertexComponent(vk, CadVertexComponent::Y) = static_cast<float>(cut.y);
    st.drawing.vertexComponent(vk, CadVertexComponent::Z) = static_cast<float>(cut.z);
  } else {
    st.drawing.lineComponent(tk, trimA ? CadLineComponent::X0 : CadLineComponent::X1) = static_cast<float>(cut.x);
    st.drawing.lineComponent(tk, trimA ? CadLineComponent::Y0 : CadLineComponent::Y1) = static_cast<float>(cut.y);
    st.drawing.lineComponent(tk, trimA ? CadLineComponent::Z0 : CadLineComponent::Z1) = static_cast<float>(cut.z);
    const CadLineRecord cutLine = st.drawing.line(tk);
    const double newLen = ray3d::Length(ray3d::Sub(
        ray3d::Vec3{cutLine.x1, cutLine.y1, cutLine.z1}, ray3d::Vec3{cutLine.x0, cutLine.y0, cutLine.z0}));
    if (newLen < 1e-6) {
      st.drawing.eraseLine(tk);
      if (static_cast<size_t>(hit.index) < st.drawing.userLineAttrs.size())
        st.drawing.userLineAttrs.erase(st.drawing.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(hit.index));
    }
  }
  log.push_back("TRIM — segment shortened.");
  BumpCadGpuCache(st);
}

/// issue #399 increments 1-3: TRIM's target-pick, cutting-edge collection and pick-side math are all
/// flat world-XY (\ref TrimSegmentIntersectPickSide, \ref PickClosestTrimTarget) — correct only in
/// plan view under the world UCS. An orbited camera or a non-world UCS needs the pick resolved
/// through the camera ray (same seam 3D Object Snap / issue #395 and FILLET / issue #373 use) and
/// the crossing found in true 3D, not the screen-space projection.
///
/// Scope: the TARGET being shortened is a Line (increments 1-2) or a straight polyline segment
/// (increment 3, \ref FindNearestPolylineSegment3D). The CUTTING edges may be Line (increment 1),
/// Circle/Arc/Ellipse (increment 2), or a polyline's straight segments (increment 3, walked chord by
/// chord the same way the 2D path already does). A coplanar cutter intersects the target exactly; a
/// skew line/polyline cutter uses closest-approach-within-tolerance (\ref SegSegClosest3D); a skew
/// curved cutter is silently skipped rather than guessed at — REQ-399's "otherwise report no
/// intersection" rule — since a closest-approach solve for a line against a curved cutting edge has
/// no closed form and stays deferred. Returns true once the pick has been fully handled (trimmed or
/// refused with a log message) so the caller must not also run the 2D path.
static bool Try3DLineTrim(AppCommandState& st, double wx, double wy, float tolWorld,
                          const ray3d::Ray& pickRay, std::vector<std::string>& log) {
  auto typeName = [](SelectedEntity::Type t) -> const char* {
    switch (t) {
      case SelectedEntity::Type::Circle: return "circle";
      case SelectedEntity::Type::Arc: return "arc";
      case SelectedEntity::Type::Ellipse: return "ellipse";
      case SelectedEntity::Type::Spline: return "spline";
      case SelectedEntity::Type::Polyline: return "polyline";
      default: return "object";
    }
  };
  auto isSupportedCutter = [](SelectedEntity::Type t) {
    return t == SelectedEntity::Type::LineSeg || t == SelectedEntity::Type::Circle ||
           t == SelectedEntity::Type::Arc || t == SelectedEntity::Type::Ellipse ||
           t == SelectedEntity::Type::Polyline;
  };

  SelectedEntity hit{};
  float hd2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, tolWorld, &hit, &hd2, &pickRay)) {
    SelectedEntity under{};
    float ud2 = 0.f;
    if (PickClosestCadEntity(st, wx, wy, tolWorld, &under, &ud2, &pickRay) &&
        under.type == SelectedEntity::Type::FeatureLine)
      log.push_back("TRIM — 1 feature line ignored: trimming one has no defined elevation for the "
                    "new end.");
    else
      log.push_back("TRIM — nothing to trim at pick.");
    return false;
  }
  if (hit.type != SelectedEntity::Type::LineSeg && hit.type != SelectedEntity::Type::Polyline) {
    log.push_back(std::string("TRIM — 3D ") + typeName(hit.type) +
                  " targets not yet supported in an orbited view or non-world UCS; refused.");
    return false;
  }
  const auto* trimSess = st.sessionAs<stepengine::TrimSession>();
  if (!trimSess)
    return false;
  for (const SelectedEntity& c : trimSess->cutters) {
    if (!isSupportedCutter(c.type)) {
      log.push_back(std::string("TRIM — 3D ") + typeName(c.type) +
                    " cutting edges not yet supported in an orbited view or non-world UCS; refused.");
      return false;
    }
  }

  // issue #399 increment 3: a Polyline target resolves to the nearest STRAIGHT chord (same limit
  // the 2D path already has, \ref FindNearestPolylineSegment3D); a Line target keeps using its own
  // two endpoints directly, unchanged from increments 1-2.
  const bool targetIsPoly = hit.type == SelectedEntity::Type::Polyline;
  size_t tk = 0;
  int targetVi = -1;
  ray3d::Vec3 ta{}, tb{};
  if (targetIsPoly) {
    if (!FindNearestPolylineSegment3D(st, hit.index, wx, wy, &pickRay, &targetVi, &ta, &tb)) {
      log.push_back("TRIM — nothing to trim at pick.");
      return false;
    }
  } else {
    tk = static_cast<size_t>(hit.index);
    if (tk >= st.drawing.lineCount()) {
      log.push_back("TRIM — nothing to trim at pick.");
      return false;
    }
    const CadLineRecord tl = st.drawing.line(tk);
    ta = {tl.x0, tl.y0, tl.z0};
    tb = {tl.x1, tl.y1, tl.z1};
  }
  const double targetLen = ray3d::Length(ray3d::Sub(tb, ta));
  if (targetLen < 1e-9) {
    log.push_back("TRIM — degenerate segment.");
    return false;
  }

  double epsGeom = 1e-5;
  double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    epsGeom = std::max(1e-5, 1e-4 * std::max(mxX - mnX, mxY - mnY));
  const double epsT = std::clamp(epsGeom / targetLen, 1e-9, 0.05);

  std::vector<double> ts;
  Collect3DTrimCrossings(st, trimSess->cutters, ta, tb, targetIsPoly, hit.index, targetVi, epsGeom, epsT, &ts);
  if (ts.empty()) {
    log.push_back("TRIM — segment does not cross a cutting edge.");
    return false;
  }

  // Pick side: the point on the target line's own infinite extension closest to the pick RAY
  // (issue #386's own ray-line closest-approach approach), not the flattened 2D cursor position —
  // the ray may cross the work plane far from where it actually points at an elevated/tilted line.
  const ray3d::Vec3 lineDir = ray3d::Sub(tb, ta);
  bool degenerate = false;
  const ray3d::Vec3 pickOnLine = ray3d::ClosestPointOnLineToRay(pickRay, ta, lineDir, &degenerate);
  const double lenSq = ray3d::Dot(lineDir, lineDir);
  double u = degenerate || lenSq < 1e-24
                ? 0.5
                : ray3d::Dot(ray3d::Sub(pickOnLine, ta), lineDir) / lenSq;
  u = std::clamp(u, 0.0, 1.0);

  double tNear = ts.front();
  double bestAbs = std::fabs(ts.front() - u);
  for (double t : ts) {
    const double d = std::fabs(t - u);
    if (d < bestAbs - 1e-12) {
      bestAbs = d;
      tNear = t;
    }
  }

  const ray3d::Vec3 cut = ray3d::Add(ta, ray3d::Scale(lineDir, tNear));
  Apply3DTrimCut(st, hit, targetIsPoly, tk, targetVi, cut, /*trimA=*/u < tNear, log);
  return true;
}

/// issue #399 increment 4: the resolved outcome of a smart (drawn-line) TRIM in true 3D — shared by
/// the commit (\ref Try3DDrawnLineTrim) and the orbited-view preview
/// (\ref CadTrimAppendCutLineRemovedPreview3D) so both read the same target and cut point.
struct DrawnLineTrimSolution {
  SelectedEntity hit{};
  bool targetIsPoly = false;
  size_t tk = 0;          ///< line index (Line target); unused for a polyline target
  int targetVi = -1;      ///< picked chord's near vertex (polyline target)
  ray3d::Vec3 ta{}, tb{}; ///< the target segment
  ray3d::Vec3 cut{};      ///< the 3D crossing the trim shortens to
  bool trimA = false;     ///< true = move the ta end, false = move the tb end
};

/// issue #399 increment 4: solve a smart TRIM (TRIMSTATE 0 — two clicks draw a line across the
/// drawing) in true 3D. The pre-existing path (\ref ExecuteDrawnSegmentTrimOnce) is flat world-XY
/// throughout: it drops the Z of both drawn points and of every candidate edge, so under an orbited
/// camera or a non-world UCS it picks the wrong target and trims to a bogus XY-projected crossing
/// (issue #399's screenshot — the drawn stroke lands on the ground plane, far from the cursor and
/// geometry). Given the drawn segment \p f0 -> \p f1 with real elevation, this resolves the operation
/// the same way the classic "click the piece to remove" path's \ref Try3DLineTrim does: the target
/// is the Line or straight polyline chord whose true 3D closest approach to the drawn segment is
/// smallest (within the same match tolerance the 2D path uses), crossings are found in 3D
/// (\ref Collect3DTrimCrossings against the whole drawing), and the removed portion is the one
/// containing the drawn line's midpoint — the 3D form of the 2D fence rule. Entity coverage matches
/// increments 1-3 exactly: Line / straight-polyline-chord target; Line, coplanar Circle/Arc/Ellipse,
/// or polyline-chord cutters. \p log (optional — null for the preview, which must not push chat
/// lines) receives the same refusal messages the 2D path emits. Returns false with nothing written
/// to \p out when there is no trim to make.
static bool Solve3DDrawnLineTrim(const AppCommandState& st, const ray3d::Vec3& f0, const ray3d::Vec3& f1,
                                 float tolWorld, DrawnLineTrimSolution* out, std::vector<std::string>* log) {
  auto note = [&](const char* m) {
    if (log)
      log->push_back(m);
  };
  const ray3d::Vec3 fdir = ray3d::Sub(f1, f0);
  const double fLen2 = ray3d::Dot(fdir, fdir);
  if (fLen2 < 1e-18) {
    note("TRIM — line too short.");
    return false;
  }

  double epsGeom = 1e-5;
  double extent = 0.;
  double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY)) {
    extent = std::max(mxX - mnX, mxY - mnY);
    epsGeom = std::max(1e-5, 1e-4 * extent);
  }
  // The same target-match tolerance ExecuteDrawnSegmentTrimOnce uses, now measured in 3D.
  double matchTol = std::max(static_cast<double>(tolWorld) * 4.0, 1e-6);
  matchTol = std::max(matchTol, 2e-5 * extent);
  const double matchTol2 = matchTol * matchTol;

  bool haveTarget = false;
  double bestGap2 = 0.;
  bool targetIsPoly = false;
  int targetIndex = -1;
  int targetVi = -1;
  ray3d::Vec3 ta{}, tb{};
  auto consider = [&](bool isPoly, int idx, int vi, const ray3d::Vec3& a, const ray3d::Vec3& b) {
    double s = 0., t = 0.;
    const double gap2 = SegSegClosest3D(f0, f1, a, b, &s, &t);
    if (gap2 > matchTol2)
      return;
    if (haveTarget && gap2 >= bestGap2 - 1e-12)
      return;
    haveTarget = true;
    bestGap2 = gap2;
    targetIsPoly = isPoly;
    targetIndex = idx;
    targetVi = vi;
    ta = a;
    tb = b;
  };
  for (size_t li = 0; li < st.drawing.lineCount(); ++li) {
    const CadLineRecord ln = st.drawing.line(li);
    consider(false, static_cast<int>(li), -1, ray3d::Vec3{ln.x0, ln.y0, ln.z0}, ray3d::Vec3{ln.x1, ln.y1, ln.z1});
  }
  const int nPoly =
      static_cast<int>(st.drawing.userPolylineOffsets.size() > 0 ? st.drawing.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi) + 1];
    const bool closed =
        static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
    auto tryEdge = [&](int vi) {
      const size_t a3 = static_cast<size_t>(vi), b3 = (static_cast<size_t>(vi)+ 1);
      if (b3 >= st.drawing.vertexCount())
        return;
      consider(true, pi, vi,
               ray3d::Vec3{st.drawing.vertex(a3).x, st.drawing.vertex(a3).y, st.drawing.vertex(a3).z},
               ray3d::Vec3{st.drawing.vertex(b3).x, st.drawing.vertex(b3).y, st.drawing.vertex(b3).z});
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      tryEdge(vi);
    if (closed && v1 - v0 >= 2)
      tryEdge(v1 - 1);
  }
  if (!haveTarget) {
    note("TRIM — no segment close enough to your line (draw along the edge to shorten).");
    return false;
  }

  const double targetLen = ray3d::Length(ray3d::Sub(tb, ta));
  if (targetLen < 1e-9) {
    note("TRIM — degenerate segment.");
    return false;
  }
  const double epsT = std::clamp(epsGeom / targetLen, 1e-9, 0.05);

  // Cutters: the whole drawing. Collect3DTrimCrossings excludes the target's own segment (and, for a
  // polyline target, only that one chord — its siblings stay valid cutters, matching the 2D path).
  std::vector<SelectedEntity> cutters;
  auto addAll = [&](SelectedEntity::Type type, int count) {
    for (int i = 0; i < count; ++i) {
      SelectedEntity e{};
      e.type = type;
      e.index = i;
      cutters.push_back(e);
    }
  };
  addAll(SelectedEntity::Type::LineSeg, static_cast<int>(st.drawing.lineCount()));
  addAll(SelectedEntity::Type::Circle, static_cast<int>(st.drawing.circleCount()));
  addAll(SelectedEntity::Type::Arc, static_cast<int>(st.drawing.userArcs.size()));
  addAll(SelectedEntity::Type::Ellipse, static_cast<int>(st.drawing.userEllipses.size()));
  addAll(SelectedEntity::Type::Polyline, nPoly);

  std::vector<double> ts;
  Collect3DTrimCrossings(st, cutters, ta, tb, targetIsPoly, targetIndex, targetVi, epsGeom, epsT, &ts);
  if (ts.empty()) {
    note("TRIM — nothing crosses that segment.");
    return false;
  }

  // Which crossing, and which side: the drawn line picks the crossing nearest to it (3D
  // point-to-segment distance), and the portion CONTAINING the drawn line's midpoint is removed —
  // the 3D form of ExecuteDrawnSegmentTrimOnce's fence rule.
  const ray3d::Vec3 tdir = ray3d::Sub(tb, ta);
  const double tLen2 = ray3d::Dot(tdir, tdir);
  const ray3d::Vec3 fmid = ray3d::Scale(ray3d::Add(f0, f1), 0.5);
  const double u = std::clamp(ray3d::Dot(ray3d::Sub(fmid, ta), tdir) / tLen2, 0.0, 1.0);

  auto distToFence2 = [&](const ray3d::Vec3& p) {
    const double k = std::clamp(ray3d::Dot(ray3d::Sub(p, f0), fdir) / fLen2, 0.0, 1.0);
    const ray3d::Vec3 g = ray3d::Sub(p, ray3d::Add(f0, ray3d::Scale(fdir, k)));
    return ray3d::Dot(g, g);
  };
  double tNear = ts.front();
  double bestFence2 = std::numeric_limits<double>::max();
  double bestPickAbs = std::numeric_limits<double>::max();
  for (double tt : ts) {
    const ray3d::Vec3 p = ray3d::Add(ta, ray3d::Scale(tdir, tt));
    const double df2 = distToFence2(p);
    const double dp = std::fabs(tt - u);
    if (df2 < bestFence2 - 1e-18 || (std::fabs(df2 - bestFence2) <= 1e-18 && dp < bestPickAbs - 1e-12)) {
      bestFence2 = df2;
      bestPickAbs = dp;
      tNear = tt;
    }
  }

  out->hit.type = targetIsPoly ? SelectedEntity::Type::Polyline : SelectedEntity::Type::LineSeg;
  out->hit.index = targetIndex;
  out->targetIsPoly = targetIsPoly;
  out->tk = targetIsPoly ? 0 : static_cast<size_t>(targetIndex);
  out->targetVi = targetVi;
  out->ta = ta;
  out->tb = tb;
  out->cut = ray3d::Add(ta, ray3d::Scale(tdir, tNear));
  out->trimA = u < tNear;
  return true;
}

/// issue #399 increment 4: commit a smart (drawn-line) TRIM resolved in true 3D. Plan view keeps the
/// byte-identical 2D path — \ref SubmitTrimViewportPick only routes here when a valid pick ray is
/// supplied.
static void Try3DDrawnLineTrim(AppCommandState& st, const ray3d::Vec3& f0, const ray3d::Vec3& f1,
                               float tolWorld, std::vector<std::string>& log) {
  DrawnLineTrimSolution s;
  if (!Solve3DDrawnLineTrim(st, f0, f1, tolWorld, &s, &log))
    return;
  Apply3DTrimCut(st, s.hit, s.targetIsPoly, s.tk, s.targetVi, s.cut, s.trimA, log);
}
