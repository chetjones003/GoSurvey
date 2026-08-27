#pragma once

#include "CadDimGeom.hpp"
#include "DimensionStyle.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

/// World-space (local storage) strokes for a dimension so Paper Space viewports and plots can
/// project them the same way as linework (issue #110 / REQ-027).

struct CadDimWorldSeg {
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
  enum class Kind { Extension, DimLine, Arrow } kind = Kind::DimLine;
};

struct CadDimWorldTri {
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f, x2 = 0.f, y2 = 0.f;
};

struct CadDimWorldStrokes {
  std::vector<CadDimWorldSeg> segs;
  std::vector<CadDimWorldTri> arrows;
  float labelX = 0.f;
  float labelY = 0.f;
  float labelRotRad = 0.f;
  bool ok = false;
};

struct CadDimStrokeParams {
  float modelUnitsPerPlottedInch = 1.f;
  float arrowSizeInches = 0.10f;
  float arrowScale = 1.f;
  DimArrowType arrowType = DimArrowType::ClosedFilled;
};

inline bool CadAnnotationIsDimension(const CadAnnotation& a) {
  return a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear ||
         a.kind == CadAnnotation::Kind::DimAngular;
}

inline bool CadClipSegToRect(float xmin, float ymin, float xmax, float ymax, float& x0, float& y0, float& x1,
                             float& y1) {
  float t0 = 0.f, t1 = 1.f;
  const float dx = x1 - x0, dy = y1 - y0;
  auto clip = [&](float p, float q) {
    if (p == 0.f)
      return q >= 0.f;
    const float r = q / p;
    if (p < 0.f) {
      if (r > t1)
        return false;
      if (r > t0)
        t0 = r;
    } else {
      if (r < t0)
        return false;
      if (r < t1)
        t1 = r;
    }
    return true;
  };
  if (clip(-dx, x0 - xmin) && clip(dx, xmax - x0) && clip(-dy, y0 - ymin) && clip(dy, ymax - y0)) {
    const float nx0 = x0 + t0 * dx, ny0 = y0 + t0 * dy;
    const float nx1 = x0 + t1 * dx, ny1 = y0 + t1 * dy;
    x0 = nx0;
    y0 = ny0;
    x1 = nx1;
    y1 = ny1;
    return true;
  }
  return false;
}

inline void CadDimPushSeg(CadDimWorldStrokes* out, float x0, float y0, float x1, float y1, CadDimWorldSeg::Kind k) {
  if (!out)
    return;
  CadDimWorldSeg s;
  s.x0 = x0;
  s.y0 = y0;
  s.x1 = x1;
  s.y1 = y1;
  s.kind = k;
  out->segs.push_back(s);
}

inline void CadDimPushArrow(CadDimWorldStrokes* out, DimArrowType type, float tipx, float tipy, float dirx, float diry,
                            float alen, float hw) {
  if (!out)
    return;
  const float len = std::hypot(dirx, diry);
  if (len < 1.e-8f)
    return;
  const float ux = dirx / len, uy = diry / len;
  const float ox = -uy * hw, oy = ux * hw;
  const float bx = tipx + ux * alen, by = tipy + uy * alen;
  if (type == DimArrowType::None)
    return;
  if (type == DimArrowType::Tick) {
    CadDimPushSeg(out, tipx - oy, tipy + ox, tipx + oy, tipy - ox, CadDimWorldSeg::Kind::Arrow);
    return;
  }
  if (type == DimArrowType::Dot) {
    CadDimPushSeg(out, tipx - hw, tipy, tipx + hw, tipy, CadDimWorldSeg::Kind::Arrow);
    CadDimPushSeg(out, tipx, tipy - hw, tipx, tipy + hw, CadDimWorldSeg::Kind::Arrow);
    return;
  }
  CadDimWorldTri t;
  t.x0 = tipx;
  t.y0 = tipy;
  t.x1 = bx + ox;
  t.y1 = by + oy;
  t.x2 = bx - ox;
  t.y2 = by - oy;
  out->arrows.push_back(t);
  CadDimPushSeg(out, t.x0, t.y0, t.x1, t.y1, CadDimWorldSeg::Kind::Arrow);
  CadDimPushSeg(out, t.x1, t.y1, t.x2, t.y2, CadDimWorldSeg::Kind::Arrow);
  CadDimPushSeg(out, t.x2, t.y2, t.x0, t.y0, CadDimWorldSeg::Kind::Arrow);
}

inline bool CadDimBuildLinearStrokes(const CadAnnotation& a, const CadDimStrokeParams& p, CadDimWorldStrokes* out) {
  if (!out)
    return false;
  float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
  if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
    return false;
  const float gap = std::clamp(0.012f * meas, 1.e-5f * meas, 0.12f * meas);
  const float over = std::clamp(0.02f * meas, 1.e-5f * meas, 0.1f * meas);
  const float leg1 = std::hypot(sx1 - a.dimExt1X, sy1 - a.dimExt1Y);
  const float u1 = leg1 > 1.e-8f ? gap / leg1 : 0.f;
  const float ex1 = a.dimExt1X + (sx1 - a.dimExt1X) * u1;
  const float ey1 = a.dimExt1Y + (sy1 - a.dimExt1Y) * u1;
  const float leg2 = std::hypot(sx2 - a.dimExt2X, sy2 - a.dimExt2Y);
  const float u2 = leg2 > 1.e-8f ? gap / leg2 : 0.f;
  const float ex2 = a.dimExt2X + (sx2 - a.dimExt2X) * u2;
  const float ey2 = a.dimExt2Y + (sy2 - a.dimExt2Y) * u2;
  CadDimPushSeg(out, ex1, ey1, sx1 + nx * over, sy1 + ny * over, CadDimWorldSeg::Kind::Extension);
  CadDimPushSeg(out, ex2, ey2, sx2 + nx * over, sy2 + ny * over, CadDimWorldSeg::Kind::Extension);
  const float styleArrowWorld = p.arrowSizeInches * (std::max)(p.modelUnitsPerPlottedInch, 1.e-6f);
  const float alenW = (std::max)(styleArrowWorld * p.arrowScale * 0.10f, p.arrowScale * 0.012f * meas);
  const float dlen = std::hypot(sx2 - sx1, sy2 - sy1);
  if (dlen > 1.e-6f) {
    const float ux = (sx2 - sx1) / dlen, uy = (sy2 - sy1) / dlen;
    const float tipInset = std::clamp(0.18f * alenW, 1.e-7f * meas, (std::max)(1.e-6f, 0.22f * dlen));
    const float maxAlen = 0.47f * (std::max)(0.f, dlen - 2.f * tipInset);
    const float alenUse = (std::max)(1.e-6f, (std::min)(alenW, maxAlen));
    const float tip1x = sx1 + ux * tipInset, tip1y = sy1 + uy * tipInset;
    const float tip2x = sx2 - ux * tipInset, tip2y = sy2 - uy * tipInset;
    const float base1x = tip1x + ux * alenUse, base1y = tip1y + uy * alenUse;
    const float base2x = tip2x - ux * alenUse, base2y = tip2y - uy * alenUse;
    if (std::hypot(base2x - base1x, base2y - base1y) > 1.e-5f)
      CadDimPushSeg(out, base1x, base1y, base2x, base2y, CadDimWorldSeg::Kind::DimLine);
    const float hw = alenUse * 0.48f;
    CadDimPushArrow(out, p.arrowType, tip1x, tip1y, ux, uy, alenUse, hw);
    CadDimPushArrow(out, p.arrowType, tip2x, tip2y, -ux, -uy, alenUse, hw);
  }
  return true;
}

inline bool CadDimBuildAngularStrokes(const CadAnnotation& a, const CadDimStrokeParams& p, CadDimWorldStrokes* out) {
  if (!out)
    return false;
  float a1 = 0.f, a2 = 0.f, sweep = 0.f, theta = 0.f, bisx = 0.f, bisy = 0.f;
  if (!CadDimAngularComputeFrame(a, &a1, &a2, &sweep, &bisx, &bisy, &theta))
    return false;
  const float R = (std::max)(a.dimSignedOffset, 1.e-6f);
  const float vx = a.dimAngVertexX, vy = a.dimAngVertexY;
  const float hWorld = a.plottedHeightInches * (std::max)(p.modelUnitsPerPlottedInch, 1.e-6f);
  const float gapWorld = (std::max)(0.12f * hWorld, 0.015f * R);
  const float overWorld = (std::max)(0.08f * hWorld, 0.01f * R);
  CadDimPushSeg(out, vx + std::cos(a1) * gapWorld, vy + std::sin(a1) * gapWorld, vx + std::cos(a1) * (R + overWorld),
                vy + std::sin(a1) * (R + overWorld), CadDimWorldSeg::Kind::Extension);
  CadDimPushSeg(out, vx + std::cos(a2) * gapWorld, vy + std::sin(a2) * gapWorld, vx + std::cos(a2) * (R + overWorld),
                vy + std::sin(a2) * (R + overWorld), CadDimWorldSeg::Kind::Extension);
  constexpr int kSegs = 32;
  const float gapAng = (hWorld * 1.8f) / (std::max)(R, 1.e-6f);
  const float mid = a1 + sweep * 0.5f;
  for (int i = 0; i < kSegs; ++i) {
    const float aa = a1 + sweep * (static_cast<float>(i) / kSegs);
    const float ab = a1 + sweep * (static_cast<float>(i + 1) / kSegs);
    if (std::fabs(aa - mid) < gapAng * 0.5f || std::fabs(ab - mid) < gapAng * 0.5f)
      continue;
    if ((aa < mid && ab > mid) || (aa > mid && ab < mid))
      continue;
    CadDimPushSeg(out, vx + std::cos(aa) * R, vy + std::sin(aa) * R, vx + std::cos(ab) * R, vy + std::sin(ab) * R,
                  CadDimWorldSeg::Kind::DimLine);
  }
  const float styleArrowWorld = p.arrowSizeInches * (std::max)(p.modelUnitsPerPlottedInch, 1.e-6f);
  const float arcLen = R * std::fabs(sweep);
  const float arrowWorld = (std::max)(styleArrowWorld * p.arrowScale * 0.10f, p.arrowScale * 0.012f * arcLen);
  const float maxArrowForArc = 0.47f * (std::max)(0.f, R * std::fabs(sweep) - 0.15f * arrowWorld);
  const float alenUse = (std::max)(1.e-6f, (std::min)(arrowWorld, maxArrowForArc));
  const float sgn = (sweep >= 0.f) ? 1.f : -1.f;
  const float t1x = -std::sin(a1) * sgn, t1y = std::cos(a1) * sgn;
  const float t2x = std::sin(a2) * sgn, t2y = -std::cos(a2) * sgn;
  const float hw = alenUse * 0.48f;
  CadDimPushArrow(out, p.arrowType, vx + std::cos(a1) * R, vy + std::sin(a1) * R, t1x, t1y, alenUse, hw);
  CadDimPushArrow(out, p.arrowType, vx + std::cos(a2) * R, vy + std::sin(a2) * R, t2x, t2y, alenUse, hw);
  return true;
}

inline bool CadDimBuildWorldStrokes(const CadAnnotation& a, const CadDimStrokeParams& p, CadDimWorldStrokes* out) {
  if (!out)
    return false;
  out->segs.clear();
  out->arrows.clear();
  out->ok = false;
  out->labelX = a.insX;
  out->labelY = a.insY;
  out->labelRotRad = a.rotationRad;
  bool built = false;
  if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear)
    built = CadDimBuildLinearStrokes(a, p, out);
  else if (a.kind == CadAnnotation::Kind::DimAngular)
    built = CadDimBuildAngularStrokes(a, p, out);
  out->ok = built;
  return built;
}
