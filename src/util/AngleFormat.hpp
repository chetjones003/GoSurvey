#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

/// Centralized angle/bearing DISPLAY formatting (REQ-021, ADR-004). Pure and
/// UI/GL-free so it is unit-testable in the Domain test target. This controls
/// only how angles are SHOWN; the app's stored/compute convention — degrees
/// clockwise from north — is unchanged, and angle ENTRY keeps that convention.
///
/// At default settings (DegMinSec, precision 1, clockwise, base = north) the
/// output reproduces the pre-feature formatter CadFormatBearingCwNorthDegMinSec.

enum class AngleDisplayType { DecimalDegrees = 0, DegMinSec = 1, Surveyor = 2 };

struct AngleDisplaySettings {
  AngleDisplayType type = AngleDisplayType::DegMinSec;
  int  precision = 1;       ///< decimals on the smallest unit (degrees for DD; seconds for DMS/Surveyor)
  bool clockwise = true;    ///< true: angle increases clockwise from the base
  double baseDeg = 0.0;     ///< canonical CW-from-north degrees of the 0° direction (N=0,E=90,S=180,W=270)
};

namespace anglefmt_detail {

inline double Normalize360(double d) {
  d = std::fmod(d, 360.0);
  if (d < 0.0) d += 360.0;
  return d;
}

/// Format a magnitude angle (deg) as D°MM'SS" with `secDecimals` on seconds,
/// matching the app's pre-feature style (° ' " symbols, no zero padding).
/// Rollover replicates CadFormatBearingCwNorthDegMinSec. When `wrap360`, a
/// seconds/minutes carry past 360° wraps to 0; otherwise degrees just increment.
inline std::string FormatDms(double deg, int secDecimals, bool wrap360) {
  secDecimals = std::clamp(secDecimals, 0, 6);
  deg = Normalize360(deg);
  int id = static_cast<int>(std::floor(deg + 1e-9));
  double minf = (deg - id) * 60.0;
  if (minf < 0.0) minf = 0.0;
  int im = static_cast<int>(std::floor(minf + 1e-9));
  double sec = (minf - im) * 60.0;
  if (sec < 0.0) sec = 0.0;
  const double rollover = 60.0 - 0.5 * std::pow(10.0, -secDecimals);
  if (im >= 60) { im = 0; id = wrap360 ? (id + 1) % 360 : id + 1; }
  if (sec >= rollover) {
    sec = 0.0;
    if (++im >= 60) { im = 0; id = wrap360 ? (id + 1) % 360 : id + 1; }
  }
  char fmt[24];
  std::snprintf(fmt, sizeof(fmt), "%%d\xc2\xb0%%d'%%.%df\"", secDecimals);
  char buf[64];
  std::snprintf(buf, sizeof(buf), fmt, id, im, sec);
  return buf;
}

inline std::string FormatDecimalDeg(double deg, int decimals) {
  decimals = std::clamp(decimals, 0, 8);
  char fmt[8];
  std::snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
  char buf[48];
  std::snprintf(buf, sizeof(buf), fmt, deg);
  return std::string(buf) + "\xc2\xb0";
}

}  // namespace anglefmt_detail

/// Format a DIRECTIONAL bearing given in the canonical convention (degrees
/// clockwise from north). Applies base/direction for DD and DMS; Surveyor type
/// renders a quadrant bearing from the N-S meridian (base/direction not used).
[[nodiscard]] inline std::string FormatBearing(double canonicalCwNorthDeg, const AngleDisplaySettings& s) {
  using anglefmt_detail::Normalize360;
  const double B = Normalize360(canonicalCwNorthDeg);
  if (s.type == AngleDisplayType::Surveyor) {
    if (B <= 90.0)       return "N " + anglefmt_detail::FormatDms(B, s.precision, false) + " E";
    if (B <= 180.0)      return "S " + anglefmt_detail::FormatDms(180.0 - B, s.precision, false) + " E";
    if (B <= 270.0)      return "S " + anglefmt_detail::FormatDms(B - 180.0, s.precision, false) + " W";
    return "N " + anglefmt_detail::FormatDms(360.0 - B, s.precision, false) + " W";
  }
  const double measured = Normalize360(s.clockwise ? (B - s.baseDeg) : (s.baseDeg - B));
  if (s.type == AngleDisplayType::DecimalDegrees)
    return anglefmt_detail::FormatDecimalDeg(measured, s.precision);
  return anglefmt_detail::FormatDms(measured, s.precision, true);
}

/// Format a SWEPT/magnitude angle (e.g. an angular dimension) in degrees. Honors
/// DD vs DMS and precision, but not direction/base or quadrant bearing (a swept
/// angle has no compass quadrant); Surveyor type falls back to DMS. Folds to
/// [0,180] to match the pre-feature CadFormatAngleDegMinSecFromRad.
[[nodiscard]] inline std::string FormatSweptAngle(double angleDeg, const AngleDisplaySettings& s) {
  double a = std::fabs(angleDeg);
  a = std::fmod(a, 360.0);
  if (a > 180.0) a = 360.0 - a;
  if (s.type == AngleDisplayType::DecimalDegrees)
    return anglefmt_detail::FormatDecimalDeg(a, s.precision);
  return anglefmt_detail::FormatDms(a, s.precision, false);
}
