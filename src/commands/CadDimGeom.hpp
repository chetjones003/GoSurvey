#pragma once

#include "CadEntities.hpp"

#include <algorithm>
#include <cmath>

/// Header-only dimension frame geometry (issue #110). Same math previously lived in CadCommands.cpp;
/// tests and the paper-viewport overlay need it without linking the command layer.

inline float CadAngNormalizeMinusPiToPi(float a) {
  constexpr float kPi = 3.14159265358979323846f;
  for (int n = 0; n < 16; ++n) {
    if (a > kPi)
      a -= 2.f * kPi;
    else if (a < -kPi)
      a += 2.f * kPi;
    else
      break;
  }
  return a;
}

inline bool CadDimAlignedGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                                  float* ty, float* nx, float* ny, float* measLen) {
  if (!sx1 || !sy1 || !sx2 || !sy2 || !tx || !ty || !nx || !ny || !measLen)
    return false;
  if (a.kind != CadAnnotation::Kind::DimAligned)
    return false;
  const float x1 = a.dimExt1X, y1 = a.dimExt1Y, x2 = a.dimExt2X, y2 = a.dimExt2Y;
  float vx = x2 - x1;
  float vy = y2 - y1;
  const float len = std::hypot(vx, vy);
  if (len < 1.e-8f)
    return false;
  vx /= len;
  vy /= len;
  const float n0x = -vy;
  const float n0y = vx;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  const float dmx = cmx + n0x * a.dimSignedOffset;
  const float dmy = cmy + n0y * a.dimSignedOffset;
  const float t1 = (x1 - dmx) * vx + (y1 - dmy) * vy;
  const float t2 = (x2 - dmx) * vx + (y2 - dmy) * vy;
  *sx1 = dmx + vx * t1;
  *sy1 = dmy + vy * t1;
  *sx2 = dmx + vx * t2;
  *sy2 = dmy + vy * t2;
  *tx = vx;
  *ty = vy;
  *nx = n0x;
  *ny = n0y;
  *measLen = len;
  return true;
}

inline bool CadDimLinearGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                                 float* ty, float* nx, float* ny, float* measLen) {
  if (!sx1 || !sy1 || !sx2 || !sy2 || !tx || !ty || !nx || !ny || !measLen)
    return false;
  if (a.kind != CadAnnotation::Kind::DimLinear)
    return false;
  const float x1 = a.dimExt1X, y1 = a.dimExt1Y, x2 = a.dimExt2X, y2 = a.dimExt2Y;
  const float cmx = 0.5f * (x1 + x2);
  const float cmy = 0.5f * (y1 + y2);
  if (!a.dimLinearVertical) {
    const float span = std::fabs(x2 - x1);
    if (span < 1.e-8f)
      return false;
    const float dmy = cmy + a.dimSignedOffset;
    *sx1 = x1;
    *sy1 = dmy;
    *sx2 = x2;
    *sy2 = dmy;
    *tx = (x2 >= x1) ? 1.f : -1.f;
    *ty = 0.f;
    *nx = 0.f;
    *ny = 1.f;
    *measLen = span;
  } else {
    const float span = std::fabs(y2 - y1);
    if (span < 1.e-8f)
      return false;
    const float dmx = cmx + a.dimSignedOffset;
    *sx1 = dmx;
    *sy1 = y1;
    *sx2 = dmx;
    *sy2 = y2;
    *tx = 0.f;
    *ty = (y2 >= y1) ? 1.f : -1.f;
    *nx = 1.f;
    *ny = 0.f;
    *measLen = span;
  }
  return true;
}

inline bool CadDimAnyGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                              float* ty, float* nx, float* ny, float* measLen) {
  if (a.kind == CadAnnotation::Kind::DimAligned)
    return CadDimAlignedGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, measLen);
  if (a.kind == CadAnnotation::Kind::DimLinear)
    return CadDimLinearGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, measLen);
  return false;
}

inline bool CadDimAngularComputeFrame(const CadAnnotation& a, float* a1Out, float* a2Out, float* sweepOut, float* bisx,
                                      float* bisy, float* thetaInterior) {
  if (!a1Out || !a2Out || !sweepOut || !bisx || !bisy || !thetaInterior)
    return false;
  if (a.kind != CadAnnotation::Kind::DimAngular)
    return false;
  const float vx = a.dimAngVertexX, vy = a.dimAngVertexY;
  const float p1x = a.dimExt1X, p1y = a.dimExt1Y, p2x = a.dimExt2X, p2y = a.dimExt2Y;
  const float u1x = p1x - vx, u1y = p1y - vy;
  const float u2x = p2x - vx, u2y = p2y - vy;
  const float l1 = std::hypot(u1x, u1y);
  const float l2 = std::hypot(u2x, u2y);
  if (l1 < 1.e-8f || l2 < 1.e-8f)
    return false;
  const float n1x = u1x / l1, n1y = u1y / l1;
  const float n2x = u2x / l2, n2y = u2y / l2;
  const float dot = n1x * n2x + n1y * n2y;
  const float a1 = std::atan2(n1y, n1x);
  const float a2 = std::atan2(n2y, n2x);
  const float sweep = CadAngNormalizeMinusPiToPi(a2 - a1);
  const float theta = std::acos(std::clamp(dot, -1.f, 1.f));
  float bx = n1x + n2x;
  float by = n1y + n2y;
  const float bl = std::hypot(bx, by);
  if (bl > 1.e-6f) {
    bx /= bl;
    by /= bl;
  } else {
    bx = -n1y;
    by = n1x;
  }
  const float mid = a1 + 0.5f * sweep;
  const float mdx = std::cos(mid);
  const float mdy = std::sin(mid);
  if (bx * mdx + by * mdy < 0.f) {
    bx = -bx;
    by = -by;
  }
  *a1Out = a1;
  *a2Out = a2;
  *sweepOut = sweep;
  *bisx = bx;
  *bisy = by;
  *thetaInterior = theta;
  return theta > 1.e-7f;
}
