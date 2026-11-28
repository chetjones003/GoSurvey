#pragma once

/// Angle normalisation shared by dimension geometry (Commands' CadDimGeom.hpp) and the Document
/// entity-kind operations that re-derive a transformed dimension's text angle (issue #1577).

/// Normalizes an angle to [-pi, pi]. Performs at most 16 full turns.
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
