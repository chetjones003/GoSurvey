#include "CadSnap.hpp"

#include "SurveyPoints.hpp"
#include "geom2d.hpp"

#include <algorithm>
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

struct SnapPickAccum {
  Hit best{};
  float bestPickDistSq = 0.f;
  int bestPri = -1;
  float bestTieDistSq = 0.f;
};

void ConsiderSnap(SnapPickAccum* acc, float wx, float wy, float snapX, float snapY, Kind kind, float pickDistSq,
                  float tolWorld) {
  const float tol2 = tolWorld * tolWorld;
  if (!(pickDistSq <= tol2) || pickDistSq > 1.e28f)
    return;
  const int pri = Priority(kind);
  const float tie = (snapX - wx) * (snapX - wx) + (snapY - wy) * (snapY - wy);
  const float eps = 1.e-9f * std::max(tol2, 1.f);
  if (!acc->best.valid) {
    acc->best.valid = true;
    acc->best.kind = kind;
    acc->best.x = snapX;
    acc->best.y = snapY;
    acc->bestPickDistSq = pickDistSq;
    acc->bestPri = pri;
    acc->bestTieDistSq = tie;
    return;
  }
  if (pickDistSq < acc->bestPickDistSq - eps) {
    acc->best.kind = kind;
    acc->best.x = snapX;
    acc->best.y = snapY;
    acc->bestPickDistSq = pickDistSq;
    acc->bestPri = pri;
    acc->bestTieDistSq = tie;
    return;
  }
  if (std::fabs(pickDistSq - acc->bestPickDistSq) > eps)
    return;
  if (pri > acc->bestPri || (pri == acc->bestPri && tie < acc->bestTieDistSq - 1.e-12f)) {
    acc->best.kind = kind;
    acc->best.x = snapX;
    acc->best.y = snapY;
    acc->bestPri = pri;
    acc->bestTieDistSq = tie;
  }
}

void Consider(SnapPickAccum* acc, float wx, float wy, float px, float py, Kind kind, float tolWorld) {
  const float dx = px - wx;
  const float dy = py - wy;
  ConsiderSnap(acc, wx, wy, px, py, kind, dx * dx + dy * dy, tolWorld);
}

/// Foot of perpendicular from \p ref onto segment AB (clamped). Cursor \p wx,\p wy only gates distance.
void AppendPerpendicularFromRef(float refX, float refY, float wx, float wy, float ax, float ay, float bx, float by,
                                float tolWorld, SnapPickAccum* acc) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1.e-12f)
    return;
  float t = ((refX - ax) * vx + (refY - ay) * vy) / len2;
  t = std::clamp(t, 0.f, 1.f);
  const float qx = ax + t * vx;
  const float qy = ay + t * vy;
  Consider(acc, wx, wy, qx, qy, Kind::Perpendicular, tolWorld);
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

} // namespace

Hit FindBest(double wx, double wy, const AppCommandState& cmd, bool commandActive, float tolWorld,
             SnapExclude exclude) {
  SnapPickAccum acc{};

  float refPx = 0.f;
  float refPy = 0.f;
  const bool havePerpRef = commandActive && cmd.objectSnapPerpendicular && PerpendicularReference(cmd, &refPx, &refPy);

  const auto& L = cmd.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      if (exclude.valid && exclude.type == SelectedEntity::Type::LineSeg &&
          exclude.index == static_cast<int>(i / 6)) continue;
      const float x0 = L[i];
      const float y0 = L[i + 1];
      const float x1 = L[i + 3];
      const float y1 = L[i + 4];
      if (cmd.objectSnapEndpoint) {
        Consider(&acc, wx, wy, x0, y0, Kind::Endpoint, tolWorld);
        Consider(&acc, wx, wy, x1, y1, Kind::Endpoint, tolWorld);
      }
      if (cmd.objectSnapMidpoint)
        Consider(&acc, wx, wy, 0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, tolWorld);
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, x0, y0, x1, y1, tolWorld, &acc);
    }
  }

  const auto& C = cmd.userCirclesCxCyR;
  if (C.size() % 3 == 0 && cmd.objectSnapCenter) {
    for (size_t i = 0; i + 2 < C.size(); i += 3) {
      if (exclude.valid && exclude.type == SelectedEntity::Type::Circle &&
          exclude.index == static_cast<int>(i / 3)) continue;
      const float cx = C[i];
      const float cy = C[i + 1];
      const float r = C[i + 2];
      const float p2 = CircleCenterPickDistSq(wx, wy, cx, cy, r, tolWorld);
      ConsiderSnap(&acc, wx, wy, cx, cy, Kind::Center, p2, tolWorld);
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
      if (cmd.objectSnapEndpoint) {
        Consider(&acc, wx, wy, ax, ay, Kind::Endpoint, tolWorld);
        Consider(&acc, wx, wy, bx, by, Kind::Endpoint, tolWorld);
      }
      if (cmd.objectSnapMidpoint)
        Consider(&acc, wx, wy, 0.5f * (ax + bx), 0.5f * (ay + by), Kind::Midpoint, tolWorld);
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, ax, ay, bx, by, tolWorld, &acc);
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      considerEdge(vi, vi + 1);
    if (closed && v1 - v0 >= 2)
      considerEdge(v1 - 1, v0);

    if (cmd.objectSnapGeometricCenter && closed && v1 - v0 >= 3) {
      float gcx = 0.f;
      float gcy = 0.f;
      if (ClosedPolylineCentroid(cmd.userPolylineVerts, v0, v1, &gcx, &gcy)) {
        const float p2 = ClosedPolyGeometricPickDistSq(wx, wy, cmd.userPolylineVerts, v0, v1);
        ConsiderSnap(&acc, wx, wy, gcx, gcy, Kind::GeometricCenter, p2, tolWorld);
      }
    }
  }

  constexpr int kArcSnapSeg = 24;
  for (size_t arcIdx = 0; arcIdx < cmd.userArcs.size(); ++arcIdx) {
    if (exclude.valid && exclude.type == SelectedEntity::Type::Arc &&
        exclude.index == static_cast<int>(arcIdx)) continue;
    const CadArc& a = cmd.userArcs[arcIdx];
    if (a.r <= 1e-6f || kArcSnapSeg < 1)
      continue;
    const double dcx = static_cast<double>(a.cx);
    const double dcy = static_cast<double>(a.cy);
    const double dr = static_cast<double>(a.r);
    const double tEnd = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad);
    if (cmd.objectSnapEndpoint) {
      double ex = 0.;
      double ey = 0.;
      CirclePointWorld(dcx, dcy, dr, static_cast<double>(a.startRad), &ex, &ey);
      Consider(&acc, wx, wy, static_cast<float>(ex), static_cast<float>(ey), Kind::Endpoint, tolWorld);
      CirclePointWorld(dcx, dcy, dr, tEnd, &ex, &ey);
      Consider(&acc, wx, wy, static_cast<float>(ex), static_cast<float>(ey), Kind::Endpoint, tolWorld);
    }
    for (int i = 0; i < kArcSnapSeg; ++i) {
      const double u0 = static_cast<double>(i) / static_cast<double>(kArcSnapSeg);
      const double u1 = static_cast<double>(i + 1) / static_cast<double>(kArcSnapSeg);
      const double t0 = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad) * u0;
      const double t1 = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad) * u1;
      double x0 = 0.;
      double y0 = 0.;
      double x1 = 0.;
      double y1 = 0.;
      CirclePointWorld(dcx, dcy, dr, t0, &x0, &y0);
      CirclePointWorld(dcx, dcy, dr, t1, &x1, &y1);
      if (cmd.objectSnapMidpoint)
        Consider(&acc, wx, wy, static_cast<float>(0.5 * (x0 + x1)), static_cast<float>(0.5 * (y0 + y1)), Kind::Midpoint,
                 tolWorld);
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, static_cast<float>(x0), static_cast<float>(y0),
                                   static_cast<float>(x1), static_cast<float>(y1), tolWorld, &acc);
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
    if (cmd.objectSnapCenter) {
      const float p2 = EllipseCenterPickDistSq(wx, wy, el, tolWorld);
      ConsiderSnap(&acc, wx, wy, el.cx, el.cy, Kind::Center, p2, tolWorld);
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
      if (cmd.objectSnapMidpoint)
        Consider(&acc, wx, wy, 0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, tolWorld);
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, x0, y0, x1, y1, tolWorld, &acc);
    }
  }

  if (cmd.objectSnapSurveyPoint) {
    const float arm =
        SurveyPointCrossHalfWorldFromPaper(cmd.surveyPointCrossSpanPlottedInches, cmd.modelUnitsPerPlottedInch);
    for (const SurveyPoint& sp : cmd.surveyPoints) {
      const float p2 = MinDistSqToSurveyMarker(wx, wy, sp.easting, sp.northing, arm);
      ConsiderSnap(&acc, wx, wy, sp.easting, sp.northing, Kind::SurveyCenter, p2, tolWorld);
    }
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
      if (att.snapLines && cmd.objectSnapEndpoint) {
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
      if (att.snapLines && cmd.objectSnapMidpoint) {
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
      if (att.snapCircles && cmd.objectSnapCenter) {
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

bool CommandHasPerpendicularSnapReference(const AppCommandState& cmd, bool commandActive) {
  if (!commandActive || !cmd.objectSnapPerpendicular)
    return false;
  float rx = 0.f;
  float ry = 0.f;
  return PerpendicularReference(cmd, &rx, &ry);
}

void PushSnapPickerEntry(float px, float py, Kind kind, float sortWx, float sortWy,
                         std::vector<SnapCandidateEntry>& out) {
  SnapCandidateEntry e;
  e.hit.valid = true;
  e.hit.kind = kind;
  e.hit.x = px;
  e.hit.y = py;
  const float dx = px - sortWx;
  const float dy = py - sortWy;
  e.distSq = dx * dx + dy * dy;
  out.push_back(e);
}

void PushPerpFootEntry(float refX, float refY, float ax, float ay, float bx, float by, float sortWx, float sortWy,
                       std::vector<SnapCandidateEntry>& out) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1.e-12f)
    return;
  float t = ((refX - ax) * vx + (refY - ay) * vy) / len2;
  t = std::clamp(t, 0.f, 1.f);
  const float qx = ax + t * vx;
  const float qy = ay + t * vy;
  PushSnapPickerEntry(qx, qy, Kind::Perpendicular, sortWx, sortWy, out);
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
        PushSnapPickerEntry(L[i], L[i + 1], Kind::Endpoint, sortWorldX, sortWorldY, out);
        PushSnapPickerEntry(L[i + 3], L[i + 4], Kind::Endpoint, sortWorldX, sortWorldY, out);
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
        PushSnapPickerEntry(ax, ay, Kind::Endpoint, sortWorldX, sortWorldY, out);
        PushSnapPickerEntry(bx, by, Kind::Endpoint, sortWorldX, sortWorldY, out);
      };
      for (int vi = v0; vi + 1 < v1; ++vi)
        pushEdge(vi, vi + 1);
      if (closed && v1 - v0 >= 2)
        pushEdge(v1 - 1, v0);
    }
    for (const CadArc& a : cmd.userArcs) {
      if (a.r <= 1e-6f)
        continue;
      const float tEnd = a.startRad + a.sweepRad;
      PushSnapPickerEntry(a.cx + a.r * std::cos(a.startRad), a.cy + a.r * std::sin(a.startRad), Kind::Endpoint,
                          sortWorldX, sortWorldY, out);
      PushSnapPickerEntry(a.cx + a.r * std::cos(tEnd), a.cy + a.r * std::sin(tEnd), Kind::Endpoint, sortWorldX,
                          sortWorldY, out);
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
        PushSnapPickerEntry(0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, sortWorldX, sortWorldY, out);
      }
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
        PushSnapPickerEntry(0.5f * (ax + bx), 0.5f * (ay + by), Kind::Midpoint, sortWorldX, sortWorldY, out);
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
      const double dcx = static_cast<double>(a.cx);
      const double dcy = static_cast<double>(a.cy);
      const double dr = static_cast<double>(a.r);
      for (int i = 0; i < kArcSnapSeg; ++i) {
        const double u0 = static_cast<double>(i) / static_cast<double>(kArcSnapSeg);
        const double u1 = static_cast<double>(i + 1) / static_cast<double>(kArcSnapSeg);
        const double t0 = static_cast<double>(a.startRad + a.sweepRad * static_cast<float>(u0));
        const double t1 = static_cast<double>(a.startRad + a.sweepRad * static_cast<float>(u1));
        double x0 = 0.;
        double y0 = 0.;
        double x1 = 0.;
        double y1 = 0.;
        CirclePointWorld(dcx, dcy, dr, t0, &x0, &y0);
        CirclePointWorld(dcx, dcy, dr, t1, &x1, &y1);
        PushSnapPickerEntry(static_cast<float>(0.5 * (x0 + x1)), static_cast<float>(0.5 * (y0 + y1)), Kind::Midpoint,
                            sortWorldX, sortWorldY, out);
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
                            sortWorldX, sortWorldY, out);
      }
    }
    break;
  }
  case Kind::Center: {
    const auto& C = cmd.userCirclesCxCyR;
    if (C.size() % 3 == 0) {
      for (size_t i = 0; i + 2 < C.size(); i += 3)
        PushSnapPickerEntry(C[i], C[i + 1], Kind::Center, sortWorldX, sortWorldY, out);
    }
    for (const CadEllipse& el : cmd.userEllipses) {
      const float ma = std::hypot(el.majVx, el.majVy);
      if (ma < 1e-8f)
        continue;
      PushSnapPickerEntry(el.cx, el.cy, Kind::Center, sortWorldX, sortWorldY, out);
    }
    break;
  }
  case Kind::Perpendicular: {
    if (!havePerpRef)
      break;
    const auto& L = cmd.userLinesFlat;
    if (L.size() % 6 == 0) {
      for (size_t i = 0; i + 5 < L.size(); i += 6)
        PushPerpFootEntry(refPx, refPy, L[i], L[i + 1], L[i + 3], L[i + 4], sortWorldX, sortWorldY, out);
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
        PushPerpFootEntry(refPx, refPy, ax, ay, bx, by, sortWorldX, sortWorldY, out);
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
      for (int i = 0; i < kArcSnapSeg; ++i) {
        const float u0 = static_cast<float>(i) / static_cast<float>(kArcSnapSeg);
        const float u1 = static_cast<float>(i + 1) / static_cast<float>(kArcSnapSeg);
        const float t0 = a.startRad + a.sweepRad * u0;
        const float t1 = a.startRad + a.sweepRad * u1;
        const float x0 = a.cx + a.r * std::cos(t0);
        const float y0 = a.cy + a.r * std::sin(t0);
        const float x1 = a.cx + a.r * std::cos(t1);
        const float y1 = a.cy + a.r * std::sin(t1);
        PushPerpFootEntry(refPx, refPy, x0, y0, x1, y1, sortWorldX, sortWorldY, out);
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
        PushPerpFootEntry(refPx, refPy, x0, y0, x1, y1, sortWorldX, sortWorldY, out);
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
      const bool closed =
          static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
      if (!closed || v1 - v0 < 3)
        continue;
      float gcx = 0.f;
      float gcy = 0.f;
      if (ClosedPolylineCentroid(cmd.userPolylineVerts, v0, v1, &gcx, &gcy))
        PushSnapPickerEntry(gcx, gcy, Kind::GeometricCenter, sortWorldX, sortWorldY, out);
    }
    break;
  }
  case Kind::SurveyCenter:
    for (const SurveyPoint& sp : cmd.surveyPoints)
      PushSnapPickerEntry(sp.easting, sp.northing, Kind::SurveyCenter, sortWorldX, sortWorldY, out);
    break;
  case Kind::Grip:
    break; // grip snap points are per-selection, not gathered globally
  }
  SortDedupeSnapPicker(out);
}

Hit FindGripSnap(double wx, double wy, const AppCommandState& cmd, float tolWorld) {
  SnapPickAccum acc{};
  const float tol2 = tolWorld * tolWorld;

  auto gripCandidate = [&](float gx, float gy) {
    const float dx = gx - static_cast<float>(wx);
    const float dy = gy - static_cast<float>(wy);
    if (dx * dx + dy * dy <= tol2)
      ConsiderSnap(&acc, wx, wy, gx, gy, Kind::Grip, 0.f, tolWorld);
  };

  // CAD entity grips
  for (const SelectedEntity& sel : cmd.selection) {
    if (sel.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(sel.index) * 6;
      if (k + 5 < cmd.userLinesFlat.size()) {
        gripCandidate(cmd.userLinesFlat[k],     cmd.userLinesFlat[k + 1]);
        gripCandidate(cmd.userLinesFlat[k + 3], cmd.userLinesFlat[k + 4]);
      }
    } else if (sel.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(sel.index) * 3;
      if (k + 2 < cmd.userCirclesCxCyR.size()) {
        const float cx = cmd.userCirclesCxCyR[k];
        const float cy = cmd.userCirclesCxCyR[k + 1];
        gripCandidate(cx, cy);
        gripCandidate(cx + cmd.userCirclesCxCyR[k + 2], cy);
      }
    } else if (sel.type == SelectedEntity::Type::Polyline) {
      const int np = static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
      if (sel.index >= 0 && sel.index < np) {
        const int startV = cmd.userPolylineOffsets[static_cast<size_t>(sel.index)];
        const int endV   = cmd.userPolylineOffsets[static_cast<size_t>(sel.index + 1)];
        for (int vi = 0; vi < endV - startV; ++vi) {
          const size_t xIdx = static_cast<size_t>(startV + vi) * 3;
          if (xIdx + 1 >= cmd.userPolylineVerts.size()) break;
          gripCandidate(cmd.userPolylineVerts[xIdx], cmd.userPolylineVerts[xIdx + 1]);
        }
      }
    } else if (sel.type == SelectedEntity::Type::Arc) {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userArcs.size()) {
        const CadArc& a = cmd.userArcs[static_cast<size_t>(sel.index)];
        const float endRad = a.startRad + a.sweepRad;
        gripCandidate(a.cx, a.cy);
        gripCandidate(a.cx + a.r * std::cos(a.startRad), a.cy + a.r * std::sin(a.startRad));
        gripCandidate(a.cx + a.r * std::cos(endRad),     a.cy + a.r * std::sin(endRad));
      }
    } else if (sel.type == SelectedEntity::Type::Ellipse) {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.userEllipses.size()) {
        const CadEllipse& el = cmd.userEllipses[static_cast<size_t>(sel.index)];
        const float perpX = -el.majVy, perpY = el.majVx;
        gripCandidate(el.cx, el.cy);
        gripCandidate(el.cx + el.majVx,            el.cy + el.majVy);
        gripCandidate(el.cx + perpX * el.ratio,     el.cy + perpY * el.ratio);
      }
    } else if (sel.type == SelectedEntity::Type::Annotation) {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.cadAnnotations.size()) {
        const CadAnnotation& a = cmd.cadAnnotations[static_cast<size_t>(sel.index)];
        if (a.kind == CadAnnotation::Kind::Mtext) {
          if (a.surveyPointLabelFor >= 0) {
            gripCandidate(0.5f * (a.boxMinX + a.boxMaxX), 0.5f * (a.boxMinY + a.boxMaxY));
          } else {
            gripCandidate(a.boxMinX, a.boxMinY);
            gripCandidate(a.boxMaxX, a.boxMinY);
            gripCandidate(a.boxMaxX, a.boxMaxY);
            gripCandidate(a.boxMinX, a.boxMaxY);
          }
        }
      }
    }
  }

  // Survey point grips (selected survey points)
  for (const int idx : cmd.selectedSurveyPointIndices) {
    if (idx >= 0 && static_cast<size_t>(idx) < cmd.surveyPoints.size())
      gripCandidate(cmd.surveyPoints[static_cast<size_t>(idx)].easting,
                    cmd.surveyPoints[static_cast<size_t>(idx)].northing);
  }

  return acc.best;
}

} // namespace CadSnap
