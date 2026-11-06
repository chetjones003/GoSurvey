#include "alignment/AlignmentFeatureLine.hpp"

#include <algorithm>
#include <cmath>

namespace alignment {

std::vector<double> FeatureLineStations(const Alignment& a, double interval, double chordTolerance) {
  std::vector<double> out;
  if (a.Empty() || !(chordTolerance > 0.0) || !std::isfinite(chordTolerance)) return out;
  const double length = a.Length();
  // Entity boundaries (the PC, PT and every junction) and the geometry points the alignment carries.
  for (size_t i = 0; i <= a.Entities().size(); ++i) {
    out.push_back(i == a.Entities().size() ? length : a.EntityStartStation(static_cast<int>(i)) - a.StartStation());
  }
  // A station every `interval` along the alignment, when an interval is given.
  if (interval > 0.0 && std::isfinite(interval)) {
    for (double s = 0.0; s < length; s += interval) out.push_back(s);
  }
  // Each curve is subdivided so the chord between neighbours sits within the tolerance of the arc (REQ-498.1).
  for (size_t i = 0; i < a.Entities().size(); ++i) {
    const Entity& e = a.Entities()[i];
    if (e.kind != EntityKind::Arc) continue;
    const double start = a.EntityStartStation(static_cast<int>(i)) - a.StartStation();
    const double sweep = e.length / e.radius;  // radians
    // Sagitta of one chord spanning `dTheta`: R (1 - cos(dTheta / 2)) <= tolerance.
    const double ratio = std::clamp(1.0 - chordTolerance / e.radius, -1.0, 1.0);
    const double dTheta = std::min(sweep, 2.0 * std::acos(ratio));
    const int pieces = dTheta > 0.0 ? std::max(1, static_cast<int>(std::ceil(sweep / dTheta - 1e-9))) : 1;
    for (int k = 1; k < pieces; ++k) out.push_back(start + e.length * static_cast<double>(k) / pieces);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end(), [](double x, double y) { return std::fabs(x - y) <= 1e-9; }), out.end());
  // Keep only the stations on the alignment.
  out.erase(std::remove_if(out.begin(), out.end(), [&](double s) { return s < -1e-9 || s > length + 1e-9; }), out.end());
  return out;
}

}  // namespace alignment
