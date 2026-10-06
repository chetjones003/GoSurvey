#pragma once

// REQ-378 clause 5 / D-2026-10-05-g (issue #696 P5) — how to bring a drawing's coordinates into a
// project's coordinate system and units as ONE similarity transform (a uniform scale, a rotation about
// the vertical axis, and a shift), so every shape in the drawing stays exact.
//
// Pure: no drawing state. The transform is measured with CS-MAP (through geo::) at the drawing's
// centre; the error it leaves at the drawing's extents is measured too, and a transform that would be
// off by more than \ref kMaxResidualMeters is refused — never applied and reported as fine.

#include <string>

namespace geo {

/// Largest leftover error (meters) at the drawing's extents that a conversion may have (D-2026-10-05-g).
inline constexpr double kMaxResidualMeters = 0.02;

/// Meters in one drawing unit (INSUNITS code: 1 in, 2 ft, 4 mm, 6 m). 0 = unitless / unknown, which
/// compares as "nothing to convert". \p usSurveyFoot picks the foot's definition (REQ-357).
[[nodiscard]] double MetersPerInsUnit(int insUnits, bool usSurveyFoot);

struct ConversionInput {
  std::string fromZone;  ///< the drawing's CS-MAP code; empty = none
  std::string toZone;    ///< the project's; empty = none
  double fromMetersPerUnit = 0.0;  ///< the drawing's unit; 0 = unitless
  double toMetersPerUnit = 0.0;    ///< the project's unit; 0 = the project fixes none
  /// The drawing's extents in WORLD coordinates, in the drawing's unit.
  double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
};

/// world' = shift + scale * Rot(rotationRad) * world   (X and Y);   z' = scaleZ * z.
struct Similarity {
  double scale = 1.0;
  double rotationRad = 0.0;
  double shiftX = 0.0;
  double shiftY = 0.0;
  double scaleZ = 1.0;

  void Apply(double x, double y, double* ox, double* oy) const;
};

struct ConversionPlan {
  bool unitsDiffer = false;  ///< the drawing's unit is not the project's
  bool zoneDiffers = false;  ///< the drawing's coordinate system is not the project's
  bool Needed() const { return unitsDiffer || zoneDiffers; }
  bool ok = false;           ///< the transform could be computed and is within tolerance
  std::string error;         ///< why not, when !ok (REQ-201)
  Similarity transform;
  double residualMeters = 0.0;  ///< worst measured leftover at the extents' corners and edge midpoints
};

/// Which of the two differ, with no computation (the cheap check the preview starts from).
[[nodiscard]] ConversionPlan CompareSettings(const ConversionInput& in);

/// CompareSettings plus the measured transform and its residual. Needs the CS-MAP dictionaries loaded
/// when the coordinate system differs.
[[nodiscard]] ConversionPlan PlanConversion(const ConversionInput& in);

}  // namespace geo
