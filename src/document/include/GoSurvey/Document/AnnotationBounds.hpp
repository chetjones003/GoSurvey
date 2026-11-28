#pragma once

// The plan boxes of the kinds that size themselves from drawing state (issue #1700 phase 4,
// ADR-0011): annotations (their text height follows the drawing scale), multileaders and Position
// Markers. \ref GoSurvey::Document::EntityBounds is built on these; hit testing and the paper-space
// overlay use them too. Moved unchanged from Commands.

// CadAnnotation and the multileader and marker records come in through cadblock.hpp (ADR-0010).
#include <GoSurvey/Document/CadDimGeom.hpp>
#include <GoSurvey/Document/cadblock.hpp>

#include <algorithm>
#include <cmath>

/// World height of \p a's text at the drawing scale \p modelUnitsPerPlottedInch.
inline float CadAnnotationHeightWorld(const CadAnnotation& a, float modelUnitsPerPlottedInch) {
  // Issue #1676: a dimension's model-unit text override is the same size at every scale.
  if (std::isfinite(a.dimTextHeightModel) && a.dimTextHeightModel > 0.0 &&
      (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear ||
       a.kind == CadAnnotation::Kind::DimAngular))
    return static_cast<float>(a.dimTextHeightModel);
  return a.plottedHeightInches * std::max(modelUnitsPerPlottedInch, 1.e-6f);
}

/// Axis-aligned rough bounds for hit-testing / zoom (TEXT uses estimated glyph width).
inline void CadAnnotationRoughBounds(const CadAnnotation& a, float modelUnitsPerPlottedInch, float* outMnX, float* outMnY,
                              float* outMxX, float* outMxY) {
  const float h = CadAnnotationHeightWorld(a, modelUnitsPerPlottedInch);
  if (CadAnnotationHasTextBox(a.kind)) {
    // Delegated, not duplicated — CadTextAnnotationBounds returns the same stored box. Kept as an
    // explicit early-out only because the dimension branch below must not fall through to it.
    CadTextAnnotationBounds(a, h, outMnX, outMnY, outMxX, outMxY);
    return;
  }
  if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
    if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas)) {
      *outMnX = *outMxX = a.insX;
      *outMnY = *outMxY = a.insY;
      return;
    }
    auto expandSeg = [&](float ax, float ay, float bx, float by) {
      *outMnX = std::min(*outMnX, std::min(ax, bx));
      *outMxX = std::max(*outMxX, std::max(ax, bx));
      *outMnY = std::min(*outMnY, std::min(ay, by));
      *outMxY = std::max(*outMxY, std::max(ay, by));
    };
    *outMnX = *outMxX = sx1;
    *outMnY = *outMxY = sy1;
    expandSeg(sx1, sy1, sx2, sy2);
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
    expandSeg(ex1, ey1, sx1 + nx * over, sy1 + ny * over);
    expandSeg(ex2, ey2, sx2 + nx * over, sy2 + ny * over);
    const float tw = CadTextEstimatedWidth(a.text, h);  // the dimension LABEL's own quad
    const float c = std::cos(a.rotationRad);
    const float s = std::sin(a.rotationRad);
    auto corner = [&](float lx, float ly, float* ox, float* oy) {
      const float rx = lx * c - ly * s;
      const float ry = lx * s + ly * c;
      *ox = a.insX + rx;
      *oy = a.insY + ry;
    };
    float xs[4]{};
    float ys[4]{};
    corner(0.f, 0.f, &xs[0], &ys[0]);
    corner(tw, 0.f, &xs[1], &ys[1]);
    corner(tw, -h, &xs[2], &ys[2]);
    corner(0.f, -h, &xs[3], &ys[3]);
    for (int i = 0; i < 4; ++i)
      expandSeg(xs[i], ys[i], xs[i], ys[i]);
    return;
  }
  // Plain TEXT: the shared rule, one definition with paper space (TASK-198).
  CadTextAnnotationBounds(a, h, outMnX, outMnY, outMxX, outMxY);
}

/// Plan box of a Position Marker's circle (radius \p r, world units) and its label.
inline void CadPositionMarkerBox(const CadPositionMarker& m, float r, float* mnX, float* mnY, float* mxX, float* mxY) {
  const float cx = static_cast<float>(m.x);
  const float cy = static_cast<float>(m.y);
  *mnX = std::min(cx - r, m.label.boxMinX);
  *mnY = std::min(cy - r, m.label.boxMinY);
  *mxX = std::max(cx + r, m.label.boxMaxX);
  *mxY = std::max(cy + r, m.label.boxMaxY);
}

/// Plan box of a multileader: its leader paths, extra branches and label frame.
inline void CadMultileaderLocalBox(const CadMultileader& ml, float* mnX, float* mnY, float* mxX, float* mxY) {
  bool any = false;
  auto grow = [&](float x, float y) {
    if (!any) {
      *mnX = *mxX = x;
      *mnY = *mxY = y;
      any = true;
    } else {
      *mnX = std::min(*mnX, x);
      *mnY = std::min(*mnY, y);
      *mxX = std::max(*mxX, x);
      *mxY = std::max(*mxY, y);
    }
  };
  for (size_t i = 0; i + 2 < ml.pathXyz.size(); i += 3)
    grow(ml.pathXyz[i], ml.pathXyz[i + 1]);
  for (const std::vector<float>& branch : ml.extraLeaderPaths) {
    for (size_t i = 0; i + 2 < branch.size(); i += 3)
      grow(branch[i], branch[i + 1]);
  }
  grow(ml.label.boxMinX, ml.label.boxMinY);
  grow(ml.label.boxMaxX, ml.label.boxMaxY);
  if (!any) {
    *mnX = *mnY = *mxX = *mxY = 0.f;
  }
}
