#pragma once

#include "alignment/Alignment.hpp"

#include <cstdint>
#include <string>
#include <vector>

/// Design vertical profile (REQ-520, GitHub issue #1163).
///
/// A profile is a list of vertical intersection points (PVIs). Between two PVIs it is the straight grade line through
/// both. At an interior PVI with a curve length L it is a symmetric parabolic vertical curve, from L/2 before the PVI to
/// L/2 after it. The profile is separate from the horizontal alignment it belongs to (REQ-480.3, REQ-500.2): the
/// alignment keeps no elevation, and a 3D design path is derived from the pair (DesignPathPoint).
///
/// Pure and dependency-free apart from Alignment, so it links into the test target without the GUI stack.
/// Conventions (survey, not mathematical): elevations and stations are drawing units; grade is rise over run
/// (0.02 = 2%); stations are internal (REQ-483).
namespace alignment {

/// One vertical intersection point (REQ-520.1). `curveLength` 0 means no curve at this point.
struct VerticalPoint {
  double station = 0.0;      ///< Internal station along the alignment.
  double elevation = 0.0;    ///< Elevation of the intersection of the two tangents.
  double curveLength = 0.0;  ///< Length of the symmetric parabolic curve centred on this point; 0 = none.
};

/// A drawing-owned design profile (REQ-520.1). `alignmentId` is the stable id of the alignment it belongs to.
struct VerticalProfile {
  std::uint64_t id = 0;
  std::string name;
  std::uint64_t alignmentId = 0;
  std::vector<VerticalPoint> points;  ///< Stations strictly increasing.
};

/// Why the profile's geometry is not valid (REQ-520.3), or an empty string when it is. A profile with fewer than two
/// PVIs is valid and simply has no geometry; a single PVI with a curve is not.
[[nodiscard]] std::string ProfileGeometryRefusal(const VerticalProfile& p);

/// The answer to a station query (REQ-520.4). `reason` says why there is no value.
struct ProfileSample {
  bool hasValue = false;
  double station = 0.0;
  double elevation = 0.0;
  double grade = 0.0;  ///< Rise over run.
  std::string reason;
};

/// Elevation, grade and profile point at internal `station` (REQ-520.4). Answers only within the first and last PVI.
[[nodiscard]] ProfileSample ProfileAtStation(const VerticalProfile& p, double station);

/// A point in 3D: the alignment's plan point with the profile elevation as z (REQ-520.5).
struct DesignPoint {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

/// The derived 3D design path point at internal `station` (REQ-520.5). False with `why` when the profile does not belong
/// to `a`, the alignment has no geometry, the station is outside the alignment, or the profile has no value there.
[[nodiscard]] bool DesignPathPoint(const Alignment& a, const VerticalProfile& p, double station, DesignPoint* out,
                                   std::string* why);

}  // namespace alignment
