#pragma once

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

double Normalize360(double d);

/// Format a magnitude angle (deg) as D°MM'SS" with `secDecimals` on seconds.
/// Rollover replicates CadFormatBearingCwNorthDegMinSec. When `wrap360`, a
/// seconds/minutes carry past 360° wraps to 0; otherwise degrees just increment.
std::string FormatDms(double deg, int secDecimals, bool wrap360);

std::string FormatDecimalDeg(double deg, int decimals);

}  // namespace anglefmt_detail

/// Format a DIRECTIONAL bearing given in the canonical convention (degrees
/// clockwise from north). Applies base/direction for DD and DMS; Surveyor type
/// renders a quadrant bearing from the N-S meridian (base/direction not used).
[[nodiscard]] std::string FormatBearing(double canonicalCwNorthDeg, const AngleDisplaySettings& s);

/// Format a SWEPT/magnitude angle (e.g. an angular dimension) in degrees. Honors
/// DD vs DMS and precision, but not direction/base or quadrant bearing (a swept
/// angle has no compass quadrant); Surveyor type falls back to DMS. Folds to
/// [0,180] to match the pre-feature CadFormatAngleDegMinSecFromRad.
[[nodiscard]] std::string FormatSweptAngle(double angleDeg, const AngleDisplaySettings& s);
