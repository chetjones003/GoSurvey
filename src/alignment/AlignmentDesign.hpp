#pragma once

#include "alignment/Alignment.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace alignment {

/// Road or rail criteria (REQ-502 AC3). A road check set applies to centreline, offset and curb-return alignments; a rail
/// set to rail alignments. Miscellaneous alignments take neither.
enum class CheckSetKind : unsigned char { Road = 0, Rail = 1 };

/// One row of the radius-by-speed table: at `speed` and above (up to the next row) the curve radius must be at least `minRadius`.
struct RadiusRequirement {
  double speed = 0.0;
  double minRadius = 0.0;
};

/// A named, drawing-owned check set (REQ-502). A limit of zero means the check is off. Reference values are the user's;
/// GoSurvey ships no regional table as authoritative. Compound spiral length is not checked (A:888F1041).
struct AlignmentCheckSet {
  std::string name;
  CheckSetKind kind = CheckSetKind::Road;
  std::vector<RadiusRequirement> radiusTable;  ///< Kept sorted by speed.
  double minTangent = 0.0;       ///< Shortest tangent between curves.
  double minSpiralLength = 0.0;  ///< Shortest single spiral (not part of a compound).
  double minA = 0.0;             ///< Smallest spiral A parameter.
  double maxA = 0.0;             ///< Largest spiral A parameter.

  bool operator==(const AlignmentCheckSet& o) const {
    return name == o.name && kind == o.kind && minTangent == o.minTangent && minSpiralLength == o.minSpiralLength &&
           minA == o.minA && maxA == o.maxA && radiusTable.size() == o.radiusTable.size() &&
           std::equal(radiusTable.begin(), radiusTable.end(), o.radiusTable.begin(),
                      [](const RadiusRequirement& x, const RadiusRequirement& y) {
                        return x.speed == y.speed && x.minRadius == y.minRadius;
                      });
  }
  bool operator!=(const AlignmentCheckSet& o) const { return !(*this == o); }
};

/// One failed check, worded for the user (REQ-502 AC1). `entity` is 1-based, as the layout table numbers them.
struct DesignFinding {
  int entity = 0;
  std::string text;
};

/// The design speed in force at an internal station: the last speed set at or before it (REQ-502). Zero when none is set.
[[nodiscard]] double DesignSpeedAt(const Alignment& a, double internalStation);

/// The speed an entity is validated against: the highest speed set anywhere across its span (REQ-502 AC2). Zero when none.
[[nodiscard]] double DesignSpeedOverEntity(const Alignment& a, int entityIndex);

/// The minimum radius the table requires at `speed`, or a negative number when the table says nothing for that speed.
[[nodiscard]] double MinRadiusForSpeed(const AlignmentCheckSet& set, double speed);

/// Whether a check set may be applied to an alignment of this type (REQ-502 AC3). Returns false with the reason otherwise.
[[nodiscard]] bool CriteriaAllowed(AlignmentType type, CheckSetKind kind, std::string* whyNot);

/// Every failed check on the alignment, in travel order (REQ-502). Empty when the alignment meets the set, or when it has none.
[[nodiscard]] std::vector<DesignFinding> CheckDesign(const Alignment& a, const AlignmentCheckSet& set, int linearPrecision);

}  // namespace alignment
