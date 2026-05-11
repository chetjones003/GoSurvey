#include "CadSnap.hpp"

#include <algorithm>
#include <cmath>

namespace CadSnap {

float WorldToleranceFromPixels(float viewportHeightPx, float orthoHalfHeightWorld, float pixels) {
  const float vph = std::max(viewportHeightPx, 1.f);
  const float worldPerPixel = (2.f * orthoHalfHeightWorld) / vph;
  return pixels * worldPerPixel;
}

namespace {

void Consider(float wx, float wy, float px, float py, Kind kind, float tolWorld, Hit* best,
              float* bestDistSq, int* bestPri) {
  const float dx = px - wx;
  const float dy = py - wy;
  const float d2 = dx * dx + dy * dy;
  const float tol2 = tolWorld * tolWorld;
  if (d2 > tol2)
    return;
  const int pri = Priority(kind);
  if (!best->valid || d2 < *bestDistSq - 1.e-12f ||
      (std::abs(d2 - *bestDistSq) <= 1.e-12f * tol2 && pri > *bestPri)) {
    best->valid = true;
    best->kind = kind;
    best->x = px;
    best->y = py;
    *bestDistSq = d2;
    *bestPri = pri;
  }
}

/// Foot of perpendicular from \p ref onto segment AB (clamped). Cursor \p wx,\p wy only gates distance.
void AppendPerpendicularFromRef(float refX, float refY, float wx, float wy, float ax, float ay, float bx,
                                float by, float tolWorld, Hit* best, float* bestDistSq, int* bestPri) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1.e-12f)
    return;
  // Q = A + t*v with (ref - Q)·v = 0 on the infinite line => t = ((ref - A)·v) / (v·v)
  float t = ((refX - ax) * vx + (refY - ay) * vy) / len2;
  t = std::clamp(t, 0.f, 1.f);
  const float qx = ax + t * vx;
  const float qy = ay + t * vy;
  Consider(wx, wy, qx, qy, Kind::Perpendicular, tolWorld, best, bestDistSq, bestPri);
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

Hit FindBest(float wx, float wy, const AppCommandState& cmd, bool commandActive, float tolWorld) {
  Hit best{};
  float bestDistSq = 0.f;
  int bestPri = -1;

  float refPx = 0.f;
  float refPy = 0.f;
  const bool havePerpRef = commandActive && PerpendicularReference(cmd, &refPx, &refPy);

  const auto& L = cmd.userLinesFlat;
  if (L.size() % 6 == 0) {
    for (size_t i = 0; i + 5 < L.size(); i += 6) {
      const float x0 = L[i];
      const float y0 = L[i + 1];
      const float x1 = L[i + 3];
      const float y1 = L[i + 4];
      Consider(wx, wy, x0, y0, Kind::Endpoint, tolWorld, &best, &bestDistSq, &bestPri);
      Consider(wx, wy, x1, y1, Kind::Endpoint, tolWorld, &best, &bestDistSq, &bestPri);
      Consider(wx, wy, 0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, tolWorld, &best,
               &bestDistSq, &bestPri);
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, x0, y0, x1, y1, tolWorld, &best, &bestDistSq,
                                   &bestPri);
    }
  }

  const auto& C = cmd.userCirclesCxCyR;
  if (C.size() % 3 == 0) {
    for (size_t i = 0; i + 2 < C.size(); i += 3)
      Consider(wx, wy, C[i], C[i + 1], Kind::Center, tolWorld, &best, &bestDistSq, &bestPri);
  }

  const int polyCount =
      static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < polyCount; ++pi) {
    const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const bool closed =
        static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
    auto considerEdge = [&](int ia, int ib) {
      const float ax = cmd.userPolylineVerts[static_cast<size_t>(ia * 3)];
      const float ay = cmd.userPolylineVerts[static_cast<size_t>(ia * 3 + 1)];
      const float bx = cmd.userPolylineVerts[static_cast<size_t>(ib * 3)];
      const float by = cmd.userPolylineVerts[static_cast<size_t>(ib * 3 + 1)];
      Consider(wx, wy, ax, ay, Kind::Endpoint, tolWorld, &best, &bestDistSq, &bestPri);
      Consider(wx, wy, bx, by, Kind::Endpoint, tolWorld, &best, &bestDistSq, &bestPri);
      Consider(wx, wy, 0.5f * (ax + bx), 0.5f * (ay + by), Kind::Midpoint, tolWorld, &best, &bestDistSq,
               &bestPri);
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, ax, ay, bx, by, tolWorld, &best, &bestDistSq, &bestPri);
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      considerEdge(vi, vi + 1);
    if (closed && v1 - v0 >= 2)
      considerEdge(v1 - 1, v0);
  }

  constexpr int kArcSnapSeg = 24;
  for (const CadArc& a : cmd.userArcs) {
    if (a.r <= 1e-6f || kArcSnapSeg < 1)
      continue;
    const float tStart = a.startRad;
    const float tEnd = a.startRad + a.sweepRad;
    Consider(wx, wy, a.cx + a.r * std::cos(tStart), a.cy + a.r * std::sin(tStart), Kind::Endpoint, tolWorld, &best,
             &bestDistSq, &bestPri);
    Consider(wx, wy, a.cx + a.r * std::cos(tEnd), a.cy + a.r * std::sin(tEnd), Kind::Endpoint, tolWorld, &best,
             &bestDistSq, &bestPri);
    for (int i = 0; i < kArcSnapSeg; ++i) {
      const float u0 = static_cast<float>(i) / static_cast<float>(kArcSnapSeg);
      const float u1 = static_cast<float>(i + 1) / static_cast<float>(kArcSnapSeg);
      const float t0 = a.startRad + a.sweepRad * u0;
      const float t1 = a.startRad + a.sweepRad * u1;
      const float x0 = a.cx + a.r * std::cos(t0);
      const float y0 = a.cy + a.r * std::sin(t0);
      const float x1 = a.cx + a.r * std::cos(t1);
      const float y1 = a.cy + a.r * std::sin(t1);
      Consider(wx, wy, 0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, tolWorld, &best, &bestDistSq, &bestPri);
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, x0, y0, x1, y1, tolWorld, &best, &bestDistSq, &bestPri);
    }
  }

  constexpr int kEllSnapSeg = 36;
  constexpr float kTwoPi = 6.28318530718f;
  for (const CadEllipse& el : cmd.userEllipses) {
    const float ma = std::hypot(el.majVx, el.majVy);
    if (ma < 1e-8f || kEllSnapSeg < 3)
      continue;
    Consider(wx, wy, el.cx, el.cy, Kind::Center, tolWorld, &best, &bestDistSq, &bestPri);
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
      Consider(wx, wy, 0.5f * (x0 + x1), 0.5f * (y0 + y1), Kind::Midpoint, tolWorld, &best, &bestDistSq, &bestPri);
      if (havePerpRef)
        AppendPerpendicularFromRef(refPx, refPy, wx, wy, x0, y0, x1, y1, tolWorld, &best, &bestDistSq, &bestPri);
    }
  }

  for (const SurveyPoint& sp : cmd.surveyPoints)
    Consider(wx, wy, sp.easting, sp.northing, Kind::SurveyCenter, tolWorld, &best, &bestDistSq, &bestPri);

  return best;
}

} // namespace CadSnap
