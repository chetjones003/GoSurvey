#include "alignment/AlignmentDesign.hpp"

#include "util/NumFormat.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace alignment {
namespace {

std::string Folded(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string Lin(int precision, double v) { return FormatLinear(v, precision) + "'"; }

/// The shared "stations at or before" rule: the last design speed whose station is not after `s`.
const DesignSpeed* LastSpeedAtOrBefore(const Alignment& a, double s) {
  const DesignSpeed* found = nullptr;
  for (const DesignSpeed& d : a.designSpeeds) {
    if (d.internalStation <= s + 1e-9) found = &d;
    else break;
  }
  return found;
}

}  // namespace

const AlignmentCheckSet* FindCheckSet(const std::vector<AlignmentCheckSet>& sets, const std::string& name) {
  if (name.empty()) return nullptr;
  for (const AlignmentCheckSet& s : sets) {
    if (Folded(s.name) == Folded(name)) return &s;
  }
  return nullptr;
}

double DesignSpeedAt(const Alignment& a, double internalStation) {
  const DesignSpeed* d = LastSpeedAtOrBefore(a, internalStation);
  return d != nullptr ? d->speed : 0.0;
}

double DesignSpeedOverEntity(const Alignment& a, int entityIndex) {
  if (entityIndex < 0 || static_cast<size_t>(entityIndex) >= a.Entities().size()) return 0.0;
  const double start = a.EntityStartStation(entityIndex), end = a.EntityEndStation(entityIndex);
  // The speed in force at the start, then any speed set inside the span: the highest one governs (REQ-502 AC2).
  double highest = DesignSpeedAt(a, start);
  for (const DesignSpeed& d : a.designSpeeds) {
    if (d.internalStation > start + 1e-9 && d.internalStation <= end + 1e-9) highest = std::max(highest, d.speed);
  }
  return highest;
}

double MinRadiusForSpeed(const AlignmentCheckSet& set, double speed) {
  // The largest table speed not above `speed` governs. Below the smallest tabled speed the table says nothing.
  double required = -1.0;
  for (const RadiusRequirement& r : set.radiusTable) {
    if (r.speed <= speed + 1e-9) required = r.minRadius;
  }
  return required;
}

bool CriteriaAllowed(AlignmentType type, CheckSetKind kind, std::string* whyNot) {
  if (type == AlignmentType::Miscellaneous) {
    if (whyNot) *whyNot = "a Miscellaneous alignment takes no design criteria";
    return false;
  }
  if (type == AlignmentType::Rail && kind != CheckSetKind::Rail) {
    if (whyNot) *whyNot = "a Rail alignment cannot use a road check set";
    return false;
  }
  if (type != AlignmentType::Rail && kind == CheckSetKind::Rail) {
    if (whyNot) *whyNot = "only a Rail alignment can use a rail check set";
    return false;
  }
  return true;
}

std::vector<DesignFinding> CheckDesign(const Alignment& a, const AlignmentCheckSet& set, int linearPrecision) {
  std::vector<DesignFinding> out;
  const int n = static_cast<int>(a.Entities().size());
  for (int i = 0; i < n; ++i) {
    const Entity& e = a.Entities()[static_cast<size_t>(i)];
    const int number = i + 1;
    if (e.kind == EntityKind::Arc) {
      if (!set.radiusTable.empty()) {
        const double required = MinRadiusForSpeed(set, DesignSpeedOverEntity(a, i));
        if (required > 0.0 && e.radius < required - 1e-9) {
          out.push_back({number, "Curve radius " + Lin(linearPrecision, e.radius) +
                                     " is below the selected minimum design radius of " + Lin(linearPrecision, required) + "."});
        }
      }
    } else if (e.kind == EntityKind::Line) {
      // A tangent between two curves is the one the tangent-length check governs (the tangent intersection of REQ-502).
      const bool betweenCurves = i > 0 && i + 1 < n && a.Entities()[static_cast<size_t>(i) - 1].kind == EntityKind::Arc &&
                                 a.Entities()[static_cast<size_t>(i) + 1].kind == EntityKind::Arc;
      if (set.minTangent > 0.0 && betweenCurves && e.length < set.minTangent - 1e-9) {
        out.push_back({number, "Tangent " + Lin(linearPrecision, e.length) + " is shorter than the minimum tangent of " +
                                   Lin(linearPrecision, set.minTangent) + "."});
      }
    } else if (e.kind == EntityKind::Spiral) {
      SpiralData sd;
      if (!SpiralDataFor(i, a, &sd)) continue;
      // Compound spirals (two spirals in a row) are not length-checked (A:888F1041).
      const bool compound = (i > 0 && a.Entities()[static_cast<size_t>(i) - 1].kind == EntityKind::Spiral) ||
                            (i + 1 < n && a.Entities()[static_cast<size_t>(i) + 1].kind == EntityKind::Spiral);
      if (set.minSpiralLength > 0.0 && !compound && sd.length < set.minSpiralLength - 1e-9) {
        out.push_back({number, "Spiral length " + Lin(linearPrecision, sd.length) + " is below the minimum spiral length of " +
                                   Lin(linearPrecision, set.minSpiralLength) + "."});
      }
      if (set.minA > 0.0 && sd.parameterA < set.minA - 1e-9) {
        out.push_back({number, "Spiral A " + Lin(linearPrecision, sd.parameterA) + " is below the minimum A of " +
                                   Lin(linearPrecision, set.minA) + "."});
      }
      if (set.maxA > 0.0 && sd.parameterA > set.maxA + 1e-9) {
        out.push_back({number, "Spiral A " + Lin(linearPrecision, sd.parameterA) + " is above the maximum A of " +
                                   Lin(linearPrecision, set.maxA) + "."});
      }
    }
  }
  return out;
}

}  // namespace alignment
