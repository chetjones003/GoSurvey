#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <vector>

#include <GoSurvey/Geometry/curveintersect.hpp>

// ================================================================================================
// REQ-103 FILLET (step 6a) / CHAMFER (step 6b) — pure tangent-arc / corner-point geometry.
//
// Inline and header-only (pure — only <cmath>/<vector> and curveintersect.hpp, both dependency-free)
// so this geometry is unit-testable (tests/FilletGeomTests.cpp) without linking CadCommands.cpp's
// whole command layer.
//
// The construction: offset both picked curves by the fillet radius (a Line translated along its own
// perpendicular; an Arc/Circle's own full circle grown/shrunk by the radius) and intersect every
// combination analytically via curveisect's existing IntersectSegSeg/IntersectSegConic/
// IntersectConicConic (REQ-062/REQ-101 — never tessellated); the candidate nearest both pick points
// (summed squared distance) is chosen. Radius 0 needs no special case — every offset collapses to
// the original curve, so the "candidate center" is simply the curves' own intersection. The actual
// trim/extend mutation afterward reuses LENGTHEN's ApplyLengthenToLine/ToArc/ToPolylineEnd
// unchanged, converting a known tangent point into the `newLength` those functions already accept.
// ================================================================================================

/// One of FILLET/CHAMFER's two picked curves, reduced to only what the geometry solve needs: a
/// Line's own infinite extension (isLine=true; ax,ay/bx,by are any two distinct points on it — a
/// Polyline segment's own two endpoints work identically), or an Arc/Circle's own FULL circle
/// (isLine=false; cx,cy,r) — always the full circle, never sweep-limited, matching \ref
/// FindExtendArcTarget's own reasoning: a tangent point beyond the arc's CURRENT sweep must still
/// be found so the arc can be extended (not just trimmed within its existing span) to reach it.
struct FilletCurve {
  bool isLine = true;
  float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
  float cx = 0.f, cy = 0.f, r = 0.f;
  // issue #373: a Line's own Z at each endpoint, in WORLD space (unused for Arc/Circle). Only
  // consulted by the two-line 3D fillet path — every other consumer keeps working entirely in
  // whatever 2D frame it was already given (world XY, or a projected plane's own 2D coordinates).
  float az0 = 0.f, az1 = 0.f;
};

/// A curveisect::Seg standing in for curve `(ax,ay)-(bx,by)`'s INFINITE extension. curveisect has no
/// infinite-line primitive (confirmed by EXTEND's own research note, CadCommands.cpp) — a generously
/// long finite query segment stands in instead, EXTEND's own `FindExtendLineTarget` technique,
/// extended in BOTH directions here (a fillet's tangent point may lie beyond either original
/// endpoint, unlike EXTEND's forward-only ray).
inline curveisect::Seg FilletLongLine(float ax, float ay, float bx, float by) {
  // Double precision throughout, matching curveisect::Vec2's own double storage — computing the
  // huge offset in float32 (the original bug, found via a real user report) loses ~0.01-0.05 units
  // of precision at typical drawing scales (a ~1e6-unit offset leaves float32 only ~7 significant
  // digits to place a coordinate that's normally two digits), which showed up as fillet arcs that
  // visibly did NOT touch their own trimmed line endpoints. Doubles keep this error near 1e-9.
  const double dax = ax, day = ay, dbx = bx, dby = by;
  const double dx = dbx - dax, dy = dby - day;
  const double len = std::hypot(dx, dy);
  if (len < 1e-9)
    return {{dax, day}, {dbx, dby}};
  const double ux = dx / len, uy = dy / len;
  constexpr double kHuge = 1.0e6;
  const double mx = 0.5 * (dax + dbx), my = 0.5 * (day + dby);
  return {{mx - ux * kHuge, my - uy * kHuge}, {mx + ux * kHuge, my + uy * kHuge}};
}

/// Exact infinite-line intersection (Cramer's rule, double precision throughout) — used for the
/// Line-Line candidate case instead of `FilletLongLine`'s "huge finite segment" approximation,
/// which curveisect's Seg-based functions still need for a Line vs Circle/Conic (no infinite-line
/// primitive exists there) but which two genuinely infinite lines never need at all: this is exact
/// regardless of the drawing's coordinate scale. False only when the two lines are parallel.
inline bool FilletLineLineIntersectInf(float ax, float ay, float bx, float by, float cx, float cy, float dx,
                                       float dy, float* outX, float* outY) {
  const double rx = static_cast<double>(bx) - ax, ry = static_cast<double>(by) - ay;
  const double sx = static_cast<double>(dx) - cx, sy = static_cast<double>(dy) - cy;
  const double det = rx * sy - ry * sx;
  if (std::fabs(det) < 1e-9 * std::max(1.0, std::hypot(rx, ry) * std::hypot(sx, sy)))
    return false;
  const double t = ((static_cast<double>(cx) - ax) * sy - (static_cast<double>(cy) - ay) * sx) / det;
  *outX = static_cast<float>(ax + t * rx);
  *outY = static_cast<float>(ay + t * ry);
  return true;
}

/// Every real candidate fillet-arc center for `c1`/`c2` offset by `radius` (up to 4 for two lines,
/// up to 8 for a line+arc or two arcs — see the section comment above for the construction).
inline void FilletCandidateCenters(const FilletCurve& c1, const FilletCurve& c2, float radius,
                                   std::vector<curveisect::Vec2>* out) {
  auto lineOffsetVariant = [](const FilletCurve& c, float signedR, float* ax, float* ay, float* bx,
                              float* by) {
    const float vx = c.bx - c.ax, vy = c.by - c.ay;
    const float len = std::hypot(vx, vy);
    float nx = 0.f, ny = 1.f;
    if (len > 1e-12f) {
      nx = -vy / len;
      ny = vx / len;
    }
    *ax = c.ax + nx * signedR;
    *ay = c.ay + ny * signedR;
    *bx = c.bx + nx * signedR;
    *by = c.by + ny * signedR;
  };
  auto lineOffsetVariantSeg = [](const FilletCurve& c, float signedR) -> curveisect::Seg {
    const float vx = c.bx - c.ax, vy = c.by - c.ay;
    const float len = std::hypot(vx, vy);
    float nx = 0.f, ny = 1.f;
    if (len > 1e-12f) {
      nx = -vy / len;
      ny = vx / len;
    }
    return FilletLongLine(c.ax + nx * signedR, c.ay + ny * signedR, c.bx + nx * signedR, c.by + ny * signedR);
  };
  const float signs[2] = {1.f, -1.f};
  if (c1.isLine && c2.isLine) {
    for (float s1 : signs) {
      float a1x = 0.f, a1y = 0.f, b1x = 0.f, b1y = 0.f;
      lineOffsetVariant(c1, radius * s1, &a1x, &a1y, &b1x, &b1y);
      for (float s2 : signs) {
        float a2x = 0.f, a2y = 0.f, b2x = 0.f, b2y = 0.f;
        lineOffsetVariant(c2, radius * s2, &a2x, &a2y, &b2x, &b2y);
        float ix = 0.f, iy = 0.f;
        if (FilletLineLineIntersectInf(a1x, a1y, b1x, b1y, a2x, a2y, b2x, b2y, &ix, &iy))
          out->push_back({ix, iy});
      }
    }
    return;
  }
  if (!c1.isLine && !c2.isLine) {
    for (float s1 : signs) {
      const float r1 = c1.r + radius * s1;
      if (r1 < 1e-6f)
        continue;
      const curveisect::Conic k1 = curveisect::MakeCircle(c1.cx, c1.cy, r1);
      for (float s2 : signs) {
        const float r2 = c2.r + radius * s2;
        if (r2 < 1e-6f)
          continue;
        const curveisect::Conic k2 = curveisect::MakeCircle(c2.cx, c2.cy, r2);
        std::vector<curveisect::Hit2> hits;
        curveisect::IntersectConicConic(k1, k2, &hits);
        for (const auto& h : hits)
          out->push_back(h.p);
      }
    }
    return;
  }
  const FilletCurve& lineC = c1.isLine ? c1 : c2;
  const FilletCurve& circC = c1.isLine ? c2 : c1;
  for (float sL : signs) {
    const curveisect::Seg l = lineOffsetVariantSeg(lineC, radius * sL);
    for (float sC : signs) {
      const float rC = circC.r + radius * sC;
      if (rC < 1e-6f)
        continue;
      const curveisect::Conic k = curveisect::MakeCircle(circC.cx, circC.cy, rC);
      std::vector<curveisect::Hit2> hits;
      curveisect::IntersectSegConic(l, k, &hits);
      for (const auto& h : hits)
        out->push_back(h.p);
    }
  }
}

/// True when `pt`, projected onto the segment from `nearX,nearY` to `farX,farY`, has NOT overshot
/// past the far endpoint (parametric t <= 1, with a small tolerance). FILLET's "radius is too
/// large" refusal (D-2026-08-25-b): a valid trim/extend target must stay within (or very near) the
/// curve's own original span from its near-the-corner endpoint out to its far endpoint — going past
/// the far endpoint means the requested radius needs more material than the curve actually has.
///
/// An EARLIER version of this same idea compared which of the two endpoints `pt` is nearer to
/// (Euclidean distance) instead of this parametric projection, and was wrong: a tangent point at
/// 60% of the way along a perfectly valid, comfortably-sized fillet is *already* nearer the far
/// endpoint than the near one (past the segment's own midpoint is completely ordinary), which that
/// version misread as "overshoot" and refused a legitimate, correctly-sized fillet — caught by a
/// regression test built from the same user report this whole check exists for, using a SECOND
/// radius on the identical geometry that should have succeeded and didn't.
inline bool FilletPointWithinSpan(float nearX, float nearY, float farX, float farY, float ptX, float ptY) {
  const double dx = static_cast<double>(farX) - nearX, dy = static_cast<double>(farY) - nearY;
  const double len2 = dx * dx + dy * dy;
  if (len2 < 1e-12)
    return true;  // near == far (degenerate); nothing meaningful to check
  const double t = ((static_cast<double>(ptX) - nearX) * dx + (static_cast<double>(ptY) - nearY) * dy) / len2;
  constexpr double kSlack = 1e-3;
  return t <= 1.0 + kSlack;
}

/// True when `pt` is on the "kept" side of Line/Polyline-segment curve `c` — the side the pick
/// indicated — measured along the curve's OWN direction from its first stored endpoint, not
/// perpendicular to it. `pt - c.ax,ay` projected onto the curve's direction must have the same sign
/// as `pick - c.ax,ay}` projected the same way. Used to filter fillet-center candidates (below) to
/// the one actually consistent with both picks, not merely the one nearest them — see that
/// function's own comment for why nearest-to-pick alone is not enough.
inline bool FilletPointOnKeptSide(const FilletCurve& c, float pickX, float pickY, float ptX, float ptY) {
  const double ux = static_cast<double>(c.bx) - c.ax, uy = static_cast<double>(c.by) - c.ay;
  const double pickProj = (static_cast<double>(pickX) - c.ax) * ux + (static_cast<double>(pickY) - c.ay) * uy;
  const double ptProj = (static_cast<double>(ptX) - c.ax) * ux + (static_cast<double>(ptY) - c.ay) * uy;
  return (pickProj >= 0.0) == (ptProj >= 0.0);
}

/// Picks the candidate fillet center (from \ref FilletCandidateCenters) nearest the two pick points
/// — summed squared distance, the deterministic tie-break generalizing every other REQ-103 step's
/// own ambiguity resolution (LENGTHEN's nearest endpoint, EXTEND's nearest boundary hit, BREAK's
/// position-ordering: "whichever interpretation is closest to what the user actually clicked").
///
/// For two Lines specifically, "nearest to pick" alone is not a reliable filter: a real user report
/// found that for a large radius relative to short line segments, the mathematically-correct
/// "inside the corner" candidate is pushed FAR from the picks (a large radius means a long tangent
/// length, by construction), while a geometrically WRONG candidate — offset to one side, not
/// filling the actual corner at all — can end up numerically closer to picks placed near the
/// visible corner, and so wins the naive tie-break, producing a bizarre, disconnected-looking
/// result rather than either a correct (if large) arc or a clean refusal. Fixed by filtering to
/// candidates on the "kept" side (`FilletPointOnKeptSide`) of BOTH lines first — hand-verified
/// against this exact case in `tests/FilletGeomTests.cpp` — and running the nearest-to-pick
/// tie-break only among those; if the filter leaves nothing (should not happen for two genuinely
/// non-parallel lines, but a real geometry library earns defensive code), the unfiltered search is
/// used rather than manufacturing a spurious refusal.
///
/// False if no real candidate exists at all (parallel/collinear lines, arcs too far apart for the
/// radius, etc.) — REQ-201, the caller states the reason.
inline bool SolveFilletCenter(const FilletCurve& c1, const FilletCurve& c2, float radius, float pick1X,
                              float pick1Y, float pick2X, float pick2Y, float* outCx, float* outCy) {
  std::vector<curveisect::Vec2> cands;
  FilletCandidateCenters(c1, c2, radius, &cands);
  if (cands.empty())
    return false;
  auto pickBest = [&](const std::vector<size_t>& idx) -> size_t {
    size_t bestI = idx[0];
    double bestD = -1.0;
    for (size_t i : idx) {
      const double dx1 = cands[i].x - pick1X, dy1 = cands[i].y - pick1Y;
      const double dx2 = cands[i].x - pick2X, dy2 = cands[i].y - pick2Y;
      const double d = dx1 * dx1 + dy1 * dy1 + dx2 * dx2 + dy2 * dy2;
      if (bestD < 0.0 || d < bestD) {
        bestD = d;
        bestI = i;
      }
    }
    return bestI;
  };
  std::vector<size_t> allIdx(cands.size());
  for (size_t i = 0; i < cands.size(); ++i)
    allIdx[i] = i;
  size_t bestI = 0;
  if (c1.isLine && c2.isLine) {
    std::vector<size_t> keptIdx;
    for (size_t i : allIdx) {
      const float cx = static_cast<float>(cands[i].x), cy = static_cast<float>(cands[i].y);
      if (FilletPointOnKeptSide(c1, pick1X, pick1Y, cx, cy) && FilletPointOnKeptSide(c2, pick2X, pick2Y, cx, cy))
        keptIdx.push_back(i);
    }
    bestI = keptIdx.empty() ? pickBest(allIdx) : pickBest(keptIdx);
  } else {
    bestI = pickBest(allIdx);
  }
  *outCx = static_cast<float>(cands[bestI].x);
  *outCy = static_cast<float>(cands[bestI].y);
  return true;
}

/// The point where the fillet arc centered at (centerX,centerY) touches Line `c` — the UNCLAMPED
/// foot of the perpendicular onto its infinite extension (OFFSET's own `ClosestPointOnSegment`
/// clamps to [0,1], wrong here: the tangent point routinely lies beyond the curve's current drawn
/// extent, which is exactly what the trim/extend step afterward corrects).
inline void FilletTangentPointOnLine(const FilletCurve& c, float centerX, float centerY, float* tx, float* ty) {
  const float vx = c.bx - c.ax, vy = c.by - c.ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    *tx = c.ax;
    *ty = c.ay;
    return;
  }
  const float t = ((centerX - c.ax) * vx + (centerY - c.ay) * vy) / len2;
  *tx = c.ax + t * vx;
  *ty = c.ay + t * vy;
}

/// The point where the fillet arc centered at (centerX,centerY) touches Arc/Circle `c` — guaranteed
/// by \ref FilletCandidateCenters's own construction to already lie exactly on `c`'s own circle,
/// along the ray from `c`'s center through the fillet center.
inline void FilletTangentPointOnCircle(const FilletCurve& c, float centerX, float centerY, float* tx, float* ty) {
  const float dx = centerX - c.cx, dy = centerY - c.cy;
  const float d = std::hypot(dx, dy);
  if (d < 1e-9f) {
    *tx = c.cx + c.r;
    *ty = c.cy;
    return;
  }
  *tx = c.cx + dx / d * c.r;
  *ty = c.cy + dy / d * c.r;
}

/// CCW angular travel in [0, 2*pi) from angle `from` to angle `to`.
inline float FilletCcwAngleDelta(float from, float to) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  float d = std::fmod(to - from, kTwoPi);
  if (d < 0.f)
    d += kTwoPi;
  return d;
}

/// Converts a target tangent point (known, by \ref FilletCandidateCenters's own construction, to
/// lie on arc `(cx,cy,r,startRad,sweepRad)`'s own circle) into the ABSOLUTE new-total-arc-length
/// \ref ApplyLengthenToArc's `newLength` parameter expects, so the moving endpoint (selected by
/// `nearFirst`, the identical convention `ApplyLengthenToArc` uses) lands exactly on it. Unlike
/// EXTEND's own `FindExtendArcTarget` (additive, extend-only by construction), this computes the
/// length directly from the FIXED endpoint, so it is correct whether the fillet shortens or extends
/// the arc — verified against hand-computed cases in tests/FilletGeomTests.cpp before being trusted.
inline float FilletArcTangentPointToNewLength(float cx, float cy, float r, float startRad, float sweepRad,
                                              bool nearFirst, float targetX, float targetY) {
  const float thetaT = std::atan2(targetY - cy, targetX - cx);
  const float fixedAngle = nearFirst ? (startRad + sweepRad) : startRad;
  const bool ccw = (sweepRad >= 0.f);
  float newAbsSweep;
  if (nearFirst)
    newAbsSweep = ccw ? FilletCcwAngleDelta(thetaT, fixedAngle) : FilletCcwAngleDelta(fixedAngle, thetaT);
  else
    newAbsSweep = ccw ? FilletCcwAngleDelta(fixedAngle, thetaT) : FilletCcwAngleDelta(thetaT, fixedAngle);
  return r * newAbsSweep;
}

/// True when two lines' directions are parallel within tolerance — detects FILLET's documented
/// AutoCAD special case (two parallel lines get a semicircle regardless of the current radius
/// setting; see spec/requirements.md REQ-103 FILLET acceptance).
inline bool FilletLinesAreParallel(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
  const float v1x = bx - ax, v1y = by - ay, v2x = dx - cx, v2y = dy - cy;
  const float len1 = std::hypot(v1x, v1y), len2 = std::hypot(v2x, v2y);
  if (len1 < 1e-9f || len2 < 1e-9f)
    return false;
  const float cross = (v1x / len1) * (v2y / len2) - (v1y / len1) * (v2x / len2);
  return std::fabs(cross) < 1e-4f;
}

/// FILLET's documented AutoCAD special case: two parallel, non-collinear lines get connected by a
/// semicircle regardless of the current radius setting. `pick1X,pick1Y` chooses which end of line 1
/// anchors the semicircle (nearest that pick, returned unmoved as `anchorX,anchorY`); the
/// semicircle's OTHER endpoint is the perpendicular projection of the anchor onto line 2's infinite
/// extension (`projX,projY`) — guaranteeing exact tangency to both lines, with the radius that falls
/// out equal to exactly half the true perpendicular distance between them, matching AutoCAD's own
/// documented "radius = half the distance" rule.
inline void FilletParallelSemicircle(float l1ax, float l1ay, float l1bx, float l1by, float l2ax, float l2ay,
                                     float l2bx, float l2by, float pick1X, float pick1Y, float* anchorX,
                                     float* anchorY, float* projX, float* projY) {
  const float d0 = (pick1X - l1ax) * (pick1X - l1ax) + (pick1Y - l1ay) * (pick1Y - l1ay);
  const float d1 = (pick1X - l1bx) * (pick1X - l1bx) + (pick1Y - l1by) * (pick1Y - l1by);
  *anchorX = (d0 <= d1) ? l1ax : l1bx;
  *anchorY = (d0 <= d1) ? l1ay : l1by;
  const float vx = l2bx - l2ax, vy = l2by - l2ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    *projX = l2ax;
    *projY = l2ay;
    return;
  }
  const float t = ((*anchorX - l2ax) * vx + (*anchorY - l2ay) * vy) / len2;
  *projX = l2ax + t * vx;
  *projY = l2ay + t * vy;
}

// ================================================================================================
// REQ-103 CHAMFER (step 6b) — pure corner-point geometry. Eligible curves are Line/Polyline-segment
// only (Arc excluded — no standard chamfer-to-arc geometry, matching AutoCAD's own restriction), so
// this is simpler than FILLET's tangent-arc solve: the intersection point of the two (infinite)
// curves is found by reusing `SolveFilletCenter(c1, c2, 0.f, ...)` — FILLET's own radius-0 case,
// already proven correct — then each chamfer point is a fixed distance (or distance+angle) from it.
// ================================================================================================

/// Distance/Distance mode: the chamfer point on curve `c` (a Line/Polyline-segment `FilletCurve`),
/// `dist` from the already-known intersection point `(px,py)`, signed toward whichever side
/// `pickX,pickY` is on (the side the user's pick indicates should be kept).
inline void ChamferPointAtDistance(const FilletCurve& c, float px, float py, float dist, float pickX,
                                   float pickY, float* outX, float* outY) {
  const float dx = c.bx - c.ax, dy = c.by - c.ay;
  const float len = std::hypot(dx, dy);
  if (len < 1e-9f) {
    *outX = px;
    *outY = py;
    return;
  }
  const float ux = dx / len, uy = dy / len;
  const float side = (pickX - px) * ux + (pickY - py) * uy;
  const float sign = (side >= 0.f) ? 1.f : -1.f;
  *outX = px + ux * dist * sign;
  *outY = py + uy * dist * sign;
}

/// Distance/Angle mode: from `(fromX,fromY)` (curve `c`'s own Distance/Distance-style point),
/// a ray along curve `c`'s kept direction (toward `pickX,pickY`) rotated by `angleRad`, intersected
/// with curve `other`'s infinite extension. The accepted acceptance text names only "rotated toward
/// curve 2's side," not which of the two possible rotation senses that is in this codebase's
/// coordinate convention (ASSUMPTION-1, TASK-103) — both `+angleRad` and `-angleRad` are tried, and
/// whichever intersection lands nearer `otherPickX,otherPickY` is kept, the same "nearest to the
/// pick" disambiguation `SolveFilletCenter` and every other REQ-103 ambiguity resolution already
/// use. False if neither rotation intersects `other` at all (parallel to it in both senses).
inline bool ChamferRayIntersect(const FilletCurve& c, float fromX, float fromY, float pickX, float pickY,
                                float angleRad, const FilletCurve& other, float otherPickX, float otherPickY,
                                float* outX, float* outY) {
  const float dx = c.bx - c.ax, dy = c.by - c.ay;
  const float len = std::hypot(dx, dy);
  if (len < 1e-9f)
    return false;
  float ux = dx / len, uy = dy / len;
  const float side = (pickX - fromX) * ux + (pickY - fromY) * uy;
  if (side < 0.f) {
    ux = -ux;
    uy = -uy;
  }
  const curveisect::Seg otherLong = FilletLongLine(other.ax, other.ay, other.bx, other.by);
  bool found = false;
  float bestX = 0.f, bestY = 0.f;
  double bestD = -1.0;
  for (float s : {1.f, -1.f}) {
    const float rot = angleRad * s;
    const float rx = ux * std::cos(rot) - uy * std::sin(rot);
    const float ry = ux * std::sin(rot) + uy * std::cos(rot);
    // Double precision (see FilletLongLine's own comment) — the original float32 version of this
    // exact "extend by 1e6" computation is what a real user report traced FILLET's endpoint-vs-arc
    // gap back to.
    constexpr double kHuge = 1.0e6;
    const curveisect::Seg ray{{static_cast<double>(fromX), static_cast<double>(fromY)},
                              {fromX + static_cast<double>(rx) * kHuge, fromY + static_cast<double>(ry) * kHuge}};
    std::vector<curveisect::Hit2> hits;
    curveisect::IntersectSegSeg(ray, otherLong, &hits);
    for (const auto& h : hits) {
      const double d = (h.p.x - otherPickX) * (h.p.x - otherPickX) + (h.p.y - otherPickY) * (h.p.y - otherPickY);
      if (!found || d < bestD) {
        found = true;
        bestD = d;
        bestX = static_cast<float>(h.p.x);
        bestY = static_cast<float>(h.p.y);
      }
    }
  }
  if (!found)
    return false;
  *outX = bestX;
  *outY = bestY;
  return true;
}
