#include "AlignmentLabels.hpp"

#include <cmath>

namespace alignment {

namespace {

[[nodiscard]] bool IsMajorDisplayedStation(double displayed, double dStart, double majorInterval) {
  if (!(majorInterval > 0.0)) return false;
  const double n = (displayed - dStart) / majorInterval;
  const double k = std::round(n);
  if (k < 1.0 - 1e-9) return false;
  return std::fabs(n - k) <= 1e-6;
}

}  // namespace

std::vector<StationTickLabel> CollectMajorMinorStationTicks(const Alignment& al, double majorInterval,
                                                            double minorInterval, bool wantMajor,
                                                            bool wantMinor) {
  std::vector<StationTickLabel> out;
  if (al.Empty() || !(minorInterval > 0.0) || !(majorInterval > 0.0)) return out;
  const double dStart = al.DisplayedStation(al.StartStation(), true);
  const double dEnd = al.EndStationWithEquations();
  if (!(dEnd > dStart + 1e-9)) return out;

  for (int k = 1;; ++k) {
    const double displayed = dStart + static_cast<double>(k) * minorInterval;
    if (displayed > dEnd - minorInterval + 1e-9) break;
    const bool isMajor = IsMajorDisplayedStation(displayed, dStart, majorInterval);
    if (isMajor && !wantMajor) continue;
    if (!isMajor && !wantMinor) continue;
    const std::vector<double> raws = al.RawStationsForDisplayed(displayed);
    if (raws.empty()) continue;
    out.push_back(StationTickLabel{raws.front(), displayed, isMajor});
  }
  return out;
}

}  // namespace alignment
