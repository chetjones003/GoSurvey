// Command-layer entity picking and pixel-to-world tolerance helpers.
// Extracted from CadCommands.cpp as part of ADR-0012 / Issue #1578.
// Uses a local WorldToleranceFromPixels helper so Commands does not include Interaction/CadSnap.hpp.

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "CadDimStroke.hpp"

#include <algorithm>
#include <cmath>

namespace {

float WorldToleranceFromPixels(float viewportHeightPx, float orthoHalfHeightWorld, float pixelRadius) {
  if (viewportHeightPx <= 1.f)
    return std::max(orthoHalfHeightWorld * 0.02f, 1.e-6f);
  return std::max(1.e-6f, pixelRadius * (2.f * orthoHalfHeightWorld / viewportHeightPx));
}

float CadGizmoOrthoHalfHeightWorld(const AppCommandState& st) {
  return (1.f / std::max(st.viewportZoom, 1.e-9f)) * 50.f;
}

} // namespace

int PickCadAnnotationAt(float wxIn, float wyIn, const AppCommandState& cmd, float orthoHalfHeightWorld,
                        float viewportHeightPx, const ray3d::Ray* ray) {
  const float tol =
      WorldToleranceFromPixels(viewportHeightPx, orthoHalfHeightWorld, cmd.objectSnapAperturePx);
  const float tol2 = tol * tol;
  auto distSqSeg = [](float px, float py, float ax, float ay, float bx, float by) -> float {
    const float vx = bx - ax;
    const float vy = by - ay;
    const float len2 = vx * vx + vy * vy;
    if (len2 < 1.e-18f) {
      const float dx = px - ax;
      const float dy = py - ay;
      return dx * dx + dy * dy;
    }
    const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
    const float qx = ax + t * vx;
    const float qy = ay + t * vy;
    const float dx = px - qx;
    const float dy = py - qy;
    return dx * dx + dy * dy;
  };
  for (int i = static_cast<int>(cmd.drawing.cadAnnotations.size()) - 1; i >= 0; --i) {
    if (!cmd.hiddenEntityIds.empty() && static_cast<size_t>(i) < cmd.drawing.cadAnnotationAttrs.size() &&
        CadEntityIdHidden(&cmd.hiddenEntityIds, cmd.drawing.cadAnnotationAttrs[static_cast<size_t>(i)].id))
      continue;
    const CadAnnotation& a = cmd.drawing.cadAnnotations[static_cast<size_t>(i)];
    float wx = wxIn;
    float wy = wyIn;
    if (ray && !RayAtElevation(*ray, static_cast<double>(a.insZ), &wx, &wy))
      continue;
    if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
      if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
        continue;
      float best = tol2 + 1.f;
      auto upd = [&](float ax, float ay, float bx, float by) {
        best = std::min(best, distSqSeg(wx, wy, ax, ay, bx, by));
      };
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
      upd(ex1, ey1, sx1 + nx * over, sy1 + ny * over);
      upd(ex2, ey2, sx2 + nx * over, sy2 + ny * over);
      upd(sx1, sy1, sx2, sy2);
      const DimensionStyle& dimStyle = CadDimensionStyleForAnnotation(cmd, a);
      const float dimMup = CadDimModelUnitsPerPlottedInch(
          a, &dimStyle, cmd.drawing.modelUnitsPerPlottedInch, &cmd.drawing.annotationScales,
          cmd.currentAnnotationScaleIndex, nullptr, cmd.activeSpaceIndex == kModelSpaceIndex);
      const float h = CadDimTextHeightWorld(a, &dimStyle, dimMup);  // issue #1676
      double labelX = a.insX, labelY = a.insY;
      CadDimModelSpaceLabelPosition(cmd, a, &labelX, &labelY);
      const float charFactor = 0.55f;
      const float tw = std::max(h * charFactor * std::max(1.f, static_cast<float>(a.text.size())), h * 2.f);
      const float c = std::cos(a.rotationRad);
      const float s = std::sin(a.rotationRad);
      auto corner = [&](float lx, float ly, float* ox, float* oy) {
        const float rx = lx * c - ly * s;
        const float ry = lx * s + ly * c;
        *ox = static_cast<float>(labelX) + rx;
        *oy = static_cast<float>(labelY) + ry;
      };
      float xs[4]{};
      float ys[4]{};
      corner(0.f, 0.f, &xs[0], &ys[0]);
      corner(tw, 0.f, &xs[1], &ys[1]);
      corner(tw, -h, &xs[2], &ys[2]);
      corner(0.f, -h, &xs[3], &ys[3]);
      for (int e = 0; e < 4; ++e) {
        const int e2 = (e + 1) % 4;
        upd(xs[e], ys[e], xs[e2], ys[e2]);
      }
      if (best <= tol2)
        return i;
      continue;
    }
    if (a.kind == CadAnnotation::Kind::Table)
      continue;
    // Test in the text's own frame; its world AABB includes empty corners after rotation.
    CadAnnotation unrotated = a;
    const bool box = CadAnnotationHasTextBox(a.kind);
    const float ax = box ? a.boxMinX : a.insX, ay = box ? a.boxMaxY : a.insY;
    const float dx = wx - ax, dy = wy - ay;
    const float c = std::cos(a.rotationRad), s = std::sin(a.rotationRad);
    wx = ax + dx * c + dy * s;
    wy = ay - dx * s + dy * c;
    unrotated.rotationRad = 0.f;
    float xs[4], ys[4];
    // Cheap reject first: the estimated box, padded by its own size to cover any measured-width
    // difference, so hovering a drawing full of text doesn't lay out every string on each move.
    CadAnnotationTextPickQuad(cmd, unrotated, xs, ys, /*measure=*/false);
    float mnX = *std::min_element(xs, xs + 4), mxX = *std::max_element(xs, xs + 4);
    float mnY = *std::min_element(ys, ys + 4), mxY = *std::max_element(ys, ys + 4);
    const float pad = std::max(mxX - mnX, mxY - mnY) + tol;
    if (wx < mnX - pad || wx > mxX + pad || wy < mnY - pad || wy > mxY + pad)
      continue;
    CadAnnotationTextPickQuad(cmd, unrotated, xs, ys);
    mnX = *std::min_element(xs, xs + 4); mxX = *std::max_element(xs, xs + 4);
    mnY = *std::min_element(ys, ys + 4); mxY = *std::max_element(ys, ys + 4);
    if (wx >= mnX - tol && wx <= mxX + tol && wy >= mnY - tol && wy <= mxY + tol)
      return i;
  }
  return -1;
}

int PickCadTableAt(float wx, float wy, const AppCommandState& cmd, float orthoHalfHeightWorld,
                   float viewportHeightPx) {
  const float tol =
      WorldToleranceFromPixels(viewportHeightPx, orthoHalfHeightWorld, cmd.objectSnapAperturePx);
  for (int i = static_cast<int>(cmd.drawing.cadTables.size()) - 1; i >= 0; --i) {
    if (!cmd.hiddenEntityIds.empty() && static_cast<size_t>(i) < cmd.drawing.cadTableAttrs.size() &&
        CadEntityIdHidden(&cmd.hiddenEntityIds, cmd.drawing.cadTableAttrs[static_cast<size_t>(i)].id))
      continue;
    if (CadTableContainsLocal(cmd.drawing.cadTables[static_cast<size_t>(i)], wx, wy, tol))
      return i;
  }
  return -1;
}

namespace CadCmdGeom {

float DistSqPointSegment(float px, float py, float ax, float ay, float bx, float by) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1.e-18f) {
    const float dx = px - ax;
    const float dy = py - ay;
    return dx * dx + dy * dy;
  }
  const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
  const float qx = ax + t * vx;
  const float qy = ay + t * vy;
  const float dx = px - qx;
  const float dy = py - qy;
  return dx * dx + dy * dy;
}

float MinDistSqSegSeg(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
  float best = DistSqPointSegment(ax, ay, cx, cy, dx, dy);
  best = std::min(best, DistSqPointSegment(bx, by, cx, cy, dx, dy));
  best = std::min(best, DistSqPointSegment(cx, cy, ax, ay, bx, by));
  best = std::min(best, DistSqPointSegment(dx, dy, ax, ay, bx, by));
  constexpr int N = 28;
  for (int i = 1; i < N; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(N);
    const float px = ax + t * (bx - ax);
    const float py = ay + t * (by - ay);
    best = std::min(best, DistSqPointSegment(px, py, cx, cy, dx, dy));
    const float qx = cx + t * (dx - cx);
    const float qy = cy + t * (dy - cy);
    best = std::min(best, DistSqPointSegment(qx, qy, ax, ay, bx, by));
  }
  return best;
}

} // namespace CadCmdGeom

float CadGizmoHandleLenWorld(const AppCommandState& st) {
  const float vph = st.uiViewportHeightPx > 1.f ? st.uiViewportHeightPx : 700.f;
  return std::max(1.e-6f, WorldToleranceFromPixels(vph, CadGizmoOrthoHalfHeightWorld(st), kGizmoHandleLenPx));
}

float CadOffsetEntityPickTolWorld(const AppCommandState& st) {
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  float geom = 1e-3f;
  int skipped = 0;
  if (ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped))
    geom = std::max(1e-5f, static_cast<float>(2.5e-5 * std::max(mxX - mnX, mxY - mnY)));
  const float px = WorldToleranceFromPixels(st.viewportLastSurveyLayoutHeightPx,
                                            st.viewportLastSurveyLayoutOrthoHalfH, st.objectSnapAperturePx);
  return std::max(geom, px * 1.5f);
}

float CadHoverEntityPickTolWorld(const AppCommandState& st) {
  const float apPx = std::clamp(st.objectSnapAperturePx, 4.f, 64.f) * 0.5f;
  return std::max(1e-6f, WorldToleranceFromPixels(st.viewportLastSurveyLayoutHeightPx,
                                                  st.viewportLastSurveyLayoutOrthoHalfH, apPx));
}
