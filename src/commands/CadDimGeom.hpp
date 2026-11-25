#pragma once

#include "CadEntities.hpp"

#include <algorithm>
#include <cmath>

/// Header-only dimension frame geometry (issue #110). Same math previously lived in CadCommands.cpp;
/// tests and the paper-viewport overlay need it without linking the command layer.

template <class T>
inline T CadAngNormalizeMinusPiToPiT(T a) {
  constexpr T kPi = static_cast<T>(3.14159265358979323846);
  for (int n = 0; n < 16; ++n) {
    if (a > kPi)
      a -= 2 * kPi;
    else if (a < -kPi)
      a += 2 * kPi;
    else
      break;
  }
  return a;
}

inline float CadAngNormalizeMinusPiToPi(float a) { return CadAngNormalizeMinusPiToPiT(a); }
/// Double overload for the angular frame, which measures in double (issue #1668).
inline double CadAngNormalizeMinusPiToPi(double a) { return CadAngNormalizeMinusPiToPiT(a); }

/// Makes a dimension text rotation readable (REQ-477, issue #804). For an input in [-pi, pi]
/// radians, the result is in (-pi/2, pi/2], undoing the flip caused by reversing extension points.
/// Normalization performs at most 16 full turns, so larger inputs may remain outside that range.
inline float CadDimReadableTextRotation(float a) {
  constexpr float kHalfPi = 1.57079632679489661923f;
  float r = CadAngNormalizeMinusPiToPi(a);
  if (r > kHalfPi)
    r -= 2.f * kHalfPi;
  else if (r <= -kHalfPi)
    r += 2.f * kHalfPi;
  return r;
}

inline bool CadDimAlignedGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                                  float* ty, float* nx, float* ny, double* measLen) {
  if (!sx1 || !sy1 || !sx2 || !sy2 || !tx || !ty || !nx || !ny || !measLen)
    return false;
  if (a.kind != CadAnnotation::Kind::DimAligned)
    return false;
  // Double throughout (issue #1668): the points sit thousands of feet from the origin, where a float
  // cannot hold the difference between them to the precision a dimension reports.
  const double x1 = a.dimExt1X, y1 = a.dimExt1Y, x2 = a.dimExt2X, y2 = a.dimExt2Y;
  double vx = x2 - x1;
  double vy = y2 - y1;
  const double len = std::hypot(vx, vy);
  if (len < 1.e-8)
    return false;
  vx /= len;
  vy /= len;
  const double n0x = -vy;
  const double n0y = vx;
  const double cmx = 0.5 * (x1 + x2);
  const double cmy = 0.5 * (y1 + y2);
  const double dmx = cmx + n0x * a.dimSignedOffset;
  const double dmy = cmy + n0y * a.dimSignedOffset;
  const double t1 = (x1 - dmx) * vx + (y1 - dmy) * vy;
  const double t2 = (x2 - dmx) * vx + (y2 - dmy) * vy;
  *sx1 = static_cast<float>(dmx + vx * t1);
  *sy1 = static_cast<float>(dmy + vy * t1);
  *sx2 = static_cast<float>(dmx + vx * t2);
  *sy2 = static_cast<float>(dmy + vy * t2);
  *tx = static_cast<float>(vx);
  *ty = static_cast<float>(vy);
  *nx = static_cast<float>(n0x);
  *ny = static_cast<float>(n0y);
  *measLen = len;
  return true;
}

inline bool CadDimAlignedGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                                  float* ty, float* nx, float* ny, float* measLen) {
  double len = 0.0;
  if (!measLen || !CadDimAlignedGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, &len))
    return false;
  *measLen = static_cast<float>(len);
  return true;
}

inline bool CadDimLinearGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                                 float* ty, float* nx, float* ny, double* measLen) {
  if (!sx1 || !sy1 || !sx2 || !sy2 || !tx || !ty || !nx || !ny || !measLen)
    return false;
  if (a.kind != CadAnnotation::Kind::DimLinear)
    return false;
  const double x1 = a.dimExt1X, y1 = a.dimExt1Y, x2 = a.dimExt2X, y2 = a.dimExt2Y;  // issue #1668
  const double cmx = 0.5 * (x1 + x2);
  const double cmy = 0.5 * (y1 + y2);
  if (!a.dimLinearVertical) {
    const double span = std::fabs(x2 - x1);
    if (span < 1.e-8)
      return false;
    const double dmy = cmy + a.dimSignedOffset;
    *sx1 = static_cast<float>(x1);
    *sy1 = static_cast<float>(dmy);
    *sx2 = static_cast<float>(x2);
    *sy2 = static_cast<float>(dmy);
    *tx = (x2 >= x1) ? 1.f : -1.f;
    *ty = 0.f;
    *nx = 0.f;
    *ny = 1.f;
    *measLen = span;
  } else {
    const double span = std::fabs(y2 - y1);
    if (span < 1.e-8)
      return false;
    const double dmx = cmx + a.dimSignedOffset;
    *sx1 = static_cast<float>(dmx);
    *sy1 = static_cast<float>(y1);
    *sx2 = static_cast<float>(dmx);
    *sy2 = static_cast<float>(y2);
    *tx = 0.f;
    *ty = (y2 >= y1) ? 1.f : -1.f;
    *nx = 1.f;
    *ny = 0.f;
    *measLen = span;
  }
  return true;
}

inline bool CadDimLinearGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                                 float* ty, float* nx, float* ny, float* measLen) {
  double span = 0.0;
  if (!measLen || !CadDimLinearGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, &span))
    return false;
  *measLen = static_cast<float>(span);
  return true;
}

inline bool CadDimAnyGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                              float* ty, float* nx, float* ny, double* measLen) {
  if (a.kind == CadAnnotation::Kind::DimAligned)
    return CadDimAlignedGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, measLen);
  if (a.kind == CadAnnotation::Kind::DimLinear)
    return CadDimLinearGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, measLen);
  return false;
}

inline bool CadDimAnyGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                              float* ty, float* nx, float* ny, float* measLen) {
  double len = 0.0;
  if (!measLen || !CadDimAnyGeometry(a, sx1, sy1, sx2, sy2, tx, ty, nx, ny, &len))
    return false;
  *measLen = static_cast<float>(len);
  return true;
}

inline bool CadDimAngularComputeFrame(const CadAnnotation& a, float* a1Out, float* a2Out, float* sweepOut, float* bisx,
                                      float* bisy, double* thetaInterior) {
  if (!a1Out || !a2Out || !sweepOut || !bisx || !bisy || !thetaInterior)
    return false;
  if (a.kind != CadAnnotation::Kind::DimAngular)
    return false;
  // Double throughout (issue #1668): the ray directions are differences of points thousands of feet
  // from the origin; in float those differences carried a minute of arc of error.
  const double vx = a.dimAngVertexX, vy = a.dimAngVertexY;
  const double u1x = a.dimExt1X - vx, u1y = a.dimExt1Y - vy;
  const double u2x = a.dimExt2X - vx, u2y = a.dimExt2Y - vy;
  const double l1 = std::hypot(u1x, u1y);
  const double l2 = std::hypot(u2x, u2y);
  if (l1 < 1.e-8 || l2 < 1.e-8)
    return false;
  const double n1x = u1x / l1, n1y = u1y / l1;
  const double n2x = u2x / l2, n2y = u2y / l2;
  const double dot = n1x * n2x + n1y * n2y;
  const double cross = n1x * n2y - n1y * n2x;
  const double a1 = std::atan2(n1y, n1x);
  const double a2 = std::atan2(n2y, n2x);
  const double sweep = CadAngNormalizeMinusPiToPi(a2 - a1);
  // atan2 rather than acos: acos loses precision near 0 and 180 degrees.
  const double theta = std::atan2(std::fabs(cross), dot);
  double bx = n1x + n2x;
  double by = n1y + n2y;
  const double bl = std::hypot(bx, by);
  if (bl > 1.e-6) {
    bx /= bl;
    by /= bl;
  } else {
    bx = -n1y;
    by = n1x;
  }
  const double mid = a1 + 0.5 * sweep;
  if (bx * std::cos(mid) + by * std::sin(mid) < 0.0) {
    bx = -bx;
    by = -by;
  }
  *a1Out = static_cast<float>(a1);
  *a2Out = static_cast<float>(a2);
  *sweepOut = static_cast<float>(sweep);
  *bisx = static_cast<float>(bx);
  *bisy = static_cast<float>(by);
  *thetaInterior = theta;
  return theta > 1.e-7;
}

inline bool CadDimAngularComputeFrame(const CadAnnotation& a, float* a1Out, float* a2Out, float* sweepOut, float* bisx,
                                      float* bisy, float* thetaInterior) {
  double theta = 0.0;
  if (!thetaInterior || !CadDimAngularComputeFrame(a, a1Out, a2Out, sweepOut, bisx, bisy, &theta))
    return false;
  *thetaInterior = static_cast<float>(theta);
  return true;
}
